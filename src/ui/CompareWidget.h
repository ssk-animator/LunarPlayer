#ifndef COMPAREWIDGET_H
#define COMPAREWIDGET_H

#include <QWidget>
#include <QLabel>

class VideoWidget;
class QSplitter;

enum class CompareLayout {
    Horizontal,
    Vertical,
    Overlay,
    Difference,
    Wipe,
    Checkerboard
};

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    explicit CompareWidget(QWidget *parent = nullptr);

    VideoWidget *widgetA() const { return m_videoA; }
    VideoWidget *widgetB() const { return m_videoB; }
    QSplitter   *splitter() const { return m_splitter; }

    void setLayoutMode(CompareLayout mode);
    CompareLayout layoutMode() const { return m_layoutMode; }

    void setLabelA(const QString &text);
    void setLabelB(const QString &text);
    void setActiveAudio(int side); // 0 = A, 1 = B

private:
    void rebuildLayout();

    CompareLayout m_layoutMode = CompareLayout::Horizontal;
    VideoWidget *m_videoA = nullptr;
    VideoWidget *m_videoB = nullptr;
    QSplitter   *m_splitter = nullptr;
    QLabel      *m_labelA = nullptr;
    QLabel      *m_labelB = nullptr;
    QLabel      *m_audioBadge = nullptr;
};

#endif // COMPAREWIDGET_H
