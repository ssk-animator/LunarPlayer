#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include "UpdateManager.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(UpdateManager *manager, QWidget *parent = nullptr);

    void showChecking();
    void showUpToDate();
    void showUpdateAvailable(const ReleaseInfo &info);
    void showDownloading();
    void showDownloadProgress(int percent, double speedMbps, int remainingSec);
    void showDownloadFailed(const QString &error);
    void showVerifying();
    void showInstallReady();
    void showInstalling();
    void showInstallFailed(const QString &error);

    int exec() override;

private slots:
    void onUpdateClicked();
    void onSkipClicked();
    void onLaterClicked();
    void onRetryClicked();

private:
    void setupUI();
    void updateUI(const QString &title, const QString &body, const QString &detail = {});

    UpdateManager *m_manager;
    QLabel *m_titleLabel;
    QLabel *m_currentVersionLabel;
    QLabel *m_latestVersionLabel;
    QLabel *m_dateLabel;
    QLabel *m_sizeLabel;
    QLabel *m_statusLabel;
    QTextEdit *m_releaseNotes;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QPushButton *m_updateBtn;
    QPushButton *m_skipBtn;
    QPushButton *m_laterBtn;
    QPushButton *m_retryBtn;
    ReleaseInfo m_release;
};

#endif
