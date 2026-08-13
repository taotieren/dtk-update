#pragma once

#include <DWidget>

namespace DtkUpdate
{

    /**
     * @brief 托盘区显示控件（16x16）
     *
     * 根据监控状态切换图标：无更新 / 有更新（红点）。阶段 6 实现绘制逻辑。
     */
    class TrayWidget : public QWidget
    {
        Q_OBJECT
      public:
        explicit TrayWidget(QWidget* parent = nullptr);

        void setState(int updatableCount);

      signals:
        void clicked();

      protected:
        void mousePressEvent(QMouseEvent* event) override;
        void paintEvent(QPaintEvent* event) override;

      private:
        int m_updatable = 0;
    };

} // namespace DtkUpdate
