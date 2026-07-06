#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Lunar Player");
    app.setOrganizationName("LunarPlayer");
    app.setApplicationVersion("0.1.0-alpha");

    app.setWindowIcon(QIcon(":/app/LunarPlayer.png"));

    MainWindow window;
    window.show();

    if (argc > 1) {
        window.loadFile(QString::fromLocal8Bit(argv[1]));
    }

    return app.exec();
}
