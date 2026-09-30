#include "core/PluginSet.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace PluginSet {

namespace {

const QString kDsh = QStringLiteral("dsh");
const QString kProfile = QStringLiteral("profile");
const QString kBundles = QStringLiteral("bundles");

QJsonArray bundlesArray(const QJsonObject &pkg)
{
    return pkg.value(kDsh).toObject().value(kProfile).toObject().value(kBundles).toArray();
}

} // namespace

bool isCorePackage(const QString &name)
{
    return name.startsWith(QLatin1String("@deepseek-ai/"));
}

QStringList bundles(const QJsonObject &pkg)
{
    QStringList result;
    for (const QJsonValue &v : bundlesArray(pkg)) {
        if (v.isString())
            result.append(v.toString());
    }
    return result;
}

bool isEnabled(const QJsonObject &pkg, const QString &name)
{
    return bundles(pkg).contains(name);
}

QJsonObject setEnabled(QJsonObject pkg, const QString &name, bool on)
{
    if (name.isEmpty() || isEnabled(pkg, name) == on)
        return pkg;

    QJsonArray arr = bundlesArray(pkg);
    if (on) {
        arr.append(name);
    } else {
        QJsonArray kept;
        for (const QJsonValue &v : arr) {
            if (v.isString() && v.toString() == name)
                continue;
            kept.append(v);
        }
        arr = kept;
    }

    QJsonObject dsh = pkg.value(kDsh).toObject();
    QJsonObject profile = dsh.value(kProfile).toObject();
    profile.insert(kBundles, arr);
    dsh.insert(kProfile, profile);
    pkg.insert(kDsh, dsh);
    return pkg;
}

QList<Row> list(const QJsonObject &pkg, bool showCore, const QStringList &quarantined)
{
    const QJsonObject deps = pkg.value(QStringLiteral("dependencies")).toObject();
    const QStringList enabled = bundles(pkg);
    const QSet<QString> quarantinedSet(quarantined.cbegin(), quarantined.cend());

    QList<Row> rows;
    QSet<QString> seen;
    auto append = [&](const QString &name) {
        if (name.isEmpty() || seen.contains(name))
            return;
        seen.insert(name);
        const bool core = isCorePackage(name);
        if (core && !showCore)
            return;
        Row row;
        row.name = name;
        row.version = deps.value(name).toString();
        row.displayVersion = row.version.isEmpty() ? QString(QChar(0x2014)) : row.version;
        row.enabled = enabled.contains(name);
        row.core = core;
        row.quarantined = quarantinedSet.contains(name);
        rows.append(row);
    };
    for (auto it = deps.constBegin(); it != deps.constEnd(); ++it)
        append(it.key());
    for (const QString &name : enabled)
        append(name);
    return rows;
}

void BatchSummary::record(const QString &name, bool ok, const QString &reason)
{
    ++total;
    if (ok) {
        ++succeeded;
        succeededNames.append(name);
    } else {
        failures.append(Failure{name, reason});
    }
}

bool BatchSummary::needsRestartHint(const QStringList &affected) const
{
    return succeeded >= 1 && !affected.isEmpty();
}

QJsonObject applyBatch(QJsonObject pkg, const QStringList &names, bool on,
                       const QList<bool> &outcomes, const QStringList &reasons,
                       BatchSummary *summary)
{
    for (qsizetype i = 0; i < names.size(); ++i) {
        const bool ok = i < outcomes.size() && outcomes.at(i);
        if (ok)
            pkg = setEnabled(pkg, names.at(i), on);
        if (summary)
            summary->record(names.at(i), ok, ok ? QString() : reasons.value(i));
    }
    return pkg;
}

} // namespace PluginSet
