#pragma once

// Instance 与 instances.json 条目互转。
// - 缺少的字段取 Instance 的默认值；字段类型不对时取默认值，并把 "文件名.字段名" 追加到 typeErrors
// - 无法识别的字段放进 Instance::extra（env 行与 icon 的未知子字段放进各自的 extra），toJson 时原样写回
// - 运行时状态（status/pid/lastError）不读不写
// 约束：只依赖 Qt6::Core。

#include "Instance.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace InstanceCodec {

// prefix：类型错误的字段前缀，默认 "instances.json"；列表读取时为 "instances.json.instances[<i>]"
Instance fromJson(const QJsonObject &obj, QStringList *typeErrors,
                  const QString &prefix = QStringLiteral("instances.json"));

QJsonObject toJson(const Instance &item);

// instances.json 根对象中 "instances" 数组的读写。
// 不是对象的数组元素记为类型错误并跳过；其余条目保持原顺序（不过滤空 id，由调用方决定）。
QList<Instance> listFromJson(const QJsonArray &array, QStringList *typeErrors,
                             const QString &prefix = QStringLiteral("instances.json.instances"));

QJsonArray listToJson(const QList<Instance> &items);

} // namespace InstanceCodec
