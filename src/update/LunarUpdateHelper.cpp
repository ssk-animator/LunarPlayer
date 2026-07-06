#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <cstdio>
#include <cstring>
#include <windows.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

static bool extractZip(const QString &zipPath, const QString &destDir)
{
    QDir().mkpath(destDir);

    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));

    QByteArray zipBytes = zipPath.toUtf8();
    if (!mz_zip_reader_init_file(&archive, zipBytes.constData(), 0))
        return false;

    int fileCount = (int)mz_zip_reader_get_num_files(&archive);
    for (int i = 0; i < fileCount; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat))
            continue;

        QString outPath = destDir + "/" + QString::fromUtf8(stat.m_filename);

        if (stat.m_filename[strlen(stat.m_filename) - 1] == '/') {
            QDir().mkpath(outPath);
            continue;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QByteArray outBytes = outPath.toUtf8();
        if (!mz_zip_reader_extract_to_file(&archive, i, outBytes.constData(), 0)) {
            fprintf(stderr, "[HELPER] Failed to extract: %s\n", stat.m_filename);
            fflush(stderr);
        }
    }

    mz_zip_reader_end(&archive);
    return true;
}

static bool waitForProcess(DWORD pid, int timeoutMs = 120000)
{
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return true;

    DWORD waitResult = WaitForSingleObject(hProcess, timeoutMs);
    CloseHandle(hProcess);
    return (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED);
}

static bool backupDir(const QString &src, const QString &dst)
{
    QDir().mkpath(dst);
    QDir srcDir(src);
    QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        QString s = src + "/" + entry;
        QString d = dst + "/" + entry;
        if (QFileInfo(s).isDir()) {
            if (!backupDir(s, d)) return false;
        } else {
            QFile::remove(d);
            if (!QFile::copy(s, d)) return false;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("LunarUpdateHelper");

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption("archive", "Path to downloaded ZIP", "path"));
    parser.addOption(QCommandLineOption("install-dir", "Target installation directory", "dir"));
    parser.addOption(QCommandLineOption("pid", "PID of running LunarPlayer.exe to wait for", "pid"));
    parser.addOption(QCommandLineOption("version", "Version being installed", "version"));
    parser.process(app);

    QString archivePath = parser.value("archive");
    QString installDir = parser.value("install-dir");
    QString pidStr = parser.value("pid");
    QString version = parser.value("version");

    if (archivePath.isEmpty() || installDir.isEmpty() || pidStr.isEmpty()) {
        fprintf(stderr, "[HELPER] Error: Missing required arguments\n");
        fprintf(stderr, "[HELPER] Usage: LunarUpdateHelper.exe --archive <path> "
                        "--install-dir <dir> --pid <pid> [--version <ver>]\n");
        fflush(stderr);
        return 1;
    }

    fprintf(stderr, "[HELPER] Starting update installation\n");
    fprintf(stderr, "[HELPER] Archive: %s\n", archivePath.toUtf8().constData());
    fprintf(stderr, "[HELPER] Install dir: %s\n", installDir.toUtf8().constData());
    fprintf(stderr, "[HELPER] Waiting for PID: %s\n", pidStr.toUtf8().constData());
    fprintf(stderr, "[HELPER] Version: %s\n", version.toUtf8().constData());
    fflush(stderr);

    // Step 1: Wait for main process to exit
    DWORD pid = pidStr.toULong();
    fprintf(stderr, "[HELPER] Waiting for LunarPlayer.exe to exit...\n");
    fflush(stderr);

    if (!waitForProcess(pid, 120000)) {
        fprintf(stderr, "[HELPER] Timeout waiting for main process\n");
        fflush(stderr);
        return 2;
    }
    fprintf(stderr, "[HELPER] Main process exited\n");
    fflush(stderr);

    // Step 2: Backup current install
    QString updatesDir = QFileInfo(archivePath).absolutePath();
    QString backupPath = updatesDir + "/backup_before_update";

    fprintf(stderr, "[HELPER] Backing up current install...\n");
    fflush(stderr);
    backupDir(installDir, backupPath);

    // Step 3: Extract ZIP to install directory
    fprintf(stderr, "[HELPER] Extracting update...\n");
    fflush(stderr);

    if (!extractZip(archivePath, installDir)) {
        fprintf(stderr, "[HELPER] Extraction failed! Rolling back...\n");
        fflush(stderr);
        backupDir(backupPath, installDir);
        QDir(backupPath).removeRecursively();
        return 3;
    }
    fprintf(stderr, "[HELPER] Extraction complete\n");
    fflush(stderr);

    // Step 4: Verify new exe exists
    QString newExe = installDir + "/LunarPlayer.exe";
    if (!QFileInfo::exists(newExe)) {
        fprintf(stderr, "[HELPER] LunarPlayer.exe not found after extraction! Rolling back...\n");
        fflush(stderr);
        backupDir(backupPath, installDir);
        QDir(backupPath).removeRecursively();
        return 4;
    }
    fprintf(stderr, "[HELPER] Verified LunarPlayer.exe exists\n");
    fflush(stderr);

    // Step 5: Cleanup backup
    QDir(backupPath).removeRecursively();

    // Step 6: Launch new LunarPlayer.exe
    fprintf(stderr, "[HELPER] Launching LunarPlayer.exe...\n");
    fflush(stderr);

    bool launched = QProcess::startDetached(newExe, {});
    if (!launched) {
        fprintf(stderr, "[HELPER] Failed to launch LunarPlayer.exe\n");
        fflush(stderr);
        return 5;
    }

    // Step 7: Delete downloaded archive
    QFile::remove(archivePath);

    fprintf(stderr, "[HELPER] Update complete!\n");
    fflush(stderr);

    return 0;
}
