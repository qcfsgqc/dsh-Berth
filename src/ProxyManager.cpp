#include "ProxyManager.h"
#include "Settings.h"
#include "core/ProxyConfig.h"

#include <QByteArray>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {
const QString kModeNone = QStringLiteral("none");
const QString kModeSystem = QStringLiteral("system");
const QString kModeManual = QStringLiteral("manual");

// 自动探测的本机端口（需求 16.2），按此顺序逐个探测
const QList<int> kProbePorts = {7890, 7897, 1080, 10808, 10809, 8080};
constexpr int kProbeTimeoutMs = 1000;
// 整次探测的截止时间：留出余量保证 10 秒内结束
constexpr qint64 kDetectBudgetMs = 9000;
constexpr int kTestTimeoutMs = 10000;
const QString kGithubApi = QStringLiteral("https://api.github.com/");

struct Preset {
    const char *name;
    const char *url;
};
const Preset kPresets[] = {
    {"npmmirror（淘宝）", "https://registry.npmmirror.com/"},
    {"腾讯云", "https://mirrors.cloud.tencent.com/npm/"},
    {"华为云", "https://repo.huaweicloud.com/repository/npm/"},
};

QString categoryText(const QString &category) {
    if (category == QLatin1String("ok"))
        return QStringLiteral("成功");
    if (category == QLatin1String("timeout"))
        return QStringLiteral("超时");
    if (category == QLatin1String("refused"))
        return QStringLiteral("连接被拒绝");
    if (category == QLatin1String("proxyAuth"))
        return QStringLiteral("代理认证失败");
    return QStringLiteral("其他");
}

QString fieldError(const QString &field) {
    if (field == QLatin1String("host"))
        return QStringLiteral("代理主机不能为空");
    if (field == QLatin1String("port"))
        return QStringLiteral("代理端口必须是 1–65535 的整数");
    if (field == QLatin1String("registryUrl"))
        return QStringLiteral("镜像地址须以 http:// 或 https:// 开头");
    if (field == QLatin1String("mode"))
        return QStringLiteral("代理模式无效");
    if (field == QLatin1String("scheme"))
        return QStringLiteral("代理协议须为 HTTP、HTTPS 或 SOCKS5");
    return field;
}
} // namespace

