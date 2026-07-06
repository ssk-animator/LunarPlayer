#ifndef SEEKLIDER_H
#define SEEKLIDER_H

#include <QSlider>
#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QPainter>
#include <QVector>
#include <cstdio>

class SeekSlider : public QSlider {
public:
    using QSlider::QSlider;

    void setMarkers(const QVector<double> &times, double durationSec) {
        m_markerTimes = times;
        m_duration = durationSec;
        update();
    }

    void clearMarkers() {
        m_markerTimes.clear();
        update();
    }

    void setDuration(double dur) {
        m_duration = dur;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            QStyleOptionSlider opt;
            initStyleOption(&opt);

            QRect handleRect = style()->subControlRect(
                QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            if (handleRect.contains(event->pos())) {
                fprintf(stderr, "[SLIDER] mousePress: handle click val=%d\n", value());
                fflush(stderr);
                QSlider::mousePressEvent(event);
                return;
            }

            QRect grooveRect = style()->subControlRect(
                QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
            int newVal = QStyle::sliderValueFromPosition(
                minimum(), maximum(),
                event->pos().x() - grooveRect.x(),
                grooveRect.width());
            newVal = qBound(minimum(), newVal, maximum());

            fprintf(stderr, "[SLIDER] mousePress: groove click val=%d->%d\n", value(), newVal);
            fflush(stderr);
            setValue(newVal);
            QAbstractSlider::setSliderDown(true);
            emit sliderMoved(newVal);

            initStyleOption(&opt);
            QRect newHandle = style()->subControlRect(
                QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
            QMouseEvent fakePress(
                QEvent::MouseButtonPress,
                newHandle.center(), event->globalPosition().toPoint(),
                event->button(), event->buttons(), event->modifiers());
            QSlider::mousePressEvent(&fakePress);

            event->accept();
        } else {
            QSlider::mousePressEvent(event);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        QSlider::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override {
        QSlider::paintEvent(event);

        if (m_markerTimes.isEmpty()) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QStyleOptionSlider opt;
        initStyleOption(&opt);
        QRect groove = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

        int minVal = minimum();
        int maxVal = maximum();
        if (maxVal <= minVal) return;

        int handleHalf = 4;
        int markerY = groove.center().y();
        int markerH = qMin(groove.height() + 4, height() - 4);
        int markerTop = markerY - markerH / 2;

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 200, 50, 200));

        double dur = qMax(m_duration, 0.001);
        for (double sec : m_markerTimes) {
            double frac = sec / dur;
            int x = groove.left() + static_cast<int>(frac * groove.width());
            x = qBound(groove.left() + handleHalf, x, groove.right() - handleHalf);
            painter.drawRect(x - 1, markerTop, 3, markerH);
        }
    }

private:
    QVector<double> m_markerTimes;
    double m_duration = 1.0;
};

#endif
