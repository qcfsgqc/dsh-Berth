#pragma once

// 单个泊位的崩溃自动重启状态机（值类型，无定时器、无副作用）。
// 定时与重启由 AutoRestarter 根据返回的 Action 执行。只依赖 Qt6::Core。
//
// 不变量：始终 0 <= n <= N。
struct RestartState {
    struct Action {
        enum Kind { None, Schedule, GiveUp };
        Kind kind = None;
        int ms = 0; // 仅 Schedule 有效：退避等待毫秒数

        static Action none() { return {}; }
        static Action schedule(int ms) { return {Schedule, ms}; }
        static Action giveUp() { return {GiveUp, 0}; }
        bool operator==(const Action &o) const { return kind == o.kind && ms == o.ms; }
        bool operator!=(const Action &o) const { return !(*this == o); }
    };

    static constexpr int kResetAfterSec = 120; // 连续运行满此秒数后 n 清零

    int n = 0;          // 当前连续重启次数
    int N = 5;          // 上限，1–20
    bool enabled = false; // 泊位的"崩溃自动重启"开关
    bool external = false; // 是否处于"运行中（外部）"
    int baseSec = 2;    // 基础间隔，1–60
    int maxSec = 60;    // 最大间隔，baseSec–600

    RestartState() = default;
    RestartState(int limit, int baseSec, int maxSec, bool enabled = false);

    // 更新配置（夹紧到合法范围），并保证 n <= N
    void configure(int limit, int baseSec, int maxSec);

    // 发生 Crash（含自动重启未能成功启动）：
    // - 开关关闭或外部运行：None，n 不变
    // - n < N：n += 1，Schedule(backoffMs(n))
    // - n == N：GiveUp，n 不变
    Action onCrash();

    // 自最近一次进入"运行中"起已连续运行 sec 秒；满 120 秒时 n 清零
    void onRunningFor(int sec);

    void onManualStop();  // n 清零
    void onManualStart(); // n 清零
    void onToggle(bool on); // 设置开关；关闭时 n 清零
    void onExternal(bool ext); // 进入/离开"运行中（外部）"
};
