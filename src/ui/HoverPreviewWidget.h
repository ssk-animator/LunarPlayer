#ifndef HOVERPREVIEWWIDGET_H
#define HOVERPREVIEWWIDGET_H

#include <QWidget>
#include <QImage>
#include <QString>

class HoverPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit HoverPreviewWidget(QWidget *parent = nullptr);

    void showThumbnail(const QImage &thumb, const QString &timestamp);
    void hidePreview();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_thumbnail;
    QString m_timestamp;
    int m_thumbWidth = 180;
    int m_thumbHeight = 101;
};

#endif
