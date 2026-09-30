#include "UpdateCenter.h"

#include "DshUpdate.h"
#include "DshVersionStore.h"
#include "PluginManager.h"
#include "ProcessRunner.h"
#include "Settings.h"
#include "core/SemVer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace {
const QString kDshPackage = QStringLiteral("@deepseek-ai/dsh");
const QString kSystemId = QStringLiteral("dsh:system");
const QString kOfficialRegistry = QStringLiteral("https://registry.npmjs.org/");
constexpr int kRequestTimeoutMs = 30000;

// 从 `dsh --version` 之类的输出里取第一个像 SemVer 的片段；找不到时退回第一行（之后显示"版本无法比较"）
QString extractVersion(const QString &text) {
    static const QRegularExpression re(
        QStringLiteral("[vV]?(\\d+\\.\\d+\\.\\d+(?:-[0-9A-Za-z.-]+)?(?:\\+[0-9A-Za-z.-]+)?)"));
    const QRegularExpressionMatch m = re.match(text);
    if (m.hasMatch())
        return m.captured(1);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            return t;
    }
    return {};
}

// 声明版本去掉开头的范围符号（^1.2.3 → 1.2.3）；无法对应到具体版本的（git URL 等）原样返回
QString stripRange(const QString &declared) {
    QString v = declared.trimmed();
    while (!v.isEmpty() && QStringLiteral("^~=<> ").contains(v.front()))
        v.remove(0, 1);
    return v;
}
} // namespace

UpdateCenter::UpdateCenter(ProcessRunner *runner, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_runner(runner), m_network(network) {}

void UpdateCenter::setDeps(Deps deps) {
    m_deps = std::move(deps);
    if (m_deps.versions)
        connect(m_deps.versions, &DshVersionStore::versionsChanged, this, &UpdateCenter::refresh);
    if (m_deps.plugins)
        connect(m_deps.plugins, &PluginManager::pluginsChanged, this, &UpdateCenter::refresh);
    if (m_deps.settings)
        connect(m_deps.settings, &Settings::updateCheckHoursChanged, this, &UpdateCenter::applySchedule);

    // 升级结果：排队连接，避免在 startUpgrade 内同步回调时重入
    if (m_deps.dshUpdate) {
        connect(m_deps.dshUpdate, &DshUpdate::updateFinished, this,
                [this](bool ok, const QString &message) {
            const int i = indexOf(m_currentId);
            if (i < 0 || m_items.at(i).kind != Kind::SystemDsh)
                return;
            const QString ver = ok ? extractVersion(m_deps.dshUpdate->installedVersion()) : QString();
            finishUpgrade(ok, message, ver);
        }, Qt::QueuedConnection);
    }
    if (m_deps.versions) {
        connect(m_deps.versions, &DshVersionStore::installFinished, this,
                [this](const QString &version, bool ok, const QString &message) {
            const int i = indexOf(m_currentId);
            if (i < 0 || m_items.at(i).kind != Kind::StoreDsh || version != m_upgradeTarget)
                return;
            finishUpgrade(ok, message, ok ? version : QString());
        }, Qt::QueuedConnection);
    }
    if (m_deps.plugins) {
        connect(m_deps.plugins, &PluginManager::installFinished, this,
                [this](const QString &home, const QString &profile, const QString &, bool ok,
                       const QString &, const QString &message, const QString &tail) {
            const int i = indexOf(m_currentId);
            if (i < 0 || m_items.at(i).kind != Kind::Plugin
                || m_deps.plugins->keyOf(home, profile) != m_items.at(i).group)
                return;
            const Item &it = m_items.at(i);
            QString ver;
            if (ok) {
                ver = installedPluginVersion(it.home, it.profile, it.name, QString());
                if (ver.isEmpty())
                    ver = m_upgradeTarget;
            }
            const QString detail = ok || tail.isEmpty() ? message : message + QLatin1Char('\n') + tail;
            finishUpgrade(ok, detail, ver);
        }, Qt::QueuedConnection);
    }

    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() { checkAll(); });
    }
    rebuild();
    applySchedule();
    emit itemsChanged();
}

// ---------------------------------------------------------------------------
// 条目
// ---------------------------------------------------------------------------

