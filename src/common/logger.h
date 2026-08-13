#pragma once

#include <QLoggingCategory>

// 统一日志分类，便于过滤
Q_DECLARE_LOGGING_CATEGORY(dtkUpdateCore)
Q_DECLARE_LOGGING_CATEGORY(dtkUpdateTray)
Q_DECLARE_LOGGING_CATEGORY(dtkUpdateUi)
Q_DECLARE_LOGGING_CATEGORY(dtkUpdateDaemon)

// 通过 DLogManager 注册后使用 qCInfo/qCWarning/qCDebug
