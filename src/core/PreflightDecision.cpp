#include "core/PreflightDecision.h"

#include <QHash>
#include <QSet>

namespace PreflightDecision {

namespace {

std::optional<Reason> repairReason(RepairOutcome repair)
{
    switch (repair) {
    case RepairOutcome::Failed:
        return Reason::InstallFailed;
    case RepairOutcome::TimedOut:
        return Reason::InstallTimeout;
    case RepairOutcome::NoPackageManager:
        return Reason::NoPackageManager;
    case RepairOutcome::NotAttempted:
    case RepairOutcome::Succeeded:
        break;
    }
    return std::nullopt;
}

} // namespace

Decision decide(const QList<CheckResult> &before, const std::optional<QList<CheckResult>> &after,
                RepairOutcome repair)
{
    // after 中同名结果取首次出现
    QHash<QString, const CheckResult *> afterByName;
    if (after) {
        for (const CheckResult &r : *after) {
            if (!afterByName.contains(r.name))
                afterByName.insert(r.name, &r);
        }
    }

    const std::optional<Reason> forced = repairReason(repair);
    Decision d;
    QSet<QString> seen;
    for (const CheckResult &b : before) {
        if (seen.contains(b.name))
            continue;
        seen.insert(b.name);

        const CheckResult *fin = afterByName.value(b.name, &b);
        if (fin->ok) {
            d.remaining.append(b.name);
            continue;
        }
        Quarantined q;
        q.name = b.name;
        q.reason = forced ? *forced : fin->failure;
        q.detail = fin->detail;
        d.quarantined.append(q);
    }
    return d;
}

QString reasonString(Reason r)
{
    switch (r) {
    case Reason::Missing:
        return QStringLiteral("missing");
    case Reason::DepUnresolved:
        return QStringLiteral("dep-unresolved");
    case Reason::InstallFailed:
        return QStringLiteral("install-failed");
    case Reason::InstallTimeout:
        return QStringLiteral("install-timeout");
    case Reason::NoPackageManager:
        return QStringLiteral("no-package-manager");
    }
    return QStringLiteral("missing");
}

std::optional<Reason> reasonFromString(const QString &s)
{
    for (Reason r : {Reason::Missing, Reason::DepUnresolved, Reason::InstallFailed,
                     Reason::InstallTimeout, Reason::NoPackageManager}) {
        if (reasonString(r) == s)
            return r;
    }
    return std::nullopt;
}

} // namespace PreflightDecision