void UpdateCenter::rebuild() {
    QHash<QString, Item> old;
    for (const Item &it : m_items)
        old.insert(it.id, it);

    QList<Item> next;
    {
        Item sys;
        sys.id = kSystemId;
        sys.kind = Kind::SystemDsh;
        sys.name = kDshPackage;
        sys.label = QStringLiteral("系统 dsh");
        sys.group = QStringLiteral("dsh");
        sys.groupLabel = QStringLiteral("dsh");
        sys.tag = QStringLiteral("latest");
        next.append(sys);
    }
    if (m_deps.versions) {
        for (const QVariant &v : m_deps.versions->versions()) {
            const QString ver = v.toMap().value(QStringLiteral("version")).toString();
            if (ver.isEmpty())
                continue;
            Item s;
            s.id = QStringLiteral("dsh:store:") + ver;
            s.kind = Kind::StoreDsh;
            s.name = kDshPackage;
            s.label = QStringLiteral("dsh %1（多版本）").arg(ver);
            s.group = QStringLiteral("dsh");
            s.groupLabel = QStringLiteral("dsh");
            s.tag = QStringLiteral("latest");
            s.current = ver;
            next.append(s);
        }
    }
    if (m_deps.plugins) {
        for (const QVariant &tv : m_deps.plugins->targets()) {
            const QVariantMap t = tv.toMap();
            const QString home = t.value(QStringLiteral("home")).toString();
            const QString profile = t.value(QStringLiteral("profile")).toString();
            const QString key = t.value(QStringLiteral("key")).toString();
            const QVariantMap res = m_deps.plugins->list(home, profile, false);
            if (!res.value(QStringLiteral("ok")).toBool())
                continue;
            for (const QVariant &pv : res.value(QStringLiteral("plugins")).toList()) {
                const QVariantMap p = pv.toMap();
                if (p.value(QStringLiteral("core")).toBool())
                    continue;
                const QString name = p.value(QStringLiteral("name")).toString();
                if (name.isEmpty())
                    continue;
                Item it;
                it.id = QStringLiteral("plugin:") + key + QLatin1Char('|') + name;
                it.kind = Kind::Plugin;
                it.name = name;
                it.label = name;
                it.home = home;
                it.profile = profile;
                it.group = key;
                it.groupLabel = t.value(QStringLiteral("label")).toString();
                const QString recorded =
                    m_deps.settings ? m_deps.settings->pluginChannel(key + QLatin1Char('|') + name) : QString();
                it.channel = recorded.isEmpty() ? QStringLiteral("stable") : recorded;
                it.tag = m_deps.plugins->distTag(it.channel);
                if (it.tag.isEmpty())
                    it.tag = QStringLiteral("latest");
                it.current = installedPluginVersion(home, profile, name,
                                                    p.value(QStringLiteral("version")).toString());
                next.append(it);
            }
        }
    }

    // 保留已有的检查结果与进行中标记
    for (Item &it : next) {
        const auto prev = old.constFind(it.id);
        if (prev == old.constEnd())
            continue;
        if (it.kind == Kind::SystemDsh)
            it.current = prev->current;
        it.latest = prev->latest;
        it.checkError = prev->checkError;
        it.upgradeError = prev->upgradeError;
        it.lastChecked = prev->lastChecked;
        it.pendingParts = prev->pendingParts;
        it.upgrading = prev->upgrading;
    }
    m_items = next;
}

QString UpdateCenter::installedPluginVersion(const QString &home, const QString &profile, const QString &name,
                                             const QString &declared) const {
    const QString resolved = m_deps.resolveHome ? m_deps.resolveHome(home) : home;
    QFile file(resolved + QStringLiteral("/profiles/") + profile + QStringLiteral("/node_modules/") + name
               + QStringLiteral("/package.json"));
    if (file.open(QIODevice::ReadOnly)) {
        const QString v = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("version")).toString();
        if (!v.isEmpty())
            return v;
    }
    return stripRange(declared);
}

