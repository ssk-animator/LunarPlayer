#include <QtTest/QtTest>
#include <QImage>
#include "../src/ui/MainWindow.h"

class TestMediaSession : public QObject {
    Q_OBJECT

private slots:
    void testDefaultState()
    {
        MediaSession session;
        QVERIFY(!session.isOpen());
        QCOMPARE(session.durationSec(), 0.0);
        QCOMPARE(session.fps(), 24);
        QVERIFY(session.currentFrame().isNull());
    }

    void testCloseWithoutOpen()
    {
        MediaSession session;
        session.close();
        QVERIFY(!session.isOpen());
    }

    void testSeekWithoutOpen()
    {
        MediaSession session;
        session.seekSec(10.0);
        QVERIFY(!session.isOpen());
    }
};

QTEST_MAIN(TestMediaSession)
#include "test_mediasession.moc"