ProxyManager::ProxyManager(Settings *settings, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_settings(settings), m_network(network) {
    // 先连接再 reload：解密失败时 setProxyData 触发的 proxyChanged 需要再走一次 reload 完成应用
    connect(m_settings, &Settings::proxyChanged, this, &ProxyManager::reload);
    reload();
}

ProxyManager::~ProxyManager() = default;

// ---- 已保存设置 ----

QString ProxyManager::mode() const { return m_settings->data().proxy.mode; }
QString ProxyManager::scheme() const { return m_settings->data().proxy.scheme; }
QString ProxyManager::host() const { return m_settings->data().proxy.host; }
int ProxyManager::port() const { return m_settings->data().proxy.port; }
QString ProxyManager::user() const { return m_settings->data().proxy.user; }
bool ProxyManager::hasPassword() const { return !m_settings->data().proxy.passwordDpapi.isEmpty(); }
QString ProxyManager::registryKind() const { return m_settings->data().npmRegistry.kind; }
QString ProxyManager::registryUrl() const { return m_settings->data().npmRegistry.url; }
bool ProxyManager::mirrorFallback() const { return m_settings->data().mirrorFallback; }
bool ProxyManager::inheritProxy() const { return m_settings->data().instanceInheritProxy; }

QVariantList ProxyManager::presets() const {
    QVariantList out;
    for (const Preset &p : kPresets)
        out.append(QVariantMap{{QStringLiteral("name"), QString::fromUtf8(p.name)},
                               {QStringLiteral("url"), QString::fromLatin1(p.url)}});
    return out;
}

// ---- 草稿解析与校验 ----

ProxyManager::Draft ProxyManager::parseDraft(const QVariantMap &draft) const {
    const SettingsData &cur = m_settings->data();
    Draft d;
    d.proxy = cur.proxy; // 缺省字段与未知字段 extra 沿用已保存值
    d.registry = cur.npmRegistry;
    d.mirrorFallback = cur.mirrorFallback;
    d.inheritProxy = cur.instanceInheritProxy;
    d.portText = cur.proxy.port > 0 ? QString::number(cur.proxy.port) : QString();

    const auto str = [&draft](const char *key, QString *out) {
        const QString k = QString::fromLatin1(key);
        if (draft.contains(k))
            *out = draft.value(k).toString().trimmed();
    };
    str("mode", &d.proxy.mode);
    str("scheme", &d.proxy.scheme);
    d.proxy.scheme = d.proxy.scheme.toLower();
    str("host", &d.proxy.host);
    str("user", &d.proxy.user);
    str("registryKind", &d.registry.kind);
    str("registryUrl", &d.registry.url);
    if (draft.contains(QStringLiteral("port")))
        d.portText = draft.value(QStringLiteral("port")).toString().trimmed();
    d.proxy.port = ProxyConfig::parsePort(d.portText).value_or(0);
    if (draft.contains(QStringLiteral("password"))) {
        d.passwordGiven = true;
        d.password = draft.value(QStringLiteral("password")).toString();
    }
    if (draft.contains(QStringLiteral("mirrorFallback")))
        d.mirrorFallback = draft.value(QStringLiteral("mirrorFallback")).toBool();
    if (draft.contains(QStringLiteral("inheritProxy")))
        d.inheritProxy = draft.value(QStringLiteral("inheritProxy")).toBool();
    return d;
}

QStringList ProxyManager::invalidFields(const Draft &d) const {
    QStringList bad;
    const QString &m = d.proxy.mode;
    if (m != kModeNone && m != kModeSystem && m != kModeManual)
        bad << QStringLiteral("mode");
    if (m == kModeManual) {
        const QString &s = d.proxy.scheme;
        if (!s.isEmpty() && s != QLatin1String("http") && s != QLatin1String("https")
            && s != QLatin1String("socks5"))
            bad << QStringLiteral("scheme");
        bad << ProxyConfig::validateManual(d.proxy.host, d.portText);
    }
    const QString &k = d.registry.kind;
    if (k != QLatin1String("official") && !ProxyConfig::isValidRegistryUrl(d.registry.url))
        bad << QStringLiteral("registryUrl");
    // 官方源 + 兜底：registryUrl 作为兜底镜像，填了就必须合法（留空取第一个预置镜像）
    if (k == QLatin1String("official") && !d.registry.url.isEmpty()
        && !ProxyConfig::isValidRegistryUrl(d.registry.url))
        bad << QStringLiteral("registryUrl");
    return bad;
}

QVariantMap ProxyManager::validate(const QVariantMap &draft) const {
    const QStringList bad = invalidFields(parseDraft(draft));
    QStringList msgs;
    for (const QString &f : bad)
        msgs << fieldError(f);
    return {{QStringLiteral("ok"), bad.isEmpty()},
            {QStringLiteral("errors"), bad},
            {QStringLiteral("error"), msgs.join(QStringLiteral("；"))}};
}

QVariantMap ProxyManager::save(const QVariantMap &draft) {
    const QVariantMap check = validate(draft);
    if (!check.value(QStringLiteral("ok")).toBool()) {
        QVariantMap out = check;
        out.insert(QStringLiteral("error"),
                   check.value(QStringLiteral("error")).toString() + QStringLiteral("；未保存"));
        return out;
    }
    Draft d = parseDraft(draft);
    if (d.proxy.scheme.isEmpty())
        d.proxy.scheme = QStringLiteral("http");
    if (d.passwordGiven) {
        if (d.password.isEmpty()) {
            d.proxy.passwordDpapi.clear();
        } else {
            const QString cipher = protect(d.password);
            if (cipher.isEmpty())
                return {{QStringLiteral("ok"), false},
                        {QStringLiteral("errors"), QStringList{QStringLiteral("password")}},
                        {QStringLiteral("error"), QStringLiteral("代理密码加密失败；未保存")}};
            d.proxy.passwordDpapi = cipher;
        }
        if (!m_credentialNotice.isEmpty()) {
            m_credentialNotice.clear();
            emit credentialNoticeChanged();
        }
    }
    m_settings->setProxyData(d.proxy, d.registry, d.mirrorFallback, d.inheritProxy);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("errors"), QStringList()},
            {QStringLiteral("error"), QString()}};
}

