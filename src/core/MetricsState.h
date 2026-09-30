#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>

// 单个泊位的进程累计指标（值类型，无定时器、无副作用）。
// 采样、定时写盘由 ProcessMetrics 负责。只依赖 Qt6::Core。
//
// 不变量：始终 0 <= crashes <= starts，runtimeSec >= 0。
struct MetricsState {
    qint64 runtimeSec = 0;   // 累计运行时长（秒）
    int starts = 0;          // 启动次数（Supervisor 创建 dsh 进程计 1 次）
    int crashes = 0;         // 崩溃次数
    bool hasLastExit = false; // 是否有最近退出记录
    int lastExitCode = 0;    // 仅 hasLastExit 时有效
    QDateTime lastExitAt;    // 本地时间，精确到秒；仅 hasLastExit 时有效

    // Supervisor 创建了 dsh 进程（"复用现有服务"不要调用）
    void onStart();
    // 进程退出：记录退出码与时间（截断到秒）；crash 为真时崩溃次数 +1（不超过启动次数）
    void onExit(int code, bool crash, const QDateTime &at);
    // 累加运行时长；sec <= 0 时忽略
    void addRuntime(qint64 sec);
    // 三项计数清零并清除最近退出记录
    void reset();

    // metrics.json 中单个泊位的对象：{runtimeSec, starts, crashes, lastExitCode, lastExitAt}
    // 无最近退出记录时省略 lastExitCode / lastExitAt。
    QJsonObject toJson() const;
    // 缺失或类型错误的字段按 0/无处理；结果会修正到满足不变量
    static MetricsState fromJson(const QJsonObject &o);

    bool operator==(const MetricsState &o) const;
    bool operator!=(const MetricsState &o) const { return !(*this == o); }
};

namespace MetricsFile {
constexpr int kSchemaVersion = 1;

// {schemaVersion:1, instances:{<id>:{...}}}
QJsonObject encode(const QHash<QString, MetricsState> &all);
// 解析整份文件；root 不含 instances 对象时 ok=false 并返回空表
QHash<QString, MetricsState> decode(const QJsonObject &root, bool *ok = nullptr);
} // namespace MetricsFile

// CPU 占用百分比：d100ns 为 CPU 时间差（100ns 单位，Job Object/FILETIME 原生单位），
// dWallMs 为墙钟差（毫秒），cores 为逻辑核数。
// 结果 = (d100ns / 10000) / (dWallMs * cores) * 100，保留 1 位小数；
// d100ns < 0、dWallMs <= 0 或 cores < 1 时返回 0。
double cpuPercent(qint64 d100ns, qint64 dWallMs, int cores);
