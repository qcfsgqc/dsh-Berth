#pragma once

// 会话统计与用量统计（design.md「StatsService」）。
// - usage(range, targets)：Token 用量（需求 22，待定 TODO #2 方案 A）。每个泊位经 ProcessRunner 用 node 运行
//   helpers/usage-scan.mjs（从 qrc 释放），只读解析 <home>/sessions；结果经 usageFinished 发出。
// - sessionStats(id)：会话统计（需求 21，任务 25.4 追加；在下方「会话统计」段落扩展）。

#include "core/SettingsCodec.h" // ModelPrice
#include "core/SessionAgg.h"
#include "core/UsageAgg.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <atomic>
#include <memory>

class ProcessRunner;
class ProcessTask;

class StatsService : public QObject {
    Q_OBJECT

public:
    // 统计对象：一个泊位。home 为已解析的 DSH_HOME（调用方用 AppController::resolveHome 解析）
    struct Target {
        QString berthId;
        QString berthName;
        QString home;
        QString workspace; // 为空时统计该 DSH_HOME 下全部项目目录
    };

    // 单个泊位的读取结果
    struct BerthUsage {
        QString berthId;
        QString berthName;
        bool ok = false;
        QString error;      // 失败原因（ok=false 时）
        bool hasUsage = false;
        int files = 0;
        int skippedFiles = 0;
        int skippedLines = 0;
    };

    struct UsageResult {
        int requestId = 0;
        UsageAgg::Range range = UsageAgg::Range::Today;
        QDateTime now;
        QList<UsageAgg::Record> records; // 仅来自读取成功的泊位
        UsageAgg::Totals totals;         // records 按 range 汇总（失败泊位已排除）
        QList<BerthUsage> berths;        // 与请求的 targets 同序
        int failedCount = 0;             // 读取失败、已从合计排除的泊位数
        bool anyUsage = false;           // 至少一个成功泊位有 usage 记录（false → 按 22.5 隐藏用量区域）
        bool allFailed = false;          // 有泊位且全部失败（→ 隐藏区域并显示 error）
        QString error;                   // 整体失败原因（找不到 node、脚本释放失败、全部失败时的汇总）

        // 给 QML 用的结构（26.5）：见 StatsService.cpp 中 toVariantMap 的注释
        QVariantMap toVariantMap(const QMap<QString, ModelPrice> &prices) const;
    };

    explicit StatsService(ProcessRunner *runner, QObject *parent = nullptr);
    ~StatsService() override;

    // Settings.nodeExecutable；为空时在 PATH 上找 node
    void setNodeExecutable(const QString &path);
    // 辅助脚本释放目录，通常为 <dataDir>/helpers
    void setHelpersDir(const QString &dir);

    // ---- 用量统计 ----------------------------------------------------------
    // 发起一次读取并返回 requestId。新请求会结束仍在运行的上一次读取（其结果不再发出）。
    // 结果总是经 usageFinished 异步发出（包括 targets 为空、找不到 node 等立即失败的情况）。
    int usage(UsageAgg::Range range, const QList<Target> &targets);
    bool usageBusy() const { return m_usage.active; }
    void cancelUsage();

    // "today" | "7d" | "30d" | "all"（其他值按 today）
    static UsageAgg::Range rangeFromString(const QString &s);
    static QString rangeToString(UsageAgg::Range range);

    // 单次脚本运行超时（毫秒）
    static constexpr int kUsageTimeoutMs = 10000;

    // ---- 会话统计（需求 21）-------------------------------------------------
    // 单次读取的结果。只读枚举 <home>/sessions/<项目目录>/<会话目录>/session*.jsonl*，不打开文件、不加锁。
    struct SessionScan {
        enum class Kind { Ok, NotFound, Unreadable, Unparsable, Timeout };
        Kind kind = Kind::Ok;
        QString detail;          // 失败时的路径或说明
        SessionAgg::Result agg;  // kind == Ok 时有效
    };

    // 在工作线程读取一次（kSessionTimeoutMs 后放弃，记为超时）；同一泊位读取中时忽略新请求。
    // home 为已解析的 DSH_HOME；workspace 为空时统计全部项目目录。
    // 读取期间保留上一次结果；完成后更新缓存并发 sessionStatsChanged(id)。
    void sessionStats(const QString &id, const QString &home, const QString &workspace);
    // 给 QML：{state: "idle"|"ok"|"error", loading, total, active,
    //          lastActivity("YYYY-MM-DD HH:mm"，总数为 0 或无时间时为 "—"), error(原因), updatedAt}
    Q_INVOKABLE QVariantMap sessionStatsFor(const QString &id) const;
    // 同步扫描（工作线程内调用；cancel 置位时尽快返回 Timeout）。可单独测试
    static SessionScan scanSessions(const QString &home, const QString &workspace,
                                    const std::atomic_bool *cancel = nullptr);
    static QString sessionReasonText(const SessionScan &scan);

    static constexpr int kSessionTimeoutMs = 5000;

signals:
    void usageFinished(const StatsService::UsageResult &result);
    void sessionStatsChanged(const QString &id);

private:
    struct UsageRun {
        bool active = false;
        int remaining = 0;
        UsageResult result;
        QList<QPointer<ProcessTask>> tasks;
    };

    // 把 qrc 中的脚本释放到 helpersDir（内容相同则不写）；成功返回脚本路径
    QString ensureHelper(QString *error) const;
    QString resolveNode() const;
    void finishTarget(int requestId, int index, bool ok, int code, const QString &tail, bool timedOut,
                      bool failedToStart, const QString &processError);
    void completeUsage();
    void failAllUsage(const QString &reason);
    void finishSession(const QString &id, int seq, const SessionScan &scan);

    struct SessionEntry {
        int seq = 0;
        bool loading = false;
        bool hasResult = false;
        SessionScan scan;
        QDateTime updatedAt;
        std::shared_ptr<std::atomic_bool> cancel;
    };
    // 工作线程回投结果前检查 alive；析构时置 false，之后不再向本对象投递
    struct SessionLife;
    std::shared_ptr<SessionLife> m_life;
    QHash<QString, SessionEntry> m_sessions;
    int m_sessionSeq = 0;

    ProcessRunner *m_runner = nullptr;
    QString m_nodeExecutable;
    QString m_helpersDir;
    int m_usageSeq = 0;
    UsageRun m_usage;
};
