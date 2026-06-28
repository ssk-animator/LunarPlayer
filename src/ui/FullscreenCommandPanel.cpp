#include "FullscreenCommandPanel.h"
#include "ui/MainWindow.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QCursor>
#include <QStyle>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FullscreenCommandPanel::FullscreenCommandPanel(MainWindow *mainWindow,
                                               FullscreenCommandPanel *parentPanel)
    : QWidget(mainWindow)
    , m_mainWindow(mainWindow)
    , m_parentPanel(parentPanel)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover);
    // Translucent background so the rounded corners punch through
    setAttribute(Qt::WA_TranslucentBackground);
    // The panel is a plain child widget, NEVER a top-level popup
    setWindowFlags(Qt::Widget | Qt::FramelessWindowHint);
    hide();

    // Only the root panel monitors outside clicks
    if (!parentPanel) {
        qApp->installEventFilter(this);
        m_filterInstalled = true;
    }
}

FullscreenCommandPanel::~FullscreenCommandPanel()
{
    if (m_filterInstalled)
        qApp->removeEventFilter(this);
    delete m_submenu;
}

// ---------------------------------------------------------------------------
// Build content from a QMenu tree
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::buildFromMenu(QMenu *menu)
{
    m_rows.clear();
    for (QAction *a : menu->actions()) {
        RowData r;
        r.action       = a;
        r.submenu      = a->menu();
        r.text         = a->text().remove(QChar('&'));
        r.icon         = a->icon();
        r.shortcut     = a->shortcut();
        r.isSeparator  = a->isSeparator();
        r.isEnabled    = a->isEnabled();
        r.isCheckable  = a->isCheckable();
        r.isChecked    = a->isChecked();
        r.hasSubmenu   = (r.submenu != nullptr);
        m_rows.append(r);
    }
    recalcGeometry();
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::recalcGeometry()
{
    int h = kPad;
    for (int i = 0; i < m_rows.size(); ++i)
        h += rowHeight(i);
    h += kPad;
    setFixedSize(kPanelW, h);
}

int FullscreenCommandPanel::rowHeight(int index) const
{
    if (index < 0 || index >= m_rows.size()) return 0;
    return m_rows[index].isSeparator ? kSeparatorH : kItemHeight;
}

QRect FullscreenCommandPanel::rowRect(int index) const
{
    int y = kPad;
    for (int i = 0; i < index; ++i)
        y += rowHeight(i);
    return QRect(kPad, y, width() - 2 * kPad, rowHeight(index));
}

int FullscreenCommandPanel::rowAtPoint(const QPoint &pos) const
{
    if (!rect().contains(pos)) return -1;
    int y = kPad;
    for (int i = 0; i < m_rows.size(); ++i) {
        int h = rowHeight(i);
        if (pos.y() >= y && pos.y() < y + h)
            return m_rows[i].isSeparator ? -1 : i;
        y += h;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Position helpers
// ---------------------------------------------------------------------------

QPoint FullscreenCommandPanel::clampPosition(const QPoint &pos) const
{
    QSize sh = sizeHint();
    QSize ws = m_mainWindow->size();
    int x = qBound(0, pos.x(), ws.width()  - sh.width());
    int y = qBound(0, pos.y(), ws.height() - sh.height());
    return QPoint(x, y);
}

QSize FullscreenCommandPanel::sizeHint() const
{
    return QSize(kPanelW, height());
}

// ---------------------------------------------------------------------------
// Show / close
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::showAt(const QPoint &globalPos)
{
    QPoint local = m_mainWindow->mapFromGlobal(globalPos);
    move(clampPosition(local));
    show();
    raise();
    setFocus();
}

void FullscreenCommandPanel::closePanel()
{
    closeSubmenu();
    hide();

    // If this panel is a submenu being closed directly (not via the
    // parent's closeSubmenu), clear the parent's back-pointer so it
    // never holds a dangling reference after deleteLater() runs.
    if (m_parentPanel && m_parentPanel->m_submenu == this) {
        m_parentPanel->m_submenu = nullptr;
        m_parentPanel->m_submenuRow = -1;
    }

    if (isRoot() && m_filterInstalled) {
        qApp->removeEventFilter(this);
        m_filterInstalled = false;
    }
    emit closed();
    deleteLater();
}

// ---------------------------------------------------------------------------
// Submenu management
// ---------------------------------------------------------------------------

FullscreenCommandPanel *FullscreenCommandPanel::rootPanel()
{
    FullscreenCommandPanel *r = this;
    while (r->m_parentPanel)
        r = r->m_parentPanel;
    return r;
}

void FullscreenCommandPanel::openSubmenu(int index)
{
    closeSubmenu();
    if (index < 0 || index >= m_rows.size()) return;
    if (!m_rows[index].hasSubmenu) return;

    auto *sub = new FullscreenCommandPanel(m_mainWindow, this);
    sub->buildFromMenu(m_rows[index].submenu);

    // position to the right, aligned with the row
    QRect rr = rowRect(index);
    QPoint subPos(pos().x() + width(), pos().y() + rr.y());

    // if it would go past the right edge, open to the left
    if (subPos.x() + sub->width() > m_mainWindow->width())
        subPos.setX(pos().x() - sub->width());

    sub->move(sub->clampPosition(subPos));
    sub->show();
    sub->raise();
    m_submenu    = sub;
    m_submenuRow = index;
}

void FullscreenCommandPanel::closeSubmenu()
{
    if (m_submenu) {
        // Prevent the submenu's deleteLater from re-entering
        FullscreenCommandPanel *tmp = m_submenu;
        m_submenu    = nullptr;
        m_submenuRow = -1;
        tmp->closePanel();
    }
}

// ---------------------------------------------------------------------------
// Row triggering
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::triggerRow(int index)
{
    if (index < 0 || index >= m_rows.size()) return;
    RowData &r = m_rows[index];
    if (!r.isEnabled || r.isSeparator) return;

    if (r.hasSubmenu) {
        openSubmenu(index);
        return;
    }

    if (r.action) {
        if (r.isCheckable)
            r.action->setChecked(!r.isChecked);
        r.action->trigger();
    }

    // Guard against re-entrancy: the action's slot may have already
    // closed this panel (e.g. via a modal dialog that re-entered
    // showContextMenu).  Check visibility before calling closePanel.
    if (isVisible())
        closePanel();
}

// ---------------------------------------------------------------------------
// Keyboard navigation
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::selectNext()
{
    int start = qMax(0, m_selectedRow);
    for (int i = start + 1; i < m_rows.size(); ++i) {
        if (!m_rows[i].isSeparator && m_rows[i].isEnabled) {
            m_selectedRow = i;
            m_hoveredRow  = i;
            update();
            return;
        }
    }
}

void FullscreenCommandPanel::selectPrev()
{
    int start = (m_selectedRow < 0) ? m_rows.size() : m_selectedRow;
    for (int i = start - 1; i >= 0; --i) {
        if (!m_rows[i].isSeparator && m_rows[i].isEnabled) {
            m_selectedRow = i;
            m_hoveredRow  = i;
            update();
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Event filter  –  outside-click detection
// ---------------------------------------------------------------------------

bool FullscreenCommandPanel::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        QWidget *w = qobject_cast<QWidget*>(obj);
        if (!w) return false;

        // Walk up to find if the click landed inside any FullscreenCommandPanel
        bool inside = false;
        for (QWidget *cur = w; cur; cur = cur->parentWidget()) {
            if (qobject_cast<FullscreenCommandPanel*>(cur)) {
                inside = true;
                break;
            }
        }
        if (!inside)
            rootPanel()->closePanel();
    }
    return false;
}

// ---------------------------------------------------------------------------
// Mouse events
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::mouseMoveEvent(QMouseEvent *event)
{
    int row = rowAtPoint(event->pos());
    if (row != m_hoveredRow) {
        m_hoveredRow = row;
        m_selectedRow = row;
        update();

        // Submenu switching on hover
        if (row >= 0 && m_rows[row].hasSubmenu && row != m_submenuRow) {
            openSubmenu(row);
        } else if (row < 0 || !m_rows[row].hasSubmenu) {
            if (m_submenuRow >= 0) {
                // Only close submenu if the mouse is also outside the submenu
                QPoint subLocal = m_submenu
                    ? m_submenu->mapFromGlobal(QCursor::pos())
                    : QPoint();
                if (!m_submenu || !m_submenu->rect().contains(subLocal))
                    closeSubmenu();
            }
        }
    }
}

void FullscreenCommandPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int row = rowAtPoint(event->pos());
        if (row >= 0)
            triggerRow(row);
    }
}

void FullscreenCommandPanel::leaveEvent(QEvent *)
{
    m_hoveredRow = -1;
    update();
}

// ---------------------------------------------------------------------------
// Keyboard events
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        selectPrev();
        break;
    case Qt::Key_Down:
        selectNext();
        break;
    case Qt::Key_Right:
        if (m_selectedRow >= 0 && m_rows[m_selectedRow].hasSubmenu)
            openSubmenu(m_selectedRow);
        break;
    case Qt::Key_Left:
        if (m_parentPanel) {
            m_parentPanel->m_selectedRow = m_parentPanel->m_submenuRow;
            m_parentPanel->setFocus();
            m_parentPanel->closeSubmenu();
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_selectedRow >= 0)
            triggerRow(m_selectedRow);
        break;
    case Qt::Key_Escape:
        if (m_parentPanel) {
            m_parentPanel->m_selectedRow = m_parentPanel->m_submenuRow;
            m_parentPanel->setFocus();
            m_parentPanel->closeSubmenu();
        } else {
            closePanel();
        }
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void FullscreenCommandPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int W = width();
    const int H = height();

    // ---- background ----
    QPainterPath bg;
    bg.addRoundedRect(1, 1, W - 2, H - 2, kCornerR, kCornerR);
    p.fillPath(bg, QColor(43, 43, 43));

    // ---- border ----
    p.setPen(QPen(QColor(255, 255, 255, 20), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(0.5, 0.5, W - 1, H - 1), kCornerR, kCornerR);

    // ---- rows ----
    for (int i = 0; i < m_rows.size(); ++i) {
        const RowData &row = m_rows[i];
        QRect rr = rowRect(i);
        if (rr.isEmpty()) continue;

        if (row.isSeparator) {
            int midY = rr.center().y();
            p.setPen(QPen(QColor(255, 255, 255, 20), 1));
            p.drawLine(rr.left() + 8, midY, rr.right() - 8, midY);
            continue;
        }

        // ---- hover highlight ----
        if (i == m_hoveredRow || i == m_selectedRow) {
            QPainterPath hl;
            hl.addRoundedRect(rr.adjusted(2, 1, -2, -1), 6, 6);
            p.fillPath(hl, QColor(255, 255, 255, 25));
        }

        QColor fg = row.isEnabled ? QColor(245, 245, 245)
                                  : QColor(245, 245, 245, 80);

        // ---- checkable indicator ----
        if (row.isCheckable) {
            QRect chkRect(rr.left() + kCheckLeft,
                          rr.top() + (rr.height() - kIconSize) / 2,
                          kIconSize, kIconSize);
            if (row.isChecked) {
                // filled circle + checkmark
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(70, 130, 255));
                p.drawRoundedRect(chkRect, 3, 3);
                p.setPen(QPen(Qt::white, 2));
                QFont cf = p.font();
                cf.setPixelSize(11);
                p.setFont(cf);
                p.drawText(chkRect, Qt::AlignCenter, QStringLiteral("\u2713"));
            } else {
                p.setPen(QPen(fg, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(chkRect, 3, 3);
            }
        }

        // ---- icon ----
        if (!row.icon.isNull()) {
            QIcon::Mode iconMode = row.isEnabled ? QIcon::Normal : QIcon::Disabled;
            QRect iconRect(rr.left() + kIconLeft,
                           rr.top() + (rr.height() - kIconSize) / 2,
                           kIconSize, kIconSize);
            row.icon.paint(&p, iconRect, Qt::AlignCenter, iconMode);
        }

        // ---- text ----
        QFont tf = p.font();
        tf.setPixelSize(12);
        p.setFont(tf);

        int textLeft   = kTextLeft;
        int textRight  = rr.right() - kIndRight - 4;
        // Reserve space for shortcut text
        if (!row.shortcut.isEmpty()) {
            QFontMetrics fm(tf);
            int sw = fm.horizontalAdvance(row.shortcut.toString(QKeySequence::NativeText)) + 12;
            textRight -= sw;
        }

        QRect textRect(textLeft, rr.top(), textRight - textLeft, rr.height());
        p.setPen(fg);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, row.text);

        // ---- shortcut ----
        if (!row.shortcut.isEmpty()) {
            p.setPen(QColor(245, 245, 245, 100));
            QFont sf = p.font();
            sf.setPixelSize(11);
            p.setFont(sf);
            QString sc = row.shortcut.toString(QKeySequence::NativeText);
            QRect scRect(rr.left() + kTextLeft, rr.top(),
                         rr.width() - kTextLeft - kIndRight - 4, rr.height());
            p.drawText(scRect, Qt::AlignVCenter | Qt::AlignRight, sc);
        }

        // ---- submenu indicator ----
        if (row.hasSubmenu) {
            p.setPen(QColor(245, 245, 245, 120));
            QFont indf = p.font();
            indf.setPixelSize(10);
            p.setFont(indf);
            QRect indRect(rr.left(), rr.top(),
                          rr.width() - kIndRight, rr.height());
            p.drawText(indRect, Qt::AlignVCenter | Qt::AlignRight,
                       QStringLiteral("\u25B6"));
        }
    }
}
