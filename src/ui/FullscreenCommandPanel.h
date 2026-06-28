#ifndef FULLSCREENCOMMANDPANEL_H
#define FULLSCREENCOMMANDPANEL_H

#include <QWidget>
#include <QList>
#include <QString>
#include <QIcon>
#include <QKeySequence>
#include <QAction>
#include <QMenu>
#include <QTimer>

class MainWindow;

class FullscreenCommandPanel : public QWidget {
    Q_OBJECT
public:
    struct RowData {
        QAction *action = nullptr;
        QMenu   *submenu = nullptr;
        QString  text;
        QIcon    icon;
        QKeySequence shortcut;
        bool isSeparator  = false;
        bool isEnabled    = true;
        bool isCheckable  = false;
        bool isChecked    = false;
        bool hasSubmenu   = false;
    };

    explicit FullscreenCommandPanel(MainWindow *mainWindow,
                                    FullscreenCommandPanel *parentPanel = nullptr);
    ~FullscreenCommandPanel() override;

    void buildFromMenu(QMenu *menu);
    void showAt(const QPoint &globalPos);
    void closePanel();

    FullscreenCommandPanel *rootPanel();
    bool isRoot() const { return m_parentPanel == nullptr; }

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    QSize sizeHint() const override;

private:
    void recalcGeometry();
    int  rowAtPoint(const QPoint &pos) const;
    QRect rowRect(int index) const;
    int  rowHeight(int index) const;
    void triggerRow(int index);
    void openSubmenu(int index);
    void closeSubmenu();
    void selectNext();
    void selectPrev();
    QPoint clampPosition(const QPoint &pos) const;

    MainWindow *m_mainWindow = nullptr;
    FullscreenCommandPanel *m_parentPanel = nullptr;
    FullscreenCommandPanel *m_submenu = nullptr;
    int m_submenuRow = -1;
    QList<RowData> m_rows;
    int m_hoveredRow = -1;
    int m_selectedRow = -1;
    bool m_filterInstalled = false;

    static constexpr int kItemHeight     = 34;
    static constexpr int kSeparatorH     = 8;
    static constexpr int kPad            = 6;
    static constexpr int kPanelW         = 240;
    static constexpr int kCornerR        = 8;
    static constexpr int kIconLeft       = 10;
    static constexpr int kIconSize       = 16;
    static constexpr int kCheckLeft      = 8;
    static constexpr int kTextLeft       = 32;
    static constexpr int kIndRight       = 10;
};

#endif
