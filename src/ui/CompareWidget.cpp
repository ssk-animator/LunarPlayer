#include "CompareWidget.h"
#include "VideoWidget.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>

CompareWidget::CompareWidget(QWidget *parent)
    : QWidget(parent)
{
    m_videoA = new VideoWidget(this);
    m_videoB = new VideoWidget(this);

    m_labelA = new QLabel(this);
    m_labelA->setStyleSheet("color: #ccc; font-size: 11px; font-family: 'Segoe UI', monospace; padding: 2px 6px;"
                            "background: rgba(0,0,0,140); border-radius: 3px;");
    m_labelA->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_labelB = new QLabel(this);
    m_labelB->setStyleSheet("color: #ccc; font-size: 11px; font-family: 'Segoe UI', monospace; padding: 2px 6px;"
                            "background: rgba(0,0,0,140); border-radius: 3px;");
    m_labelB->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_audioBadge = new QLabel("AUDIO", this);
    m_audioBadge->setStyleSheet("color: #4a9eff; font-size: 10px; font-weight: bold;"
                                "background: rgba(0,0,0,160); padding: 1px 5px; border-radius: 2px;");

    rebuildLayout();
}

void CompareWidget::setLayoutMode(CompareLayout mode)
{
    m_layoutMode = mode;
    rebuildLayout();
}

void CompareWidget::rebuildLayout()
{
    // Remove old layout/splitter
    if (m_splitter) {
        m_splitter->deleteLater();
        m_splitter = nullptr;
    }

    if (m_layoutMode == CompareLayout::Horizontal) {
        m_splitter = new QSplitter(Qt::Horizontal, this);
    } else if (m_layoutMode == CompareLayout::Vertical) {
        m_splitter = new QSplitter(Qt::Vertical, this);
    } else {
        // For Overlay/Difference/Wipe/Checkerboard — still horizontal layout for now
        // (overlay render will be implemented in future versions)
        m_splitter = new QSplitter(Qt::Horizontal, this);
    }

    m_splitter->addWidget(m_videoA);
    m_splitter->addWidget(m_videoB);
    m_splitter->setHandleWidth(2);
    m_splitter->setStyleSheet("QSplitter::handle { background: rgba(255,255,255,30); }");

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_splitter);

    // Position labels as overlays
    m_labelA->setParent(this);
    m_labelB->setParent(this);
    m_labelA->raise();
    m_labelB->raise();
    m_labelA->move(8, 8);
    m_labelB->move(8, 28);

    m_audioBadge->setParent(this);
    m_audioBadge->raise();
    m_audioBadge->move(8, 48);

    // Install resize filter to reposition labels
    // (simple approach: poll in updateTimerTick)
}

void CompareWidget::setLabelA(const QString &text)
{
    m_labelA->setText(text);
}

void CompareWidget::setLabelB(const QString &text)
{
    m_labelB->setText(text);
}

void CompareWidget::setActiveAudio(int side)
{
    if (side == 0)
        m_audioBadge->setText("AUDIO: A");
    else
        m_audioBadge->setText("AUDIO: B");
    m_audioBadge->adjustSize();
}
