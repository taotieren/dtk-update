#include "traywidget.h"

#include <QMouseEvent>
#include <QPainter>

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
        // 有可升级包时绘制红色角标，否则灰色
        p.setPen(Qt::NoPen);
        p.setBrush(m_updatable > 0 ? Qt::red : Qt::gray);
        p.drawEllipse(2, 2, 12, 12);
    }

} // namespace DtkUpdate
