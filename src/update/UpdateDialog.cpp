#include "UpdateDialog.h"
#include "SemVer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

UpdateDialog::UpdateDialog(UpdateManager *manager, QWidget *parent)
    : QDialog(parent), m_manager(manager)
{
    setWindowTitle("Lunar Player Update");
    setMinimumSize(500, 400);
    setModal(true);
    setupUI();

    connect(m_updateBtn, &QPushButton::clicked, this, &UpdateDialog::onUpdateClicked);
    connect(m_skipBtn, &QPushButton::clicked, this, &UpdateDialog::onSkipClicked);
    connect(m_laterBtn, &QPushButton::clicked, this, &UpdateDialog::onLaterClicked);
    connect(m_retryBtn, &QPushButton::clicked, this, &UpdateDialog::onRetryClicked);
}

void UpdateDialog::setupUI()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    m_titleLabel = new QLabel;
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    mainLayout->addWidget(m_titleLabel);

    auto *infoLayout = new QFormLayout;
    infoLayout->setSpacing(6);

    m_currentVersionLabel = new QLabel;
    m_latestVersionLabel = new QLabel;
    m_dateLabel = new QLabel;
    m_sizeLabel = new QLabel;
    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);

    infoLayout->addRow("Current Version:", m_currentVersionLabel);
    infoLayout->addRow("Latest Version:", m_latestVersionLabel);
    infoLayout->addRow("Release Date:", m_dateLabel);
    infoLayout->addRow("Download Size:", m_sizeLabel);

    mainLayout->addLayout(infoLayout);

    auto *line1 = new QFrame;
    line1->setFrameShape(QFrame::HLine);
    mainLayout->addWidget(line1);

    m_releaseNotes = new QTextEdit;
    m_releaseNotes->setReadOnly(true);
    m_releaseNotes->setMaximumHeight(150);
    m_releaseNotes->setPlaceholderText("Release notes will appear here...");
    mainLayout->addWidget(m_releaseNotes);

    auto *line2 = new QFrame;
    line2->setFrameShape(QFrame::HLine);
    mainLayout->addWidget(line2);

    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    m_progressBar->setRange(0, 100);
    mainLayout->addWidget(m_progressBar);

    m_progressLabel = new QLabel;
    m_progressLabel->setVisible(false);
    mainLayout->addWidget(m_progressLabel);

    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);

    auto *buttonLayout = new QHBoxLayout;
    m_updateBtn = new QPushButton("Update Now");
    m_skipBtn = new QPushButton("Skip This Version");
    m_laterBtn = new QPushButton("Later");
    m_retryBtn = new QPushButton("Retry");
    m_retryBtn->setVisible(false);

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_skipBtn);
    buttonLayout->addWidget(m_laterBtn);
    buttonLayout->addWidget(m_retryBtn);
    buttonLayout->addWidget(m_updateBtn);

    mainLayout->addLayout(buttonLayout);
}

void UpdateDialog::updateUI(const QString &title, const QString &body, const QString &detail)
{
    m_titleLabel->setText(title);
    if (!detail.isEmpty()) m_statusLabel->setText(detail);
}