// ---- 应用 ----

void ProxyManager::reload() {
    const ProxySettings p = m_settings->data().proxy;
    m_password.clear();
    if (!p.passwordDpapi.isEmpty()) {
        QString plain;
        if (unprotect(p.passwordDpapi, &plain)) {
            m_password = plain;
        } else {
            // 解密失败（换了用户/设备或数据损坏）：只清空已存密码，其他设置不变
            const SettingsData &d = m_settings->data();
            ProxySettings cleared = p;
            cleared.passwordDpapi.clear();
            m_credentialNotice = QStringLiteral("已保存的代理密码无法解密，已清空，请重新输入");
            emit credentialNoticeChanged();
            // 会再次触发 proxyChanged → reload，此时已无密文
            m_settings->setProxyData(cleared, d.npmRegistry, d.mirrorFallback, d.instanceInheritProxy);
            return;
        }
    }
    apply();
    emit changed();
}

void ProxyManager::apply() {
    if (!m_network)
        return;
    m_network->setProxy(proxy());
    // 丢弃已建立的连接，保证新请求走新代理
    m_network->clearConnectionCache();
}

QString ProxyManager::urlFor(const ProxySettings &p, const QString &password) {
    if (p.mode == kModeSystem) {
        const SystemProxy sp = readSystemProxy();
        return sp.enabled ? ProxyConfig::buildUrl(sp.scheme, sp.host, sp.port) : QString();
    }
    return ProxyConfig::manualUrl(p, password);
}

QNetworkProxy ProxyManager::proxyFor(const ProxySettings &p, const QString &password) {
    QNetworkProxy none(QNetworkProxy::NoProxy);
    if (p.mode == kModeSystem) {
        const SystemProxy sp = readSystemProxy();
        if (!sp.enabled)
            return none;
        return QNetworkProxy(sp.scheme == QLatin1String("socks5") ? QNetworkProxy::Socks5Proxy
                                                                  : QNetworkProxy::HttpProxy,
                             sp.host, quint16(sp.port));
    }
    if (p.mode != kModeManual || !ProxyConfig::validateManual(p.host, p.port).isEmpty())
        return none;
    QNetworkProxy px(p.scheme.toLower() == QLatin1String("socks5") ? QNetworkProxy::Socks5Proxy
                                                                  : QNetworkProxy::HttpProxy,
                     p.host.trimmed(), quint16(p.port));
    if (!p.user.isEmpty()) {
        px.setUser(p.user);
        px.setPassword(password);
    }
    return px;
}

QNetworkProxy ProxyManager::proxy() const {
    return proxyFor(m_settings->data().proxy, m_password);
}

QString ProxyManager::proxyUrl() const {
    return urlFor(m_settings->data().proxy, m_password);
}

QHash<QString, QString> ProxyManager::proxyEnv() const {
    return ProxyConfig::proxyEnv(proxyUrl());
}

QString ProxyManager::registry() const {
    return ProxyConfig::registryUrl(m_settings->data().npmRegistry);
}

QHash<QString, QString> ProxyManager::childEnv() const {
    return ProxyConfig::childEnv(proxyUrl(), registry());
}

