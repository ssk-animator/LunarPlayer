#include <QtTest/QtTest>
#include <QApplication>
#include <QSignalSpy>
#include "../src/ui/SeekSlider.h"

class TestSeekSlider : public QObject {
    Q_OBJECT
private slots:
    void testTrackClickEmitsSignals();
    void testTrackClickUpdatesValue();
    void testHandleClickWorks();
};

void TestSeekSlider::testTrackClickEmitsSignals()
{
    SeekSlider slider(Qt::Horizontal);
    slider.setRange(0, 1000);
    slider.setValue(0);
    slider.resize(400, 30);
    slider.show();

    QSignalSpy pressedSpy(&slider, &QSlider::sliderPressed);
    QSignalSpy movedSpy(&slider, &QSlider::sliderMoved);
    QSignalSpy releasedSpy(&slider, &QSlider::sliderReleased);

    // Click at 75% of the slider (far from handle at 0)
    QPoint clickPos(300, 15);
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, clickPos);

    QVERIFY2(pressedSpy.count() >= 1,
             qPrintable(QString("sliderPressed not emitted (count=%1)").arg(pressedSpy.count())));
    QVERIFY2(movedSpy.count() >= 1,
             qPrintable(QString("sliderMoved not emitted (count=%1)").arg(movedSpy.count())));

    int emittedValue = movedSpy.last().at(0).toInt();
    QVERIFY2(emittedValue > 500,
             qPrintable(QString("Expected value > 500, got %1").arg(emittedValue)));

    qDebug() << "Track click: value=" << slider.value() << " sliderMoved emitted value=" << emittedValue;

    // Drag a bit
    QTest::mouseMove(&slider, QPoint(350, 15));
    QVERIFY2(movedSpy.count() >= 2,
             qPrintable(QString("sliderMoved not emitted during drag (count=%1)").arg(movedSpy.count())));

    // Release
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(350, 15));
    QVERIFY2(releasedSpy.count() >= 1,
             qPrintable(QString("sliderReleased not emitted (count=%1)").arg(releasedSpy.count())));

    qDebug() << "PASS: Track click -> jump + drag + release works";
}

void TestSeekSlider::testTrackClickUpdatesValue()
{
    SeekSlider slider(Qt::Horizontal);
    slider.setRange(0, 1000);
    slider.setValue(0);
    slider.resize(400, 30);
    slider.show();

    // Click at 50% of the slider width
    QPoint clickPos(200, 15);
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, clickPos);

    QVERIFY2(slider.value() >= 400 && slider.value() <= 600,
             qPrintable(QString("Expected value ~500, got %1").arg(slider.value())));

    qDebug() << "Track click value update: value=" << slider.value();
    qDebug() << "PASS: Track click updates slider value";
}

void TestSeekSlider::testHandleClickWorks()
{
    SeekSlider slider(Qt::Horizontal);
    slider.setRange(0, 1000);
    slider.setValue(500);
    slider.resize(400, 30);
    slider.show();

    QSignalSpy pressedSpy(&slider, &QSlider::sliderPressed);

    // Handle should be at 50% = ~200px
    // Use QTest::mousePress with precise coordinates
    QPoint handleCenter(200, 15);
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, handleCenter);

    QVERIFY2(pressedSpy.count() >= 1,
             qPrintable(QString("sliderPressed not emitted for handle click (count=%1)").arg(pressedSpy.count())));

    qDebug() << "Handle click: value=" << slider.value();
    qDebug() << "PASS: Handle click works";
}

QTEST_MAIN(TestSeekSlider)
#include "test_seekslider.moc"
