#pragma once

#include "Instance.h"

#include <QList>
#include <QString>
#include <QStringList>

// 插件变动后的受影响泊位判定与 Restart_Hint 列表合并（纯逻辑）。只依赖 Qt6::Core。
namespace AffectedSet {

// 规范化 DSH_HOME：统一为 '/' 分隔、cleanPath、去掉末尾 '/'（根路径除外）。不访问文件系统。
QString normalizeHome(const QString &home);

// 规范化后不区分大小写比较
bool sameHome(const QString &a, const QString &b);

// 返回 home（规范化后不区分大小写）与 profile 都相同、且状态为 starting/running 的泊位 id，保持列表顺序。
// 泊位 dshHome 为空时以 defaultHome 代替（调用方传入已解析的默认 DSH_HOME）。
QStringList affected(const QList<Instance> &instances, const QString &home, const QString &profile,
                     const QString &defaultHome = {});

// 合并：结果无重复，a 中项按首次出现顺序保留，b 中新项追加在后
QStringList merge(const QStringList &a, const QStringList &b);

} // namespace AffectedSet
