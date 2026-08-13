#include "traywidget.h"

#include <QMouseEvent>
#include <QPainter>

#include "logger.h"

namespace DtkUpdate
{

    TrayWidget::TrayWidget(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(16, 16);
    }

    void TrayWidget::setState(int updatableCount)
    {
        m_updatable = updatableCount;
        update();
    }

    void TrayWidget::mousePressEvent(QMouseEvent* event)
    {
        if (event->button() == Qt::LeftButton)
            emit clicked();
        QWidget::mousePressEvent(event);
    }

    void TrayWidget::paintEvent(QPaintEvent* event)
    {
        Q_UNUSED(event)
        QPainter p(this);
        // 阶段 6 实现：绘制 deepin 风格图标 + 红点角标
        p.setPen(Qt::NoPen);
        p.setBrush(m_updatable > 0 ? Qt::red : Qt::gray);
        p.drawEllipse(2, 2, 12, 12);
    }

} // namespace DtkUpdate
