#pragma once

#include <QHash>
#include <QMap>
#include <QString>

// 顶部汇总面板的五项指标计算（纯逻辑）。只依赖 Qt6::Core。
namespace Summary {

// 某泊位最近一次采样
struct Sample {
    double cpuPercent = 0.0; // CPU 百分比（全部逻辑核心归一化）
    double memMb = 0.0;      // 内存（MB）
};

struct Result {
    int total = 0;          // 泊位总数
    int running = 0;        // 运行中：running / external
    int failed = 0;         // 失败：failed / crashStopped
    double memMb = 0.0;     // 运行中泊位最近采样内存之和（MB）
    double cpuPercent = 0.0; // 运行中泊位最近采样 CPU 之和，保留 1 位小数

    bool operator==(const Result &o) const
    {
        return total == o.total && running == o.running && failed == o.failed && memMb == o.memMb
            && cpuPercent == o.cpuPercent;
    }
    bool operator!=(const Result &o) const { return !(*this == o); }
};

bool isRunning(const QString &status); // running / external
bool isFailed(const QString &status);  // failed / crashStopped

// statuses：泊位 id -> 状态字符串（取值同 Instance::status）；samples：泊位 id -> 最近一次采样。
// 只累加运行中泊位的采样，无采样按 0 计；非有限或负的采样值按 0 计。
Result compute(const QMap<QString, QString> &statuses, const QHash<QString, Sample> &samples);

// 小于 1024 MB 输出 MB 整数（向下取整，如 "512 MB"），否则输出保留 1 位小数的 GB（如 "1.5 GB"）。
// 非有限或负值按 0 处理。
QString formatMem(double mb);

// CPU 百分比保留 1 位小数（如 "0.0%"）
QString formatCpu(double percent);

} // namespace Summary
