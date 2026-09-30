#include "PortPick.h"

#include <algorithm>

namespace PortPick {

std::optional<int> next(int from, const std::function<bool(int)> &isListening,
                        const QSet<int> &configured)
{
    constexpr int kMaxPort = 65535;
    if (from >= kMaxPort)
        return std::nullopt;

    const int start = std::max(from, 0) + 1; // from < kMaxPort，不会溢出
    for (int port = start; port <= kMaxPort; ++port) {
        if (configured.contains(port))
            continue;
        if (isListening && isListening(port))
            continue;
        return port;
    }
    return std::nullopt;
}

QStringList duplicates(const QList<Instance> &instances, const QString &id, int port)
{
    QStringList names;
    for (const Instance &item : instances) {
        if (item.id != id && item.port == port)
            names.append(item.name);
    }
    return names;
}

} // namespace PortPick
