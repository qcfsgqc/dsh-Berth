#pragma once

// 端口挑选与重复校验。只依赖 Qt6::Core。

#include "Instance.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace PortPick {

// 返回 (from, 65535] 中最小的、isListening 返回 false 且不在 configured 中的端口；
// 不存在时返回空。from < 0 时从 1 开始（端口 0 不作为候选）。
std::optional<int> next(int from, const std::function<bool(int)> &isListening,
                        const QSet<int> &configured);

// 返回除 id 外所有 port 相同的泊位名，保持 instances 中的顺序。
QStringList duplicates(const QList<Instance> &instances, const QString &id, int port);

} // namespace PortPick
