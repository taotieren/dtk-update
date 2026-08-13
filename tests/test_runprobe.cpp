#include <gtest/gtest.h>

#include "core/package/aptbackend.h"
#include "core/package/dnfbackend.h"

#include <QString>

using namespace DtkUpdate;

// runProbe 是 PackageBackend 的 protected 方法；通过子类暴露以便测试。
// 这是修复"needs-restarting 退出码语义反转"的根基：
//   探针以退出码承载语义（1=需重启），runProbe 必须如实把退出码交到 *exitCode。
class ProbeHarness : public AptBackend {
    Q_OBJECT
public:
    using AptBackend::AptBackend;
    bool probe(const QStringList &args, QString &out, int &code) const
    {
        return runProbe(args, out, code);
    }
};

// 退出码捕获契约：runProbe 必须返回进程真实退出码，而非仅在 exit 0 成功。
// 这是 `required = (exitCode == 1)` 这类语义判定的前置保证。
// 用受控命令 true(exit 0) / false(exit 1) 直接验证，避免在无 needs-restarting
// 的环境偷偷走内核比对回退而漏测修复点。
TEST(RunProbeTest, CapturesExitCodeZeroAndOne)
{
    ProbeHarness h;
    QString out;
    int code = -2;

    ASSERT_TRUE(h.probe(QStringList{QStringLiteral("true")}, out, code));
    EXPECT_EQ(code, 0) << "runProbe must surface exit code 0 (true)";

    ASSERT_TRUE(h.probe(QStringList{QStringLiteral("false")}, out, code));
    EXPECT_EQ(code, 1) << "runProbe must surface exit code 1 (false); "
                          "this is the root contract for needs-restarting "
                          "where 1 means reboot required";
}

// 命令不存在时 runProbe 应返回 false 且 exitCode 保持 -1，不误判为成功。
TEST(RunProbeTest, MissingCommandReportsFalse)
{
    ProbeHarness h;
    QString out;
    int code = -2;
    ASSERT_FALSE(h.probe(QStringList{QStringLiteral("this-command-does-not-exist-xyz")},
                         out, code));
    EXPECT_EQ(code, -1) << "unstartable command leaves exitCode at -1";
}

// 退出码契约同样直接适用于 dnf 后端（runProbe 现已统一为 protected）。
class DnfProbeHarness : public DnfBackend {
    Q_OBJECT
public:
    using DnfBackend::DnfBackend;
    bool probe(const QStringList &args, QString &out, int &code) const
    {
        return runProbe(args, out, code);
    }
};

TEST(RunProbeTest, DnfCapturesExitCodeContract)
{
    DnfProbeHarness h;
    QString out;
    int code = -2;
    ASSERT_TRUE(h.probe(QStringList{QStringLiteral("false")}, out, code));
    EXPECT_EQ(code, 1) << "DnfBackend::runProbe must surface exit 1";
}

// 真实契约：applyStableLocale 必须让子进程继承 LC_ALL=C，否则 apt/dnf 的
// 解析锚点（"[upgradable from: ...]" / "Available" 表头）在非英文 locale 下被
// 翻译，导致 fetchUpgradable 返回空列表、用户看不到任何更新。
// 用 bash -c 'echo $LC_ALL' 验证注入生效（不依赖真实 apt/dnf 命令）。
TEST(RunProbeTest, InheritsStableLocaleC)
{
    ProbeHarness h;
    QString out;
    int code = -2;
    ASSERT_TRUE(h.probe(QStringList{QStringLiteral("bash"),
                                     QStringLiteral("-c"),
                                     QStringLiteral("echo $LC_ALL")},
                        out, code));
    EXPECT_EQ(code, 0);
    EXPECT_EQ(out.trimmed(), QStringLiteral("C"))
        << "runProbe must force LC_ALL=C into the child so output anchors stay untranslated";
}

#include "test_runprobe.moc"
