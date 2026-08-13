#pragma once

#include "core/monitor/updatemonitor.h"
#include "core/package/packagebackend.h"
#include "common/appconfig.h"

#include <QObject>

namespace DtkUpdate {

/**
 * @brief 后台常驻服务（D-Bus）
 *
 * 由 systemd user service 拉起，提供 D-Bus 接口供 tray/ui 查询更新状态，
 * 避免多个前端各自轮询。tray 与 gui 可连接同一服务。
 */
class Daemon : public QObject {
    Q_OBJECT
public:
    explicit Daemon(QObject *parent = nullptr);
    ~Daemon() override;

    bool registerOnBus();

public slots:
    void checkNow();
    QVariantMap status() const;

signals:
    void statusChanged(const QVariantMap &status);

private:
    PackageBackend *m_backend;
    AppConfig *m_config;
    UpdateMonitor *m_monitor;
    SecurityAdvisor *m_advisor = nullptr;
};

}  // namespace DtkUpdate