int UpdateCenter::indexOf(const QString &id) const {
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

bool UpdateCenter::hasUpdate(const Item &item) const {
    if (!item.checkError.isEmpty() || item.latest.isEmpty())
        return false;
    const auto cur = SemVer::parse(item.current.trimmed());
    const auto lat = SemVer::parse(item.latest.trimmed());
    if (!cur || !lat || SemVer::compare(*lat, *cur) <= 0)
        return false;
    // 最新版本已作为独立目录装好：不用再升级
    if (item.kind == Kind::StoreDsh && m_deps.versions && m_deps.versions->isInstalled(item.latest.trimmed()))
        return false;
    return true;
}

UpdateCenter::State UpdateCenter::stateOf(const Item &item) const {
    if (item.upgrading)
        return State::Upgrading;
    if (item.pendingParts > 0)
        return State::Checking;
    if (!item.checkError.isEmpty())
        return State::Failed;
    if (item.latest.isEmpty())
        return State::Unchecked;
    const auto cur = SemVer::parse(item.current.trimmed());
    const auto lat = SemVer::parse(item.latest.trimmed());
    if (!cur || !lat)
        return State::Incomparable;
    if (SemVer::compare(*lat, *cur) <= 0)
        return State::UpToDate;
    return hasUpdate(item) ? State::HasUpdate : State::LatestInstalled;
}

QVariantMap UpdateCenter::itemMap(const Item &item) const {
    static const char *const kStateKeys[] = {"unchecked", "checking", "upgrading", "upToDate",
                                             "hasUpdate", "incomparable", "latestInstalled", "failed"};
    static const char *const kKindKeys[] = {"systemDsh", "storeDsh", "plugin"};
    QString stateText;
    const State state = stateOf(item);
    switch (state) {
    case State::Unchecked: stateText = QStringLiteral("未检查"); break;
    case State::Checking: stateText = QStringLiteral("检查中"); break;
    case State::Upgrading: stateText = QStringLiteral("升级中"); break;
    case State::UpToDate: stateText = QStringLiteral("已是最新"); break;
    case State::HasUpdate: stateText = QStringLiteral("有更新"); break;
    case State::Incomparable: stateText = QStringLiteral("版本无法比较"); break;
    case State::LatestInstalled: stateText = QStringLiteral("最新版本已安装"); break;
    case State::Failed: stateText = QStringLiteral("检查失败"); break;
    }
    QVariantMap m;
    m.insert(QStringLiteral("id"), item.id);
    m.insert(QStringLiteral("kind"), QString::fromLatin1(kKindKeys[int(item.kind)]));
    m.insert(QStringLiteral("section"), item.kind == Kind::Plugin ? QStringLiteral("plugin") : QStringLiteral("dsh"));
    m.insert(QStringLiteral("group"), item.group);
    m.insert(QStringLiteral("groupLabel"), item.groupLabel);
    m.insert(QStringLiteral("name"), item.name);
    m.insert(QStringLiteral("label"), item.label);
    m.insert(QStringLiteral("home"), item.home);
    m.insert(QStringLiteral("profile"), item.profile);
    m.insert(QStringLiteral("channel"), item.channel);
    m.insert(QStringLiteral("current"), item.current);
    m.insert(QStringLiteral("latest"), item.latest);
    m.insert(QStringLiteral("lastChecked"),
             item.lastChecked.isValid() ? item.lastChecked.toString(Qt::ISODate) : QString());
    m.insert(QStringLiteral("lastCheckedText"),
             item.lastChecked.isValid() ? item.lastChecked.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                        : QStringLiteral("未检查"));
    m.insert(QStringLiteral("state"), QString::fromLatin1(kStateKeys[int(state)]));
    m.insert(QStringLiteral("stateText"), stateText);
    m.insert(QStringLiteral("hasUpdate"), hasUpdate(item));
    m.insert(QStringLiteral("busy"), item.upgrading || item.pendingParts > 0);
    m.insert(QStringLiteral("error"), item.upgradeError.isEmpty() ? item.checkError : item.upgradeError);
    return m;
}

QVariantList UpdateCenter::items() const {
    QVariantList out;
    out.reserve(m_items.size());
    for (const Item &it : m_items)
        out.append(itemMap(it));
    return out;
}

int UpdateCenter::updateCount() const {
    int n = 0;
    for (const Item &it : m_items) {
        if (hasUpdate(it))
            ++n;
    }
    return n;
}

QString UpdateCenter::lastCheckAt() const {
    return m_lastCheck.isValid() ? m_lastCheck.toString(Qt::ISODate) : QString();
}

QString UpdateCenter::lastCheckText() const {
    return m_lastCheck.isValid() ? m_lastCheck.toString(QStringLiteral("yyyy-MM-dd HH:mm")) : QString();
}

bool UpdateCenter::canUpgradeAll() const {
    return !m_checking && !m_upgrading && updateCount() > 0;
}

void UpdateCenter::refresh() {
    if (m_checking) {
        m_dirty = true;
        return;
    }
    rebuild();
    emit itemsChanged();
}

// ---------------------------------------------------------------------------
// 检查
// ---------------------------------------------------------------------------

bool UpdateCenter::checkAll() {
    if (m_checking || m_upgrading || !m_network || !m_runner)
        return false;
    rebuild();
    m_checking = true;
    m_dirty = false;
    // 守卫计数：全部请求发出前不让 finishCheckIfIdle 提前收尾
    m_pending = 1;
    QStringList packages;
    for (Item &it : m_items) {
        it.pendingParts = it.kind == Kind::SystemDsh ? 2 : 1;
        it.checkError.clear();
        it.upgradeError.clear();
        if (!packages.contains(it.name))
            packages.append(it.name);
    }
    emit checkingChanged();
    emit itemsChanged();

    checkSystemDsh();
    for (const QString &name : packages)
        fetchPackage(name);
    --m_pending;
    finishCheckIfIdle();
    return true;
}

void UpdateCenter::checkSystemDsh() {
    const QString exe = m_deps.settings ? m_deps.settings->resolvedDshExecutable() : QStringLiteral("dsh");
    ++m_pending;
    ProcessOptions options;
    options.timeoutMs = kRequestTimeoutMs;
    options.tailLines = 5;
    ProcessTask *task = m_runner->run(exe, {QStringLiteral("--version")}, options);
    connect(task, &ProcessTask::finished, this,
            [this, task](bool ok, int code, const QString &tail, bool timedOut) {
        const int i = indexOf(kSystemId);
        if (i >= 0) {
            Item &it = m_items[i];
            if (ok) {
                const QString ver = extractVersion(tail);
                if (ver.isEmpty())
                    it.checkError = QStringLiteral("dsh --version 没有输出版本信息");
                else
                    it.current = ver;
            } else if (task->failedToStart()) {
                it.checkError = QStringLiteral("无法运行 dsh：") + task->errorString();
            } else if (timedOut) {
                it.checkError = QStringLiteral("读取本机 dsh 版本超时（30 秒）");
            } else {
                it.checkError = QStringLiteral("读取本机 dsh 版本失败（退出码 %1）").arg(code);
            }
            if (it.pendingParts > 0)
                partDone(it);
        }
        --m_pending;
        emit itemsChanged();
        finishCheckIfIdle();
    });
}

void UpdateCenter::fetchPackage(const QString &name) {
    ++m_pending;
    QString base = m_deps.registry ? m_deps.registry().trimmed() : QString();
    if (base.isEmpty())
        base = kOfficialRegistry;
    if (!base.endsWith(QLatin1Char('/')))
        base.append(QLatin1Char('/'));
    QString encoded = name;
    encoded.replace(QLatin1Char('/'), QStringLiteral("%2F"));
    const QUrl url(base + encoded);
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        applyPackage(name, false, {}, QStringLiteral("registry 地址无效：") + base);
        --m_pending;
        return;
    }

    QNetworkRequest request(url);
    // 精简元数据即可拿到 dist-tags，响应体小很多
    request.setRawHeader("Accept", "application/vnd.npm.install-v1+json; q=1.0, application/json; q=0.8, */*");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply *reply = m_network->get(request);
    m_timedOut.insert(reply, false);
    // 单次请求总时长 30 秒；reply 先结束时定时器随之取消
    QTimer::singleShot(kRequestTimeoutMs, reply, [this, reply]() {
        if (reply->isRunning()) {
            m_timedOut.insert(reply, true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() { onPackageReply(reply, name); });
}

void UpdateCenter::onPackageReply(QNetworkReply *reply, const QString &name) {
    const bool timedOut = m_timedOut.take(reply);
    reply->deleteLater();

    QHash<QString, QString> tags;
    QString error;
    if (timedOut) {
        error = QStringLiteral("检查超时（30 秒）");
    } else if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        error = status == 404 ? QStringLiteral("registry 上找不到该包")
                              : QStringLiteral("网络错误：") + reply->errorString();
    } else {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        const QJsonValue distTags = doc.object().value(QStringLiteral("dist-tags"));
        if (parseError.error != QJsonParseError::NoError || !doc.isObject() || !distTags.isObject()) {
            error = QStringLiteral("响应无法解析");
        } else {
            const QJsonObject obj = distTags.toObject();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString())
                    tags.insert(it.key(), it.value().toString());
            }
        }
    }
    applyPackage(name, error.isEmpty(), tags, error);
    --m_pending;
    emit itemsChanged();
    finishCheckIfIdle();
}