void UpdateDialog::showChecking()
{
    m_titleLabel->setText("Checking for updates...");
    m_currentVersionLabel->setText(QCoreApplication::applicationVersion());
    m_latestVersionLabel->setText("...");
    m_dateLabel->setText("...");
    m_sizeLabel->setText("...");
    m_releaseNotes->clear();
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_statusLabel->setVisible(false);
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(false);
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showUpToDate()
{
    m_titleLabel->setText("You are using the latest version.");
    m_currentVersionLabel->setText(QCoreApplication::applicationVersion());
    m_latestVersionLabel->setText("Up to date");
    m_dateLabel->setText("");
    m_sizeLabel->setText("");
    m_releaseNotes->clear();
    m_statusLabel->setVisible(false);
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("OK");
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showUpdateAvailable(const ReleaseInfo &info)
{
    m_release = info;
    m_titleLabel->setText("A new version of Lunar Player is available.");
    m_currentVersionLabel->setText(QCoreApplication::applicationVersion());
    m_latestVersionLabel->setText(info.version);
    m_dateLabel->setText(info.releaseDate);

    QString sizeStr;
    if (info.downloadSize > 1024 * 1024)
        sizeStr = QString("%1 MB").arg(info.downloadSize / (1024.0 * 1024.0), 0, 'f', 1);
    else
        sizeStr = QString("%1 KB").arg(info.downloadSize / 1024.0, 0, 'f', 0);
    m_sizeLabel->setText(sizeStr);

    m_releaseNotes->setMarkdown(info.releaseNotes);
    m_statusLabel->setVisible(false);
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_updateBtn->setVisible(true);
    m_updateBtn->setText("Update Now");
    m_skipBtn->setVisible(true);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("Later");
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showDownloading()
{
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("Cancel");
    m_retryBtn->setVisible(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressLabel->setVisible(true);
    m_progressLabel->setText("Preparing download...");
    m_statusLabel->setVisible(false);
}

void UpdateDialog::showDownloadProgress(int percent, double speedMbps, int remainingSec)
{
    m_progressBar->setValue(percent);

    int min = remainingSec / 60;
    int sec = remainingSec % 60;
    QString timeStr = QString("%1:%2")
                          .arg(min, 2, 10, QChar('0'))
                          .arg(sec, 2, 10, QChar('0'));

    m_progressLabel->setText(
        QString("%1%  |  %2 MB/s  |  Remaining: %3")
            .arg(percent)
            .arg(speedMbps, 0, 'f', 1)
            .arg(timeStr));
}

void UpdateDialog::showDownloadFailed(const QString &error)
{
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_statusLabel->setVisible(true);
    m_statusLabel->setText("Download failed: " + error);
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("Close");
    m_retryBtn->setVisible(true);
}

void UpdateDialog::showVerifying()
{
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_statusLabel->setVisible(true);
    m_statusLabel->setText("Verifying download integrity...");
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(false);
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showInstallReady()
{
    m_statusLabel->setText("Verification passed. Ready to install.");
    m_updateBtn->setVisible(true);
    m_updateBtn->setText("Install and Restart");
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("Later");
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showInstalling()
{
    m_statusLabel->setText("Installing update...");
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(false);
    m_retryBtn->setVisible(false);
}

void UpdateDialog::showInstallFailed(const QString &error)
{
    m_statusLabel->setText("Installation failed: " + error);
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_updateBtn->setVisible(false);
    m_skipBtn->setVisible(false);
    m_laterBtn->setVisible(true);
    m_laterBtn->setText("Close");
    m_retryBtn->setVisible(false);
}

void UpdateDialog::onUpdateClicked()
{
    if (m_manager->hasPendingUpdate()) {
        showInstalling();
        m_manager->launchPendingUpdate();
        return;
    }

    showDownloading();
    m_manager->startUpdate();
}

void UpdateDialog::onSkipClicked()
{
    if (m_release.isValid())
        m_manager->skipVersion(m_release.version);
    reject();
}

void UpdateDialog::onLaterClicked()
{
    reject();
}

void UpdateDialog::onRetryClicked()
{
    showDownloading();
    m_manager->startUpdate();
}

int UpdateDialog::exec()
{
    showChecking();

    QEventLoop loop;
    bool hasUpdate = false;
    bool hasError = false;
    QString errorMsg;

    connect(m_manager, &UpdateManager::updateAvailable, this,
            [&](const ReleaseInfo &info) {
                showUpdateAvailable(info);
                hasUpdate = true;
                loop.quit();
            });
    connect(m_manager, &UpdateManager::noUpdateAvailable, this, [&]() {
        showUpToDate();
        loop.quit();
    });
    connect(m_manager, &UpdateManager::checkFailed, this, [&](const QString &err) {
        hasError = true;
        errorMsg = err;
        loop.quit();
    });

    m_manager->checkForUpdate(false);
    loop.exec();

    disconnect(m_manager, &UpdateManager::updateAvailable, this, nullptr);
    disconnect(m_manager, &UpdateManager::noUpdateAvailable, this, nullptr);
    disconnect(m_manager, &UpdateManager::checkFailed, this, nullptr);

    if (hasError) {
        showDownloadFailed(errorMsg);
        QDialog::exec();
        return QDialog::Rejected;
    }

    if (!hasUpdate) return QDialog::Accepted;

    connect(m_manager, &UpdateManager::downloadProgressChanged, this,
            &UpdateDialog::showDownloadProgress);
    connect(m_manager, &UpdateManager::verificationPassed, this,
            &UpdateDialog::showInstallReady);
    connect(m_manager, &UpdateManager::verificationFailed, this,
            &UpdateDialog::showDownloadFailed);
    connect(m_manager, &UpdateManager::installationFailed, this,
            &UpdateDialog::showInstallFailed);

    int result = QDialog::exec();

    disconnect(m_manager, &UpdateManager::downloadProgressChanged, this, nullptr);
    disconnect(m_manager, &UpdateManager::verificationPassed, this, nullptr);
    disconnect(m_manager, &UpdateManager::verificationFailed, this, nullptr);
    disconnect(m_manager, &UpdateManager::installationFailed, this, nullptr);

    return result;
}
