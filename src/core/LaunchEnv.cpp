#include "LaunchEnv.h"

#include <algorithm>

namespace {

// 删除所有与 key 不区分大小写同名的键，再写入 key=value
void setCaseInsensitive(QProcessEnvironment &env, const QString &key, const QString &value)
{
    const QStringList keys = env.keys();
    for (const QString &k : keys) {
        if (k.compare(key, Qt::CaseInsensitive) == 0)
            env.remove(k);
    }
    env.insert(key, value);
}

} // namespace

namespace LaunchEnv {

QProcessEnvironment build(const QProcessEnvironment &sys,
                          const QList<InstanceEnvVar> &envTable,
                          const QString &dshHome,
                          const QHash<QString, QString> &proxyEnv,
                          bool inheritProxy)
{
    QProcessEnvironment env = sys;

    if (inheritProxy) {
        // QHash 遍历顺序不确定，排序后写入保证结果可复现
        QStringList proxyKeys = proxyEnv.keys();
        std::sort(proxyKeys.begin(), proxyKeys.end());
        for (const QString &k : proxyKeys) {
            if (!k.isEmpty())
                setCaseInsensitive(env, k, proxyEnv.value(k));
        }
    }

    for (const InstanceEnvVar &row : envTable) {
        if (!row.key.isEmpty())
            setCaseInsensitive(env, row.key, row.value);
    }

    setCaseInsensitive(env, QStringLiteral("DSH_HOME"), dshHome);
    return env;
}

QStringList filterArgs(const QStringList &extra, QStringList *conflicts)
{
    static const QString kPort = QStringLiteral("--port");
    static const QString kProfile = QStringLiteral("--profile");
    static const QString kPortEq = QStringLiteral("--port=");
    static const QString kProfileEq = QStringLiteral("--profile=");

    QStringList out;
    out.reserve(extra.size());
    for (qsizetype i = 0; i < extra.size(); ++i) {
        const QString &a = extra.at(i);
        if (a == kPort || a == kProfile) {
            if (conflicts)
                conflicts->append(a);
            if (i + 1 < extra.size()) {
                ++i;
                if (conflicts)
                    conflicts->append(extra.at(i));
            }
            continue;
        }
        if (a.startsWith(kPortEq) || a.startsWith(kProfileEq)) {
            if (conflicts)
                conflicts->append(a);
            continue;
        }
        out.append(a);
    }
    return out;
}

} // namespace LaunchEnv
