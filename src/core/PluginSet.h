#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// profile 插件启用配置的纯逻辑（package.json 的 dsh.profile.bundles）。只依赖 Qt6::Core。
namespace PluginSet {

// Core_Package：包名以 "@deepseek-ai/" 开头
bool isCorePackage(const QString &name);

// dsh.profile.bundles 中的字符串项（保持顺序，忽略非字符串项）
QStringList bundles(const QJsonObject &pkg);

bool isEnabled(const QJsonObject &pkg, const QString &name);

// 只改 dsh.profile.bundles：on=true 时不在其中则追加到末尾；on=false 时移除全部同名项。
// 状态已符合时原样返回 pkg；dependencies 与其他字段、其他 bundles 项（含非字符串项）不变。
QJsonObject setEnabled(QJsonObject pkg, const QString &name, bool on);

struct Row {
    QString name;
    QString version;        // dependencies 中声明的版本，可为空
    QString displayVersion; // version 为空时为 "—"
    bool enabled = false;   // 是否在 bundles 中
    bool core = false;      // 是否 Core_Package
    bool quarantined = false;

    bool operator==(const Row &) const = default;
};

// 每个插件恰好一行：先按 dependencies 的键顺序，再追加只在 bundles 中出现的名字。
// showCore=false 时过滤 Core_Package；quarantined 中的名字标记 quarantined=true。
QList<Row> list(const QJsonObject &pkg, bool showCore, const QStringList &quarantined = {});

struct Failure {
    QString name;
    QString reason;

    bool operator==(const Failure &) const = default;
};

// 批量操作汇总：按选中顺序逐项 record
struct BatchSummary {
    int total = 0;
    int succeeded = 0;
    QStringList succeededNames;
    QList<Failure> failures;

    void record(const QString &name, bool ok, const QString &reason = {});
    int failed() const { return int(failures.size()); }

    // 当且仅当至少 1 项成功且受影响泊位非空时需要（整次变更只触发 1 次）Restart_Hint
    bool needsRestartHint(const QStringList &affected) const;
};

// 按 names 顺序批量设置启用状态；outcomes[i] 表示第 i 项写入是否成功（缺省视为失败）。
// 失败项保持操作前状态；reasons[i] 为失败原因（可缺省）。summary 可为空。
QJsonObject applyBatch(QJsonObject pkg, const QStringList &names, bool on,
                       const QList<bool> &outcomes, const QStringList &reasons,
                       BatchSummary *summary);

} // namespace PluginSet
