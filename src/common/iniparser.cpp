#include "iniparser.h"
#include "logger.h"

#include <QFile>
#include <QTextStream>

namespace DtkUpdate {

QString IniParser::sectionKey(const QString &section, const QString &key)
{
    return section.toLower() + QLatin1Char('.') + key.toLower();
}

QString IniParser::stripComment(const QString &line)
{
    // 不在引号内剥离 # / ; 注释
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('"'))
            inQuote = !inQuote;
        else if (!inQuote && (c == QLatin1Char('#') || c == QLatin1Char(';')))
            return line.left(i);
    }
    return line;
}

QString IniParser::stripQuotes(const QString &v)
{
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')))
        return v.mid(1, v.size() - 2);
    return v;
}

bool IniParser::parse(const QString &text, QString *error)
{
    m_sections.clear();
    m_globals.clear();

    QString curSection;  // 空 = 全局段
    QTextStream stream(const_cast<QString *>(&text));
    QString line;
    int lineNo = 0;
    while (stream.readLineInto(&line)) {
        ++lineNo;
        // 去尾随 \r（跨平台文件）
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString raw = line.trimmed();
        if (raw.isEmpty())
            continue;
        if (raw.startsWith(QLatin1Char('#')) || raw.startsWith(QLatin1Char(';')))
            continue;

        // 段头：[Section]
        if (raw.startsWith(QLatin1Char('[')) && raw.endsWith(QLatin1Char(']'))) {
            curSection = raw.mid(1, raw.size() - 2).trimmed();
            if (curSection.isEmpty()) {
                if (error) *error = QStringLiteral("empty section header at line %1").arg(lineNo);
                return false;
            }
            if (!m_sections.contains(curSection.toLower()))
                m_sections.insert(curSection.toLower(), SectionMap());
            continue;
        }

        const QString content = stripComment(raw);
        const int eq = content.indexOf(QLatin1Char('='));
        if (eq < 0) {
            if (error) *error = QStringLiteral("missing '=' in line %1").arg(lineNo);
            return false;
        }
        const QString key = content.left(eq).trimmed();
        const QString val = stripQuotes(content.mid(eq + 1).trimmed());
        if (key.isEmpty()) {
            if (error) *error = QStringLiteral("empty key in line %1").arg(lineNo);
            return false;
        }
        if (curSection.isEmpty())
            m_globals.insert(key, val);  // 保留原始大小写，查询时大小写不敏感
        else
            m_sections[curSection.toLower()].insert(key, val);
    }
    return true;
}

bool IniParser::parseFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.exists()) {
        if (error) *error = QStringLiteral("file not found: %1").arg(path);
        return false;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(path, f.errorString());
        return false;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const QString text = in.readAll();
    return parse(text, error);
}

QString IniParser::globalValue(const QString &key, const QString &def) const
{
    const QString k = key.toLower();
    for (auto it = m_globals.constBegin(); it != m_globals.constEnd(); ++it)
        if (it.key().toLower() == k)
            return it.value();
    return def;
}

QString IniParser::sectionValue(const QString &section, const QString &key,
                                const QString &def) const
{
    const auto sit = m_sections.constFind(section.toLower());
    if (sit == m_sections.constEnd())
        return def;
    const QString k = key.toLower();
    const auto &sec = sit.value();
    for (auto it = sec.constBegin(); it != sec.constEnd(); ++it)
        if (it.key().toLower() == k)
            return it.value();
    return def;
}

QString IniParser::value(const QString &key, const QString &def) const
{
    // 支持 "Section.Key" 点号查询。
    // 注意：用 isEmpty() 而非 isNull() 判断"命中"——空字符串值（Key =）也应视为
    // 命中并覆盖全局；否则空值会被误判为未命中而错误回退到全局默认值。
    const int dot = key.indexOf(QLatin1Char('.'));
    if (dot > 0) {
        const QString sec = key.left(dot);
        const QString k = key.mid(dot + 1);
        const QString v = sectionValue(sec, k, QString());
        if (!v.isEmpty())
            return v;
    }
    const QString g = globalValue(key, QString());
    return g.isEmpty() ? def : g;
}

}  // namespace DtkUpdate
