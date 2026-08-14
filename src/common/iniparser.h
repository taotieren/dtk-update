#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace DtkUpdate
{

    /**
     * @brief 轻量 INI / conf 解析器（无第三方依赖）
     *
     * 支持：
     *   - 扁平键值：Key = Value
     *   - 段：[Section]，其下的键可通过 "Section.Key" 查询
     *   - 注释（# 或 ; 开头）与空行
     *   - 行内尾注（# / ; 之前的内容为注释），但不在引号内剥离
     *   - 引号包裹的值会被去掉首尾引号
     *
     * 不支持：多行值、类型推断（统一以字符串存储，调用方自行转换）。
     */
    class IniParser
    {
      public:
        using SectionMap = QMap<QString, QString>; // 段名(小写) -> 键值表

        IniParser() = default;

        // 解析整段文本。error 在失败时写入可读信息并返回 false。
        bool parse(const QString& text, QString* error = nullptr);

        // 解析文件（不存在/不可读视为失败）。
        bool parseFile(const QString& path, QString* error = nullptr);

        // 查询：优先 sectionKey（"Section.Key"），回退到全局键。
        QString value(const QString& key, const QString& def = QString()) const;

        // 仅查询全局段（无段头）的键。
        QString globalValue(const QString& key, const QString& def = QString()) const;

        // 查询某段内的键（段名不区分大小写）。
        QString sectionValue(const QString& section, const QString& key,
                             const QString& def = QString()) const;

        const SectionMap& globals() const { return m_globals; }
        const QMap<QString, SectionMap>& sections() const { return m_sections; }

      private:
        static QString sectionKey(const QString& section, const QString& key);
        static QString stripComment(const QString& line);
        static QString stripQuotes(const QString& v);

        // 段名(小写) -> 键值；全局段用空字符串键
        QMap<QString, SectionMap> m_sections;
        SectionMap m_globals;
    };

} // namespace DtkUpdate
