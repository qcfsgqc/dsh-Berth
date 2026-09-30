#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// 批量启停的错峰调度计划（纯逻辑）。只依赖 Qt6::Core。
namespace BatchPlan {

// Active_State：启动中 / 运行中 / 停止中（与 Instance::status 取值一致）
bool isActiveState(const QString &status);

struct Step {
    QString id;
    qint64 atMs = 0; // 相对批量开始时刻的发起时间（毫秒）
};

struct Plan {
    QList<Step> steps;   // 按目标列表顺序，第 k 个在 k·t 秒发起
    QStringList skipped; // 处于 Active_State 而被跳过的泊位（计入已完成）
};

// targets：有序泊位 id 列表；statuses：id → 状态字符串（缺省视为非活动）；
// intervalSec：错峰间隔，限制在 [0, 60]。
// 跳过的泊位不占序号，其余 k 从 0 连续编号。
Plan plan(const QStringList &targets, const QHash<QString, QString> &statuses, int intervalSec);

// 在 elapsedMs 时刻取消：返回尚未发起的泊位（atMs > elapsedMs；等于视为已发起），保持顺序
QStringList cancel(const Plan &plan, qint64 elapsedMs);

} // namespace BatchPlan
