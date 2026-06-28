#ifndef MEDIAINFODIALOG_H
#define MEDIAINFODIALOG_H

#include <QDialog>
#include "MediaInfo.h"

class QTableWidget;

class MediaInfoDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MediaInfoDialog(const MediaInfo &info, QWidget *parent = nullptr);

private:
    void addRow(QTableWidget *table, const QString &label, const QString &value);
};

#endif
