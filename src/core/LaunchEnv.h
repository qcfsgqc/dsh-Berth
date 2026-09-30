#pragma once

// 泊位启动时的环境变量与额外参数组装。
// 约束：只依赖 Qt6::Core。

#include "Instance.h"

#include <QHash>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace LaunchEnv {

// 组装子进程环境。键一律不区分大小写；写入某键前先删除所有不区分大小写同名的旧键。
// 优先级由低到高：
//   sys（系统环境）
//   < proxyEnv（仅 inheritProxy 为真时）
//   < envTable（泊位环境变量表，按行顺序写入；空字符串值照常写入）
//   < DSH_HOME = dshHome（Berth 计算值，始终覆盖）
QProcessEnvironment build(const QProcessEnvironment &sys,
                          const QList<InstanceEnvVar> &envTable,
                          const QString &dshHome,
                          const QHash<QString, QString> &proxyEnv,
                          bool inheritProxy);

// 过滤额外参数：移除 "--port"、"--profile" 及以 "--port="、"--profile=" 开头的项；
// 单独成项的 "--port"/"--profile" 连同紧随的一项（若有）一起移除。
// 其余项保持原顺序、原样返回。conflicts 非空时按出现顺序追加所有被移除的项。
QStringList filterArgs(const QStringList &extra, QStringList *conflicts = nullptr);

} // namespace LaunchEnv
