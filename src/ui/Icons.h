#ifndef ICONS_H
#define ICONS_H

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QCoreApplication>

class LunarIcons {
public:
    static QIcon play() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPainterPath path;
        path.moveTo(4, 0);
        path.lineTo(17, 7);
        path.lineTo(4, 15);
        path.closeSubpath();
        p.drawPath(path);
        return QIcon(pm);
    }

    static QIcon pause() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawRoundedRect(3, 2, 5, 16, 1, 1);
        p.drawRoundedRect(12, 2, 5, 16, 1, 1);
        return QIcon(pm);
    }

    static QIcon frameNext() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPainterPath path;
        path.moveTo(2, 2);
        path.lineTo(12, 10);
        path.lineTo(2, 18);
        path.closeSubpath();
        p.drawPath(path);
        p.drawRect(14, 2, 2, 16);
        return QIcon(pm);
    }

    static QIcon framePrev() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPainterPath path;
        path.moveTo(18, 2);
        path.lineTo(8, 10);
        path.lineTo(18, 18);
        path.closeSubpath();
        p.drawPath(path);
        p.drawRect(4, 2, 2, 16);
        return QIcon(pm);
    }

    static QIcon volumeHigh() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(2, 7);
        path.lineTo(5, 7);
        path.lineTo(10, 3);
        path.lineTo(10, 17);
        path.lineTo(5, 13);
        path.lineTo(2, 13);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRect(11, 5, 3, 10), -50*16, 100*16);
        p.drawArc(QRect(14, 2, 3, 16), -50*16, 100*16);
        return QIcon(pm);
    }

    static QIcon volumeLow() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(2, 7);
        path.lineTo(5, 7);
        path.lineTo(10, 3);
        path.lineTo(10, 17);
        path.lineTo(5, 13);
        path.lineTo(2, 13);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRect(11, 5, 3, 10), -50*16, 100*16);
        return QIcon(pm);
    }

    static QIcon volumeMute() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(2, 7);
        path.lineTo(5, 7);
        path.lineTo(10, 3);
        path.lineTo(10, 17);
        path.lineTo(5, 13);
        path.lineTo(2, 13);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(13, 6, 18, 14);
        p.drawLine(18, 6, 13, 14);
        return QIcon(pm);
    }

    static QIcon fullscreen() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.8));
        p.setBrush(Qt::NoBrush);
        int m = 2;
        int cornerLen = 5;
        p.drawLine(m, m + cornerLen, m, m);
        p.drawLine(m, m, m + cornerLen, m);
        p.drawLine(20 - m - cornerLen, m, 20 - m, m);
        p.drawLine(20 - m, m, 20 - m, m + cornerLen);
        p.drawLine(20 - m, 20 - m - cornerLen, 20 - m, 20 - m);
        p.drawLine(20 - m, 20 - m, 20 - m - cornerLen, 20 - m);
        p.drawLine(m + cornerLen, 20 - m, m, 20 - m);
        p.drawLine(m, 20 - m, m, 20 - m - cornerLen);
        return QIcon(pm);
    }

    static QIcon fullscreenExit() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.8));
        p.setBrush(Qt::NoBrush);
        int cornerLen = 5;
        p.drawLine(7, 2, 2, 2);
        p.drawLine(2, 2, 2, 7);
        p.drawLine(18, 2, 13, 2);
        p.drawLine(18, 2, 18, 7);
        p.drawLine(18, 18, 18, 13);
        p.drawLine(18, 18, 13, 18);
        p.drawLine(2, 18, 2, 13);
        p.drawLine(2, 18, 7, 18);
        return QIcon(pm);
    }

    static QIcon audio() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(3, 7);
        path.lineTo(6, 7);
        path.lineTo(11, 3);
        path.lineTo(11, 17);
        path.lineTo(6, 13);
        path.lineTo(3, 13);
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(QRect(12, 5, 3, 10), -50*16, 100*16);
        p.drawArc(QRect(15, 2, 3, 16), -50*16, 100*16);
        return QIcon(pm);
    }

    static QIcon video() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(2, 3, 16, 12, 2, 2);
        p.drawLine(18, 7, 20, 5);
        p.drawLine(18, 13, 20, 15);
        return QIcon(pm);
    }

    static QIcon subtitle() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QFont f = p.font();
        f.setPixelSize(13);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(0, 0, 20, 20), Qt::AlignCenter, "CC");
        return QIcon(pm);
    }

    static QIcon settings() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRect(6, 6, 8, 8));
        p.drawEllipse(QRect(8, 8, 4, 4));
        p.drawLine(10, 0, 10, 5);
        p.drawLine(10, 15, 10, 20);
        p.drawLine(0, 10, 5, 10);
        p.drawLine(15, 10, 20, 10);
        return QIcon(pm);
    }

    static QIcon open() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        path.moveTo(2, 16);
        path.lineTo(2, 5);
        path.lineTo(8, 5);
        path.lineTo(10, 3);
        path.lineTo(18, 3);
        path.lineTo(18, 16);
        path.closeSubpath();
        p.drawPath(path);
        return QIcon(pm);
    }

    static QIcon seekBack() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPainterPath path;
        path.moveTo(12, 3);
        path.lineTo(5, 10);
        path.lineTo(12, 17);
        path.closeSubpath();
        path.moveTo(17, 3);
        path.lineTo(10, 10);
        path.lineTo(17, 17);
        path.closeSubpath();
        p.drawPath(path);
        return QIcon(pm);
    }

    static QIcon seekForward() {
        QPixmap pm(20, 20);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QPainterPath path;
        path.moveTo(8, 3);
        path.lineTo(15, 10);
        path.lineTo(8, 17);
        path.closeSubpath();
        path.moveTo(3, 3);
        path.lineTo(10, 10);
        path.lineTo(3, 17);
        path.closeSubpath();
        p.drawPath(path);
        return QIcon(pm);
    }
};

#endif



