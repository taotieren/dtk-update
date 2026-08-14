#pragma once

#include <DListView>
#include <DWidget>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QWidget>

#include "core/monitor/updatemonitor.h"

namespace DtkUpdate
{

    /**
     * @brief 左键弹出的更新概览面板（quick panel）
     *
     * 展示可升级包数量与列表摘要、安全等级、以及"立即更新"按钮。
     * 使用 DTK 控件，符合 deepin 视觉风格。
     */
    class TrayPopup : public QWidget
    {
        Q_OBJECT
      public:
        explicit TrayPopup(UpdateMonitor* monitor, QWidget* parent = nullptr);

        // 面板显示/刷新时调用
        void refresh();

      private slots:
        void onUpdateClicked();

      private:
        UpdateMonitor* m_monitor;
        Dtk::Widget::DListView* m_list;
        QLabel* m_summary;
        QLabel* m_sevLabel;
        QPushButton* m_updateBtn;
    };

} // namespace DtkUpdate