void UpdateCenter::applyPackage(const QString &name, bool ok, const QHash<QString, QString> &tags,
                                const QString &error) {
    for (Item &it : m_items) {
        if (it.name != name || it.pendingParts <= 0)
            continue;
        if (!ok) {
            if (it.checkError.isEmpty())
                it.checkError = error;
        } else {
            const QString v = tags.value(it.tag).trimmed();
            if (v.isEmpty()) {
                if (it.checkError.isEmpty())
                    it.checkError = QStringLiteral("渠道 %1（dist-tag %2）没有可用版本").arg(it.channel, it.tag);
            } else {
                it.latest = v;
            }
        }
        partDone(it);
    }
}

void UpdateCenter::partDone(Item &item) {
    if (--item.pendingParts > 0)
        return;
    item.pendingParts = 0;
    if (item.checkError.isEmpty())
        item.lastChecked = QDateTime::currentDateTime();
}

void UpdateCenter::finishCheckIfIdle() {
    if (!m_checking || m_pending > 0)
        return;
    m_checking = false;
    m_lastCheck = QDateTime::currentDateTime();
    int failed = 0;
    for (Item &it : m_items) {
        it.pendingParts = 0;
        if (!it.checkError.isEmpty())
            ++failed;
    }
    if (m_dirty) {
        m_dirty = false;
        rebuild();
    }
    emit checkingChanged();
    emit itemsChanged();
    emit checkFinished(updateCount(), failed);
}

