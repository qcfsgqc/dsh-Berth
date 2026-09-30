#pragma once

// 启动前插件自检（Preflight_Checker）：
// 1. 工作线程读取 <profileDir>/package.json，对 dsh.profile.bundles 中每个启用插件（跳过 Core_Package）检查：
//    包存在于 <profileDir>/node_modules；从包的真实路径（canonicalFilePath，兼容 pnpm 符号链接）
//    按 Node 解析规则逐级向上查找每个 dependencies 项，并用 SemVer::satisfies 校验版本
// 2. 有未通过项时执行一次修复 `dsh plugin --profile <p> install`（超时 300 秒），结束后重新检查
// 3. PreflightDecision::decide 得出隔离集合；隔离的插件从 bundles 移除并写入 quarantine.json
// 每一步写一条泊位日志。同一 profile 的请求串行排队（同一时刻最多 1 个修复进程）。
// package.json 读取失败时 aborted=true，不做任何修复与隔离。

#include "Quarantine.h"
#include "core/PreflightDecision.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class ProcessRunner;
class QThread;

class Preflight : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString id;         // 泊位 id（只用于标识）
        QString home;       // 规范化后的 DSH_HOME
        QString profile;
        QString profileDir; // <home>/profiles/<profile>
        QString exe;        // dsh 可执行文件（修复用）
        QString logPath;    // 泊位日志
        QStringList secrets;
    };

    struct Result {
        bool aborted = false; // package.json 无法读取：中止启动
        QString error;        // aborted 时的原因
        PreflightDecision::Decision decision;
        QList<QuarantineEntry> quarantined; // 本次新隔离的插件（已写入 quarantine.json）
    };

    using Done = std::function<void(const Result &)>;

    // runner / quarantine 由调用方持有，生命周期覆盖 Preflight
    Preflight(ProcessRunner *runner, Quarantine *quarantine, QObject *parent = nullptr);
    ~Preflight() override;

    // 排入该 profile 的串行队列；done 在主线程回调且只调用一次（Preflight 析构时不再回调）
    void run(const Request &request, Done done);

    // 单个插件的检查（纯文件系统读取，可在任意线程调用）
    static PreflightDecision::CheckResult checkPlugin(const QString &profileDir, const QString &name);

    // quarantine.json 的 reason 字符串 → 中文说明（未知取值原样返回）；给界面显示隔离原因用
    static QString reasonText(const QString &reason);

private:
    struct Scan {
        bool ok = false;
        QString error;
        QList<PreflightDecision::CheckResult> results;
    };
    struct Job {
        Request request;
        Done done;
        QList<PreflightDecision::CheckResult> before;
        bool started = false;
    };

    static Scan scan(const QString &profileDir);
    static QString queueKey(const QString &profileDir);

    void startNext(const QString &key);
    // 在工作线程执行 scan，完成后在主线程回调 then
    void scanAsync(const QString &profileDir, std::function<void(const Scan &)> then);
    void onFirstScan(const QString &key, const Scan &scan);
    void repair(const QString &key);
    void onRepaired(const QString &key, PreflightDecision::RepairOutcome outcome);
    void finish(const QString &key, PreflightDecision::RepairOutcome outcome,
                const std::optional<QList<PreflightDecision::CheckResult>> &after);
    void complete(const QString &key, const Result &result);
    void log(const Request &request, const QString &message) const;
    // 从 bundles 移除 names 并写回 package.json（QSaveFile）
    static bool removeBundles(const QString &profileDir, const QStringList &names, QString *error);

    ProcessRunner *m_runner;
    Quarantine *m_quarantine;
    // 队列键（profileDir 规范化小写）→ 排队中的请求，队首为正在执行的请求
    QHash<QString, QList<Job>> m_queues;
    QList<QThread *> m_threads;
};