QString ProxyManager::fallbackRegistry() const {
    const SettingsData &d = m_settings->data();
    if (!d.mirrorFallback || registry() != QString::fromLatin1(ProxyConfig::kOfficialRegistry))
        return QString();
    const QString url = d.npmRegistry.url.trimmed();
    if (ProxyConfig::isValidRegistryUrl(url) && url != QString::fromLatin1(ProxyConfig::kOfficialRegistry))
        return url;
    return QString::fromLatin1(kPresets[0].url);
}

QStringList ProxyManager::secrets() const {
    QStringList out;
    if (!m_password.isEmpty())
        out << m_password;
    const QString &u = m_settings->data().proxy.user;
    if (!u.isEmpty())
        out << u;
    return out;
}

// ---- 系统代理 ----

ProxyManager::SystemProxy ProxyManager::readSystemProxy() {
    SystemProxy sp;
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings"),
                  QSettings::NativeFormat);
    if (reg.value(QStringLiteral("ProxyEnable")).toInt() == 0)
        return sp;
    const QString server = reg.value(QStringLiteral("ProxyServer")).toString().trimmed();
    if (server.isEmpty())
        return sp;
    QString hostPort = server;
    // 形如 "http=h:p;https=h:p;socks=h:p" 时优先 https，其次 http，最后 socks
    if (server.contains(u'=')) {
        QHash<QString, QString> parts;
        for (const QString &item : server.split(u';', Qt::SkipEmptyParts)) {
            const int eq = item.indexOf(u'=');
            if (eq > 0)
                parts.insert(item.left(eq).trimmed().toLower(), item.mid(eq + 1).trimmed());
        }
        if (parts.contains(QStringLiteral("https")))
            hostPort = parts.value(QStringLiteral("https"));
        else if (parts.contains(QStringLiteral("http")))
            hostPort = parts.value(QStringLiteral("http"));
        else if (parts.contains(QStringLiteral("socks"))) {
            hostPort = parts.value(QStringLiteral("socks"));
            sp.scheme = QStringLiteral("socks5");
        } else
            return sp;
    }
    const int sep = hostPort.indexOf(QStringLiteral("://"));
    if (sep >= 0)
        hostPort = hostPort.mid(sep + 3);
    const int colon = hostPort.lastIndexOf(u':');
    if (colon <= 0)
        return sp;
    const auto port = ProxyConfig::parsePort(hostPort.mid(colon + 1));
    QString host = hostPort.left(colon).trimmed();
    if (host.startsWith(u'[') && host.endsWith(u']'))
        host = host.mid(1, host.size() - 2);
    if (!port || host.isEmpty())
        return sp;
    sp.host = host;
    sp.port = *port;
    sp.enabled = true;
#endif
    return sp;
}

// ---- DPAPI ----

QString ProxyManager::protect(const QString &plain) {
#ifdef Q_OS_WIN
    QByteArray in = plain.toUtf8();
    DATA_BLOB inBlob{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())};
    DATA_BLOB outBlob{0, nullptr};
    if (!CryptProtectData(&inBlob, L"DSH Berth proxy", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
        return QString();
    const QByteArray cipher(reinterpret_cast<const char *>(outBlob.pbData), int(outBlob.cbData));
    LocalFree(outBlob.pbData);
    return QString::fromLatin1(cipher.toBase64());
#else
    Q_UNUSED(plain);
    return QString();
#endif
}

bool ProxyManager::unprotect(const QString &cipher, QString *plain) {
#ifdef Q_OS_WIN
    const auto decoded = QByteArray::fromBase64Encoding(cipher.toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty())
        return false;
    QByteArray in = decoded.decoded;
    DATA_BLOB inBlob{DWORD(in.size()), reinterpret_cast<BYTE *>(in.data())};
    DATA_BLOB outBlob{0, nullptr};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                            &outBlob))
        return false;
    *plain = QString::fromUtf8(reinterpret_cast<const char *>(outBlob.pbData), int(outBlob.cbData));
    SecureZeroMemory(outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
#else
    Q_UNUSED(cipher);
    Q_UNUSED(plain);
    return false;
#endif
}

