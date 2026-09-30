#pragma once

// 泊位进程运行指标（Requirement 23）：
// - 累计数据（运行时长、启动/崩溃次数、最近退出）由 core/MetricsState 维护，持久化到 <dataDir>/metrics.json；
//   启动/退出/重置事件后 1 秒内写入，运行中每 60 秒写入一次运行时长；Berth 退出时补记并写入
// - 运行中（running）每 2 秒经 Supervisor::jobHandle 采集整棵进程树的 CPU 与内存；
//   运行中（外部）（external）只采所记录 PID 的单个进程，标注"仅主进程"，不计入累计数据
// - 采样失败时跳过该点（当前值无效、保留趋势），下一周期继续；趋势保留最近 150 个采样点
// - metrics.json 不存在或无法解析时累计数据按 0 处理，loadNotice 给出原因，下一次写入生成新文件；
//   schema 过高时只读，不写回
//
// 本类自行读写 metrics.json（DataStore 对 Corrupt 文件拒绝写回，与 23.10 "下一次写入生成新文件"不符）。

#include "core/MetricsState.h"
#include "core/Summary.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class Supervisor;

class ProcessMetrics : public QObject {
    Q_OBJECT
    // 启动时读取 metrics.json 的提示（不存在/无法解析/只读）；空表示正常。泊位详情据此提示"统计已重置"
    Q_PROPERTY(QString loadNotice READ loadNotice CONSTANT)

public:
    ProcessMetrics(Supervisor *supervisor, const QString &dataDir, QObject *parent = nullptr);
    ~ProcessMetrics() override;

    QString loadNotice() const { return m_loadNotice; }

    // 最近一次采样：{valid, cpu, memMb, mainOnly, at}
    // valid 为 false 时（非运行中、尚无采样或本次采样失败）界面显示"—"；mainOnly 为外部泊位的"仅主进程"标注
    Q_INVOKABLE QVariantMap current(const QString &id) const;
    // 趋势（旧→新，最多 150 点，约 5 分钟）：[{t: 毫秒时间戳, cpu, memMb}]
    Q_INVOKABLE QVariantList trend(const QString &id) const;
    // 累计数据：{runtimeSec, starts, crashes, hasLastExit, lastExitCode, lastExitAt("yyyy-MM-dd HH:mm:ss")}
    // runtimeSec 含本次运行尚未写盘的部分
    Q_INVOKABLE QVariantMap totals(const QString &id) const;
    // 重置统计：泊位处于 Active_State（starting/running/stopping）时拒绝并返回 false；成功后 1 秒内写盘
    Q_INVOKABLE bool reset(const QString &id);
    // 泊位被删除：丢弃其累计数据与趋势
    void forget(const QString &id);

    // 顶部汇总用：各泊位最近一次有效采样（采样失败/未运行的泊位不在其中）
    QHash<QString, Summary::Sample> latestSamples() const;

signals:
    // 每次采样周期结束（成功或失败）后按泊位发出，详情页据此刷新当前值与趋势
    void sampled(const QString &id);
    // 采样周期结束后发一次（顶部汇总据此重算）
    void samplesUpdated();
    // 累计数据变化（启动、退出、重置、运行时长写入）
    void totalsChanged(const QString &id);

private:
    struct Point {
        qint64 t = 0;
        double cpu = 0.0;
        double memMb = 0.0;
    };
    struct Live {
        QString status;          // 最近一次 Supervisor 状态
        qint64 pid = 0;          // external 时的 PID
        bool own = false;        // 由 Berth 创建的进程正在运行（计运行时长）
        qint64 startMs = 0;      // own 开始时的单调时钟（毫秒）
        qint64 creditedSec = 0;  // 本次运行已计入 MetricsState 的秒数
        bool hasBase = false;    // CPU 基准是否有效
        qint64 baseCpu100ns = 0;
        qint64 baseWallMs = 0;
        bool currentValid = false;
        Point current;
        QList<Point> trend;
    };

    void load();
    void onStatus(const QString &id, const QString &status, qint64 pid);
    void onStarted(const QString &id);
    void onExited(const QString &id, int exitCode, bool crash);
    void sampleAll();
    void sampleOne(const QString &id, Live &live);
    // 把 own 运行中的时长累加进 MetricsState；返回是否有变化
    bool creditRuntime(const QString &id, Live &live);
    void periodicFlush();
    void scheduleSave();
    void saveNow();
    void updateTimers();

    Supervisor *m_supervisor;
    QString m_path;
    QHash<QString, MetricsState> m_states;
    QHash<QString, Live> m_live;
    QJsonObject m_originalRoot; // 读取时的顶层对象（保留未知字段）
    bool m_readOnly = false;
    QString m_loadNotice;
    int m_cores = 1;
    QElapsedTimer m_clock;
    QTimer m_sampleTimer; // 2 秒采样
    QTimer m_flushTimer;  // 60 秒写入运行时长
    QTimer m_saveTimer;   // 事件后合并写盘（单次，< 1 秒）
};