void UpdateCenter::applySchedule() {
    if (!m_timer)
        return;
    const int hours = m_deps.settings ? m_deps.settings->updateCheckHours() : 24;
    if (hours <= 0) {
        m_timer->stop();
        return;
    }
    m_timer->start(hours * 3600 * 1000);
}

// ---------------------------------------------------------------------------
// 升级
// ---------------------------------------------------------------------------

namespace {
QVariantMap result(bool ok, const QString &error = {}) {
    QVariantMap m;
    m.insert(QStringLiteral("ok"), ok);
    m.insert(QStringLiteral("error"), error);
    return m;
}
} // namespace

QVariantMap UpdateCenter::upgrade(const QString &itemId) {
    const int i = indexOf(itemId);
    if (i < 0)
        return result(false, QStringLiteral("条目不存在"));
    if (m_checking)
        return result(false, QStringLiteral("检查进行中，请稍后"));
    if (m_upgrading)
        return result(false, QStringLiteral("有升级正在进行"));
    if (!hasUpdate(m_items.at(i)))
        return result(false, QStringLiteral("该项没有可用更新"));
    m_batch = false;
    m_queue = {itemId};
    setUpgrading(true);
    startNextUpgrade();
    return result(true);
}

QVariantMap UpdateCenter::upgradeAll() {
    if (m_checking)
        return result(false, QStringLiteral("检查进行中，请稍后"));
    if (m_upgrading)
        return result(false, QStringLiteral("有升级正在进行"));
    QStringList ids;
    // 先 dsh 后插件
    for (const Item &it : m_items) {
        if (it.kind != Kind::Plugin && hasUpdate(it))
            ids.append(it.id);
    }
    for (const Item &it : m_items) {
        if (it.kind == Kind::Plugin && hasUpdate(it))
            ids.append(it.id);
    }
    if (ids.isEmpty())
        return result(false, QStringLiteral("没有可升级的项"));
    m_batch = true;
    m_batchTotal = int(ids.size());
    m_batchOk = 0;
    m_batchFailed.clear();
    m_queue = ids;
    setUpgrading(true);
    startNextUpgrade();
    QVariantMap r = result(true);
    r.insert(QStringLiteral("total"), m_batchTotal);
    return r;
}

