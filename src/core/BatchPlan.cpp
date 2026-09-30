#include "core/BatchPlan.h"

#include <algorithm>

namespace BatchPlan {

bool isActiveState(const QString &status)
{
    return status == QLatin1String("starting") || status == QLatin1String("running")
        || status == QLatin1String("stopping");
}

Plan plan(const QStringList &targets, const QHash<QString, QString> &statuses, int intervalSec)
{
    const qint64 intervalMs = qint64(std::clamp(intervalSec, 0, 60)) * 1000;

    Plan result;
    qint64 k = 0;
    for (const QString &id : targets) {
        if (isActiveState(statuses.value(id))) {
            result.skipped.append(id);
            continue;
        }
        result.steps.append(Step{id, k * intervalMs});
        ++k;
    }
    return result;
}

QStringList cancel(const Plan &plan, qint64 elapsedMs)
{
    QStringList pending;
    for (const Step &step : plan.steps) {
        if (step.atMs > elapsedMs)
            pending.append(step.id);
    }
    return pending;
}

} // namespace BatchPlan
