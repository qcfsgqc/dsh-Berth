#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// 启动前插件自检的隔离决策（纯逻辑）。只依赖 Qt6::Core。
// 输入为修复前、修复后（可选）的检查结果与修复结局，输出被隔离集合与剩余启用集合。
namespace PreflightDecision {

// 隔离原因；字符串形式与 quarantine.json 的 reason 字段一致
enum class Reason {
    Missing,          // "missing"：包不在 node_modules
    DepUnresolved,    // "dep-unresolved"：某个 dependencies 项不可解析或版本不满足
    InstallFailed,    // "install-failed"：修复命令非零退出
    InstallTimeout,   // "install-timeout"：修复命令超时被终止
    NoPackageManager, // "no-package-manager"：包管理器不可用，跳过修复
};

// 单个插件的检查结果。ok=false 时 failure 只取 Missing 或 DepUnresolved，
// detail 为补充说明（如不可解析的依赖名）。
struct CheckResult {
    QString name;
    bool ok = true;
    Reason failure = Reason::Missing;
    QString detail;
};

// 修复结局
enum class RepairOutcome {
    NotAttempted,     // 无需修复（修复前全部通过）
    Succeeded,        // 修复命令 0 退出
    Failed,           // 修复命令非零退出
    TimedOut,         // 修复超时
    NoPackageManager, // 包管理器不可用
};

struct Quarantined {
    QString name;
    Reason reason = Reason::Missing;
    QString detail;
};

struct Decision {
    QList<Quarantined> quarantined; // 最终仍未通过的插件，按 before 顺序
    QStringList remaining;          // 最终通过的插件，按 before 顺序
};

// before 定义原启用集合（按名字去重，保留首次出现）。
// after 存在时以 after 中同名结果为最终结果（after 缺该插件则沿用 before）；否则以 before 为最终结果。
// 最终未通过的插件原因：修复结局为 Failed/TimedOut/NoPackageManager 时取对应原因，
// 否则取检查结果的 failure；detail 始终保留检查结果的 detail。
// 保证：quarantined 与 remaining 不相交，且名字并集等于原启用集合。
Decision decide(const QList<CheckResult> &before, const std::optional<QList<CheckResult>> &after,
                RepairOutcome repair);

// 原因 ↔ 字符串（"missing" / "dep-unresolved" / "install-failed" / "install-timeout" / "no-package-manager"）
QString reasonString(Reason r);
std::optional<Reason> reasonFromString(const QString &s);

} // namespace PreflightDecision