// ---- 自动探测 ----

void ProxyManager::detect() {
    if (m_detecting)
        return;
    m_detecting = true;
    emit detectingChanged();
    m_candidates.clear();
    const SystemProxy sp = readSystemProxy();
    if (sp.enabled)
        m_candidates.append(QVariantMap{{QStringLiteral("scheme"), sp.scheme},
                                        {QStringLiteral("host"), sp.host},
                                        {QStringLiteral("port"), sp.port},
                                        {QStringLiteral("source"), QStringLiteral("system")},
                                        {QStringLiteral("sourceText"), QStringLiteral("系统代理")}});
    emit candidatesChanged();
    m_probePorts = kProbePorts;
    m_detectClock.start();
    probeNext();
}

void ProxyManager::probeNext() {
    if (m_probePorts.isEmpty() || m_detectClock.elapsed() > kDetectBudgetMs) {
        finishDetect();
        return;
    }
    const int port = m_probePorts.takeFirst();
    auto *sock = new QTcpSocket(this);
    sock->setProxy(QNetworkProxy::NoProxy);
    auto *timer = new QTimer(sock);
    timer->setSingleShot(true);
    m_probeSocket = sock;
    const auto done = [this, sock, port](bool ok) {
        if (m_probeSocket != sock)
            return;
        m_probeSocket = nullptr;
        if (ok) {
            bool dup = false;
            for (const QVariant &v : std::as_const(m_candidates)) {
                const QVariantMap c = v.toMap();
                const QString h = c.value(QStringLiteral("host")).toString();
                if (c.value(QStringLiteral("port")).toInt() == port
                    && (h == QLatin1String("127.0.0.1") || h.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0))
                    dup = true;
            }
            if (!dup) {
                m_candidates.append(QVariantMap{{QStringLiteral("scheme"), QStringLiteral("http")},
                                                {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                                                {QStringLiteral("port"), port},
                                                {QStringLiteral("source"), QStringLiteral("probe")},
                                                {QStringLiteral("sourceText"), QStringLiteral("本机端口探测")}});
                emit candidatesChanged();
            }
        }
        sock->abort();
        sock->deleteLater();
        QTimer::singleShot(0, this, &ProxyManager::probeNext);
    };
    connect(sock, &QTcpSocket::connected, this, [done]() { done(true); });
    connect(sock, &QTcpSocket::errorOccurred, this, [done]() { done(false); });
    connect(timer, &QTimer::timeout, this, [done]() { done(false); });
    timer->start(kProbeTimeoutMs);
    sock->connectToHost(QHostAddress(QHostAddress::LocalHost), quint16(port));
}

void ProxyManager::finishDetect() {
    m_detecting = false;
    emit detectingChanged();
    const bool found = !m_candidates.isEmpty();
    emit detectFinished(found, found ? QStringLiteral("发现 %1 个候选代理").arg(m_candidates.size())
                                     : QStringLiteral("未发现可用代理"));
}

// ---- 测试连接 ----

void ProxyManager::test(const QVariantMap &draft) {
    if (m_testing)
        return;
    const Draft d = parseDraft(draft);
    m_testResults.clear();
    const QString registryUrl = ProxyConfig::registryUrl(d.registry);
    const QStringList targets{QStringLiteral("npm"), QStringLiteral("github")};
    const QStringList names{QStringLiteral("npm registry"), QStringLiteral("GitHub API")};
    const QStringList urls{registryUrl, kGithubApi};

    // 手动模式字段非法时不发请求，直接给出结果
    if (d.proxy.mode == kModeManual && !ProxyConfig::validateManual(d.proxy.host, d.portText).isEmpty()) {
        for (int i = 0; i < targets.size(); ++i)
            m_testResults.append(QVariantMap{{QStringLiteral("target"), targets.at(i)},
                                             {QStringLiteral("name"), names.at(i)},
                                             {QStringLiteral("url"), urls.at(i)},
                                             {QStringLiteral("ok"), false},
                                             {QStringLiteral("ms"), 0},
                                             {QStringLiteral("status"), 0},
                                             {QStringLiteral("category"), QStringLiteral("other")},
                                             {QStringLiteral("categoryText"), categoryText(QStringLiteral("other"))},
                                             {QStringLiteral("error"), QStringLiteral("代理主机或端口非法")}});
        emit testResultsChanged();
        emit testFinished();
        return;
    }

    if (!m_testNetwork)
        m_testNetwork = new QNetworkAccessManager(this);
    m_testNetwork->clearConnectionCache();
    m_testNetwork->setProxy(proxyFor(d.proxy, d.passwordGiven ? d.password : m_password));

    m_testing = true;
    emit testingChanged();
    m_testPending = targets.size();
    for (int i = 0; i < targets.size(); ++i)
        m_testResults.append(QVariantMap{{QStringLiteral("target"), targets.at(i)},
                                         {QStringLiteral("name"), names.at(i)},
                                         {QStringLiteral("url"), urls.at(i)},
                                         {QStringLiteral("pending"), true}});
    emit testResultsChanged();
    for (int i = 0; i < targets.size(); ++i)
        startTestRequest(i, urls.at(i));
}

void ProxyManager::startTestRequest(int index, const QString &url) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("dsh-berth"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QElapsedTimer clock;
    clock.start();
    QNetworkReply *reply = m_testNetwork->get(req);
    auto timedOut = std::make_shared<bool>(false);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, timedOut]() {
        *timedOut = true;
        reply->abort();
    });
    timer->start(kTestTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, index, reply, timedOut, clock]() {
        onTestReply(index, reply, *timedOut, clock.elapsed());
    });
}

