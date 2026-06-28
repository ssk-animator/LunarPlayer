#include "GradientOverlay.h"
#include <QPainter>
#include <QLinearGradient>

GradientOverlay::GradientOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
}

void GradientOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QLinearGradient g(0, 0, 0, height());
    g.setColorAt(0.0, QColor(18, 18, 18, 0));
    g.setColorAt(0.2, QColor(18, 18, 18, 50));
    g.setColorAt(0.6, QColor(18, 18, 18, 150));
    g.setColorAt(1.0, QColor(18, 18, 18, 230));
    p.fillRect(rect(), g);
}
