#include "core/ProxyConfig.h"

#include <QUrl>

namespace ProxyConfig {

std::optional<int> parsePort(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || t.size() > 5)
        return std::nullopt;
    for (QChar c : t) {
        if (c < u'0' || c > u'9')
            return std::nullopt;
    }
    const int v = t.toInt();
    if (v < 1 || v > 65535)
        return std::nullopt;
    return v;
}

QStringList validateManual(const QString &host, const QString &portText)
{
    QStringList bad;
    if (host.trimmed().isEmpty())
        bad << QStringLiteral("host");
    if (!parsePort(portText))
        bad << QStringLiteral("port");
    return bad;
}

QStringList validateManual(const QString &host, int port)
{
    QStringList bad;
    if (host.trimmed().isEmpty())
        bad << QStringLiteral("host");
    if (port < 1 || port > 65535)
        bad << QStringLiteral("port");
    return bad;
}

QString buildUrl(const QString &scheme, const QString &host, int port,
                 const QString &user, const QString &password)
{
    if (!validateManual(host, port).isEmpty())
        return QString();

    QString s = scheme.trimmed().toLower();
    if (s.isEmpty())
        s = QStringLiteral("http");

    QString h = host.trimmed();
    if (h.contains(u':') && !h.startsWith(u'['))
        h = u'[' + h + u']';

    QString out = s + QStringLiteral("://");
    if (!user.isEmpty()) {
        out += QString::fromLatin1(QUrl::toPercentEncoding(user));
        if (!password.isEmpty())
            out += u':' + QString::fromLatin1(QUrl::toPercentEncoding(password));
        out += u'@';
    }
    out += h + u':' + QString::number(port);
    return out;
}

QString manualUrl(const ProxySettings &p, const QString &password)
{
    if (p.mode != QLatin1String("manual"))
        return QString();
    return buildUrl(p.scheme, p.host, p.port, p.user, password);
}

QHash<QString, QString> proxyEnv(const QString &url)
{
    QHash<QString, QString> env;
    if (url.isEmpty())
        return env;
    env.insert(QStringLiteral("HTTP_PROXY"), url);
    env.insert(QStringLiteral("HTTPS_PROXY"), url);
    env.insert(QStringLiteral("http_proxy"), url);
    env.insert(QStringLiteral("https_proxy"), url);
    return env;
}

bool isValidRegistryUrl(const QString &url)
{
    const QString t = url.trimmed();
    for (const QLatin1String prefix : {QLatin1String("http://"), QLatin1String("https://")}) {
        if (t.startsWith(prefix, Qt::CaseInsensitive))
            return t.size() > prefix.size();
    }
    return false;
}

QString registryUrl(const NpmRegistrySettings &r)
{
    if (r.kind == QLatin1String("official") || !isValidRegistryUrl(r.url))
        return QString::fromLatin1(kOfficialRegistry);
    return r.url.trimmed();
}

QHash<QString, QString> registryEnv(const QString &url)
{
    QHash<QString, QString> env;
    if (!url.isEmpty())
        env.insert(QStringLiteral("npm_config_registry"), url);
    return env;
}

QHash<QString, QString> childEnv(const QString &proxyUrl, const QString &registry)
{
    QHash<QString, QString> env = proxyEnv(proxyUrl);
    const QHash<QString, QString> reg = registryEnv(registry);
    for (auto it = reg.cbegin(); it != reg.cend(); ++it)
        env.insert(it.key(), it.value());
    return env;
}

} // namespace ProxyConfig
