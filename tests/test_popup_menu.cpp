#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QMenu>
#include <QVBoxLayout>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QMainWindow win;
    win.resize(800, 600);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);

    auto *btn = new QPushButton("Click for Menu");
    layout->addWidget(btn);

    auto *menu = new QMenu(&win);
    menu->addAction("Open File...");
    menu->addAction("Close");
    menu->addSeparator();
    menu->addAction("Exit");

    QObject::connect(btn, &QPushButton::clicked, [&]() {
        QPoint gp = btn->mapToGlobal(QPoint(0, btn->height()));
        qDebug() << "Popup at:" << gp << "menu visible:" << menu->isVisible();
        menu->popup(gp);
        qDebug() << "After popup - visible:" << menu->isVisible() << "hidden:" << menu->isHidden();
    });

    win.setCentralWidget(central);
    win.showFullScreen();

    return app.exec();
}
