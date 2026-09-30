#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

// 会话统计聚合（纯逻辑）。只依赖 Qt6::Core。
// 输入是某项目目录下每个会话目录及其日志文件的最大 mtime，由调用方枚举。
namespace SessionAgg {

inline constexpr qint64 kActiveWindowMs = 30LL * 60 * 1000; // 30 分钟

struct SessionDir {
    QString name;      // 会话目录名（encodedId）
    QDateTime mtime;   // 目录下日志文件的最大修改时间；没有日志时为无效值
};

struct Result {
    int total = 0;           // 会话总数 = 目录数
    int active = 0;          // mtime 距 now 不超过 30 分钟（含未来时间）的会话数
    QDateTime lastActivity;  // 所有有效 mtime 的最大值；没有时为无效值
};

Result aggregate(const QList<SessionDir> &dirs, const QDateTime &now);

} // namespace SessionAgg
