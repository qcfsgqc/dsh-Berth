#include "MetricsState.h"

#include <QJsonValue>

#include <cmath>
#include <limits>

namespace {
const QString kFmt = QStringLiteral("yyyy-MM-ddTHH:mm:ss");

QDateTime truncToSec(const QDateTime &t)
{
    if (!t.isValid())
        return t;
    const QDateTime local = t.toLocalTime();
    return QDateTime::fromSecsSinceEpoch(local.toSecsSinceEpoch());
}

// 读取非负整数字段；缺失、非数字、负数、非整数返回 0
qint64 readCount(const QJsonObject &o, const QString &key, qint64 hi)
{
    const QJsonValue v = o.value(key);
    if (!v.isDouble())
        return 0;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d < 0 || d != std::floor(d))
        return 0;
    return d > double(hi) ? hi : qint64(d);
}
} // namespace

void MetricsState::onStart()
{
    if (starts < std::numeric_limits<int>::max())
        ++starts;
}

void MetricsState::onExit(int code, bool crash, const QDateTime &at)
{
    hasLastExit = true;
    lastExitCode = code;
    lastExitAt = truncToSec(at);
    if (crash && crashes < starts)
        ++crashes;
}

void MetricsState::addRuntime(qint64 sec)
{
    if (sec <= 0)
        return;
    if (runtimeSec > std::numeric_limits<qint64>::max() - sec)
        runtimeSec = std::numeric_limits<qint64>::max();
    else
        runtimeSec += sec;
}

void MetricsState::reset()
{
    *this = MetricsState();
}

QJsonObject MetricsState::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("runtimeSec"), double(runtimeSec));
    o.insert(QStringLiteral("starts"), starts);
    o.insert(QStringLiteral("crashes"), crashes);
    if (hasLastExit) {
        o.insert(QStringLiteral("lastExitCode"), lastExitCode);
        o.insert(QStringLiteral("lastExitAt"),
                 lastExitAt.isValid() ? lastExitAt.toLocalTime().toString(kFmt) : QString());
    }
    return o;
}

MetricsState MetricsState::fromJson(const QJsonObject &o)
{
    MetricsState m;
    // JSON 数字为 double，运行时长上限取 2^53 以保证往返精确
    m.runtimeSec = readCount(o, QStringLiteral("runtimeSec"), qint64(1) << 53);
    m.starts = int(readCount(o, QStringLiteral("starts"), std::numeric_limits<int>::max()));
    m.crashes = int(readCount(o, QStringLiteral("crashes"), std::numeric_limits<int>::max()));
    if (m.crashes > m.starts)
        m.crashes = m.starts;

    const QJsonValue code = o.value(QStringLiteral("lastExitCode"));
    if (code.isDouble()) {
        const double d = code.toDouble();
        if (std::isfinite(d) && d == std::floor(d) && d >= double(std::numeric_limits<int>::min())
            && d <= double(std::numeric_limits<int>::max())) {
            m.hasLastExit = true;
            m.lastExitCode = int(d);
            const QDateTime at = QDateTime::fromString(
                o.value(QStringLiteral("lastExitAt")).toString(), kFmt);
            m.lastExitAt = at.isValid() ? truncToSec(at) : QDateTime();
        }
    }
    return m;
}

bool MetricsState::operator==(const MetricsState &o) const
{
    if (runtimeSec != o.runtimeSec || starts != o.starts || crashes != o.crashes
        || hasLastExit != o.hasLastExit)
        return false;
    if (!hasLastExit)
        return true;
    if (lastExitCode != o.lastExitCode || lastExitAt.isValid() != o.lastExitAt.isValid())
        return false;
    return !lastExitAt.isValid() || lastExitAt.toSecsSinceEpoch() == o.lastExitAt.toSecsSinceEpoch();
}

namespace MetricsFile {

QJsonObject encode(const QHash<QString, MetricsState> &all)
{
    QJsonObject inst;
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        inst.insert(it.key(), it.value().toJson());
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("instances"), inst);
    return root;
}

QHash<QString, MetricsState> decode(const QJsonObject &root, bool *ok)
{
    QHash<QString, MetricsState> out;
    const QJsonValue v = root.value(QStringLiteral("instances"));
    if (!v.isObject()) {
        if (ok)
            *ok = false;
        return out;
    }
    const QJsonObject inst = v.toObject();
    for (auto it = inst.constBegin(); it != inst.constEnd(); ++it) {
        if (it.value().isObject())
            out.insert(it.key(), MetricsState::fromJson(it.value().toObject()));
    }
    if (ok)
        *ok = true;
    return out;
}

} // namespace MetricsFile

double cpuPercent(qint64 d100ns, qint64 dWallMs, int cores)
{
    if (d100ns < 0 || dWallMs <= 0 || cores < 1)
        return 0.0;
    const double cpuMs = double(d100ns) / 10000.0;
    const double pct = cpuMs / (double(dWallMs) * double(cores)) * 100.0;
    return std::round(pct * 10.0) / 10.0;
}