void ProxyManager::onTestReply(int index, QNetworkReply *reply, bool timedOut, qint64 ms) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    QString category;
    if (timedOut)
        category = QStringLiteral("timeout");
    else if (err == QNetworkReply::ProxyAuthenticationRequiredError || status == 407)
        category = QStringLiteral("proxyAuth");
    else if (err == QNetworkReply::ConnectionRefusedError || err == QNetworkReply::ProxyConnectionRefusedError)
        category = QStringLiteral("refused");
    else if (err == QNetworkReply::TimeoutError || err == QNetworkReply::ProxyTimeoutError)
        category = QStringLiteral("timeout");
    else if (err == QNetworkReply::NoError || (status > 0 && status < 500))
        category = QStringLiteral("ok"); // 收到 HTTP 响应（含 4xx，如 GitHub 限流）即视为连通
    else
        category = QStringLiteral("other");

    QString error;
    if (category == QLatin1String("timeout"))
        error = QStringLiteral("超过 10 秒无响应");
    else if (category != QLatin1String("ok"))
        error = reply->errorString();

    if (index >= 0 && index < m_testResults.size()) {
        QVariantMap r = m_testResults.at(index).toMap();
        r.remove(QStringLiteral("pending"));
        r.insert(QStringLiteral("ok"), category == QLatin1String("ok"));
        r.insert(QStringLiteral("ms"), ms);
        r.insert(QStringLiteral("status"), status);
        r.insert(QStringLiteral("category"), category);
        r.insert(QStringLiteral("categoryText"), categoryText(category));
        r.insert(QStringLiteral("error"), error);
        m_testResults[index] = r;
    }
    reply->deleteLater();
    emit testResultsChanged();
    if (--m_testPending <= 0) {
        m_testing = false;
        emit testingChanged();
        emit testFinished();
    }
}