void UpdateCenter::setUpgrading(bool on) {
    if (m_upgrading == on)
        return;
    m_upgrading = on;
    emit upgradingChanged();
    emit itemsChanged();
}

void UpdateCenter::startNextUpgrade() {
    while (!m_queue.isEmpty()) {
        const QString id = m_queue.takeFirst();
        const int i = indexOf(id);
        if (i >= 0 && hasUpdate(m_items.at(i))) {
            startUpgrade(id);
            return;
        }
        // 前面的升级已让它不再需要升级（如最新版本已装为独立目录）：跳过，不计入汇总
        if (m_batch)
            --m_batchTotal;
    }
    m_currentId.clear();
    m_upgradeTarget.clear();
    const bool batch = m_batch;
    m_batch = false;
    setUpgrading(false);
    if (batch) {
        QVariantMap summary;
        summary.insert(QStringLiteral("total"), m_batchTotal);
        summary.insert(QStringLiteral("succeeded"), m_batchOk);
        summary.insert(QStringLiteral("failed"), int(m_batchFailed.size()));
        summary.insert(QStringLiteral("failedNames"), m_batchFailed);
        emit upgradeAllFinished(summary);
    }
}

void UpdateCenter::startUpgrade(const QString &id) {
    const int i = indexOf(id);
    Item &it = m_items[i];
    it.upgrading = true;
    it.upgradeError.clear();
    m_currentId = id;
    m_upgradeTarget = it.latest.trimmed();
    emit itemsChanged();

    // 被拒绝时也经事件循环收尾，避免在这里重入 startNextUpgrade
    auto rejectLater = [this](const QString &error) {
        QTimer::singleShot(0, this, [this, error]() { finishUpgrade(false, error, {}); });
    };
    switch (it.kind) {
    case Kind::SystemDsh:
        if (!m_deps.dshUpdate) {
            rejectLater(QStringLiteral("内部错误：DshUpdate 未注入"));
        } else if (m_deps.dshUpdate->busy()) {
            rejectLater(QStringLiteral("dsh 版本检查或更新正在进行，请稍后再试"));
        } else {
            // 结果经 DshUpdate::updateFinished 回来（npm 缺失时同步发出，已排队连接）
            m_deps.dshUpdate->updateDsh();
        }
        break;
    case Kind::StoreDsh: {
        const QVariantMap r = m_deps.versions ? m_deps.versions->install(m_upgradeTarget)
                                              : result(false, QStringLiteral("内部错误：版本仓库未注入"));
        if (!r.value(QStringLiteral("ok")).toBool())
            rejectLater(r.value(QStringLiteral("error")).toString());
        break;
    }
    case Kind::Plugin: {
        // 插件升级到当前渠道最新版：dsh plugin add <name>@<dist-tag>；成功后 PluginManager 自己触发 Restart_Hint
        const QVariantMap r = m_deps.plugins
                                  ? m_deps.plugins->install(it.home, it.profile, it.name, it.channel)
                                  : result(false, QStringLiteral("内部错误：插件管理未注入"));
        if (!r.value(QStringLiteral("ok")).toBool())
            rejectLater(r.value(QStringLiteral("error")).toString());
        break;
    }
    }
}

void UpdateCenter::finishUpgrade(bool ok, const QString &detail, const QString &newVersion) {
    const QString id = m_currentId;
    const int i = indexOf(id);
    QString label;
    if (i >= 0) {
        Item &it = m_items[i];
        label = it.kind == Kind::Plugin ? QStringLiteral("%1（%2）").arg(it.label, it.groupLabel) : it.label;
        it.upgrading = false;
        if (ok) {
            it.upgradeError.clear();
            // 多版本条目本身不变（新版本以新条目出现），其余条目更新当前版本
            if (it.kind != Kind::StoreDsh && !newVersion.isEmpty())
                it.current = newVersion;
        } else {
            // 失败：保持当前版本不变，在该项旁显示原因
            it.upgradeError = detail.isEmpty() ? QStringLiteral("升级失败") : detail;
        }
    }
    if (m_batch) {
        if (ok)
            ++m_batchOk;
        else
            m_batchFailed.append(label);
    }
    m_currentId.clear();
    emit itemsChanged();
    emit upgradeFinished(id, label, ok, detail, newVersion);
    QTimer::singleShot(0, this, [this]() { startNextUpgrade(); });
}
