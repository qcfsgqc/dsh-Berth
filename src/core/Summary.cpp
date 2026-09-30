#include "core/Summary.h"

#include <cmath>

namespace Summary {

namespace {

double sanitize(double v)
{
    return (std::isfinite(v) && v > 0.0) ? v : 0.0;
}

double round1(double v)
{
    return std::round(v * 10.0) / 10.0;
}

} // namespace

bool isRunning(const QString &status)
{
    return status == QLatin1String("running") || status == QLatin1String("external");
}

bool isFailed(const QString &status)
{
    return status == QLatin1String("failed") || status == QLatin1String("crashStopped");
}

Result compute(const QMap<QString, QString> &statuses, const QHash<QString, Sample> &samples)
{
    Result r;
    r.total = int(statuses.size());
    double mem = 0.0;
    double cpu = 0.0;
    for (auto it = statuses.cbegin(); it != statuses.cend(); ++it) {
        if (isFailed(it.value())) {
            ++r.failed;
            continue;
        }
        if (!isRunning(it.value()))
            continue;
        ++r.running;
        const auto s = samples.constFind(it.key());
        if (s == samples.cend())
            continue;
        mem += sanitize(s->memMb);
        cpu += sanitize(s->cpuPercent);
    }
    r.memMb = mem;
    r.cpuPercent = round1(cpu);
    return r;
}

QString formatMem(double mb)
{
    const double v = sanitize(mb);
    if (v < 1024.0)
        return QStringLiteral("%1 MB").arg(qint64(std::floor(v)));
    return QStringLiteral("%1 GB").arg(v / 1024.0, 0, 'f', 1);
}

QString formatCpu(double percent)
{
    return QStringLiteral("%1%").arg(round1(sanitize(percent)), 0, 'f', 1);
}

} // namespace Summary
