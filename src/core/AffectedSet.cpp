#include "core/AffectedSet.h"

#include <QDir>
#include <QSet>

namespace AffectedSet {

QString normalizeHome(const QString &home)
{
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(home.trimmed()));
    // 根路径（"/"、"C:/"）保留末尾 '/'
    while (p.size() > 1 && p.endsWith(QLatin1Char('/'))
           && !(p.size() == 3 && p.at(1) == QLatin1Char(':')))
        p.chop(1);
    if (p == QLatin1String("."))
        p.clear();
    return p;
}

bool sameHome(const QString &a, const QString &b)
{
    return normalizeHome(a).compare(normalizeHome(b), Qt::CaseInsensitive) == 0;
}

QStringList affected(const QList<Instance> &instances, const QString &home, const QString &profile,
                     const QString &defaultHome)
{
    const QString target = home.isEmpty() ? defaultHome : home;
    QStringList result;
    for (const Instance &inst : instances) {
        if (inst.status != QLatin1String("starting") && inst.status != QLatin1String("running"))
            continue;
        if (inst.profile != profile)
            continue;
        if (!sameHome(inst.dshHome.isEmpty() ? defaultHome : inst.dshHome, target))
            continue;
        result.append(inst.id);
    }
    return result;
}

QStringList merge(const QStringList &a, const QStringList &b)
{
    QStringList result;
    QSet<QString> seen;
    for (const QStringList *src : {&a, &b}) {
        for (const QString &id : *src) {
            if (seen.contains(id))
                continue;
            seen.insert(id);
            result.append(id);
        }
    }
    return result;
}

} // namespace AffectedSet
