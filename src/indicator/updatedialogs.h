// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <DDialog>
#include <QObject>
#include <QString>

#include "core/monitor/updatemonitor.h"
#include "core/security/securityadvisor.h"

namespace DtkUpdate
{

    /**
     * @brief 共享的更新提示对话框构建器。
     *
     * 最初 dde-tray 与 generic-tray 各自内联实现了完全一致的 DDialog 构建逻辑，
     * 造成重复维护。此处将其收敛为单一组件，两个托盘前端（以及未来的其它前端）
     * 统一调用，确保提示文案、按钮语义与默认聚焦策略保持一致。
     *
     * 设计要点：
     *  - 使用纯文本消息（\n 换行），与托盘场景的轻量提示风格一致；
     *    MainWindow 的富文本确认框保持独立（HTML 转义、父窗口绑定等不同）。
     *  - "Update Anyway" / 安全提示默认聚焦取消，避免替用户做出危险决定。
     */
    class UpdateDialogs : public QObject
    {
        Q_OBJECT
      public:
        /// 玲珑运行环境异常提示（仅当 backendId == linyaps 时由前端调用）
        static void showLinyapsUnavailable(const QString& reason);

        /// 升级前安全公告 + 预检确认。返回 true 表示用户选择继续（proceed）。
        static bool showSecurityPrompt(const QString& severity,
                                       const QList<SecurityAdvisor::Advisory>& advisories,
                                       const PreCheckReport& pre);

        /// 升级后后检提示（重启/配置/残留等），仅信息展示，不自动执行。
        static void showPostCheck(const PostCheckReport& report);
    };

} // namespace DtkUpdate
