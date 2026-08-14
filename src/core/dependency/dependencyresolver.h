#pragma once

#include <QObject>
#include <QStringList>

#include "package/packageinfo.h"

class QRegularExpression;

namespace DtkUpdate
{

    class PackageBackend;

    /**
     * @brief 依赖关系解析
     *
     * 通过 PackageBackend::simulateInstall() 获取后端原生干跑输出，
     * 再按后端类型分流解析，提取「将直接安装」与「将被移除」的包名。
     *
     * 测试可直接调用 parseSimulateOutput()（APT 格式）做解析单测，
     * 不依赖真实 apt 进程。
     */
    class DependencyResolver : public QObject
    {
        Q_OBJECT
      public:
        explicit DependencyResolver(QObject* parent = nullptr);

        /** 绑定后端（解析时按后端类型选择解析器） */
        void setBackend(PackageBackend* backend);

        /** 解析单个包的依赖变更，返回成功与否 */
        bool resolve(const QString& package, QString& error);

        /** 最近一次解析得到的"将直接安装"的包名（不含传入包本身） */
        QStringList toInstall() const { return m_toInstall; }
        /** 最近一次解析得到的"将被移除"的包名 */
        QStringList toRemove() const { return m_toRemove; }

        /**
         * @brief 解析后端干跑输出（静态，便于单测）
         *
         * 按后端类型分流：APT 解析 `Inst`/`Remv` 行（dnfFormat=false，默认），
         * DNF 解析 `Installing:`/`Removing:` 行（dnfFormat=true）。
         * 玲珑(ll-cli)等无结构化事务输出的后端不在此解析，由 resolve() 降级处理。
         * @param outToInstall 传出将安装的包
         * @param outToRemove  传出将移除的包
         * @param dnfFormat    true 时使用 DNF 事务行格式，默认 false（APT 格式）
         * @return 是否解析到任何结构化事务行
         */
        static bool parseSimulateOutput(const QString& text, QStringList& outToInstall,
                                        QStringList& outToRemove, bool dnfFormat = false);

      private:
        PackageBackend* m_backend = nullptr;
        QStringList m_toInstall;
        QStringList m_toRemove;
    };

} // namespace DtkUpdate
