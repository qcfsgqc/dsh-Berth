#include "EnvTable.h"

#include <QHash>

#include <algorithm>

namespace EnvTable {

bool isValidKey(const QString &key)
{
    if (key.isEmpty() || key.size() > kMaxKeyLength)
        return false;
    for (const QChar c : key) {
        if (c == u'=' || c.isSpace())
            return false;
    }
    return true;
}

bool isDshHomeKey(const QString &key)
{
    return key.compare(QStringLiteral("DSH_HOME"), Qt::CaseInsensitive) == 0;
}

QList<int> Validation::badRows() const
{
    QList<int> out = invalidRows + duplicateRows;
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

Validation validate(const QList<InstanceEnvVar> &rows, const QStringList &extraArgs)
{
    Validation v;
    v.tooManyRows = rows.size() > kMaxRows;
    v.tooManyArgs = extraArgs.size() > kMaxArgs;

    // 折叠大小写后的键 → 出现该键的行号列表
    QHash<QString, QList<int>> groups;
    for (int i = 0; i < rows.size(); ++i) {
        const QString &key = rows.at(i).key;
        if (!isValidKey(key))
            v.invalidRows.append(i);
        if (isDshHomeKey(key))
            v.dshHomeRows.append(i);
        if (!key.isEmpty())
            groups[key.toCaseFolded()].append(i);
    }

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        if (it.value().size() > 1)
            v.duplicateRows += it.value();
    }
    std::sort(v.duplicateRows.begin(), v.duplicateRows.end());
    return v;
}

} // namespace EnvTable
