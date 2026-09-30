#include "Settings.h"
#include "DataStore.h"
#include "core/PackageSpec.h"
#include "core/UsageAgg.h"

#include <QDir>
#include <QStandardPaths>

namespace {
const QString kSettingsFile = QStringLiteral("settings.json");
constexpr int kSettingsSchema = 1;

int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

Settings::Settings(DataStore *store, QObject *parent) : QObject(parent), m_store(store) {
    QDir().mkpath(dataDir());
    load();
}

QString Settings::dshExecutable() const { return m_data.dshExecutable; }

void Settings::setDshExecutable(const QString &value) {
    if (m_data.dshExecutable == value)
        return;
    m_data.dshExecutable = value;
    emit dshExecutableChanged();
}

QString Settings::resolvedDshExecutable() const {
    const QString exe = m_data.dshExecutable.trimmed().isEmpty() ? QStringLiteral("dsh") : m_data.dshExecutable.trimmed();
    // 用户填的是路径就原样使用
    if (exe.contains(QLatin1Char('/')) || exe.contains(QLatin1Char('\\')))
        return exe;
    // 裸命令名：npm 全局安装只有 dsh.cmd，CreateProcess 不会自动补 .cmd，需要先在 PATH 里找
    const QString found = QStandardPaths::findExecutable(exe);
    return found.isEmpty() ? exe : found;
}

QString Settings::nodeExecutable() const { return m_data.nodeExecutable; }

void Settings::setNodeExecutable(const QString &value) {
    if (m_data.nodeExecutable == value)
        return;
    m_data.nodeExecutable = value;
    emit nodeExecutableChanged();
}

QString Settings::dataDir() const {
    return m_store->dir();
}

bool Settings::startMinimized() const { return m_data.startMinimized; }

void Settings::setStartMinimized(bool value) {
    if (m_data.startMinimized == value)
        return;
    m_data.startMinimized = value;
    emit startMinimizedChanged();
}

bool Settings::openUiOnStart() const { return m_data.openUiOnStart; }

void Settings::setOpenUiOnStart(bool value) {
    if (m_data.openUiOnStart == value)
        return;
    m_data.openUiOnStart = value;
    emit openUiOnStartChanged();
}

int Settings::autoRestartBaseSec() const { return m_data.autoRestart.baseSec; }

void Settings::setAutoRestartBaseSec(int value) {
    const int base = clampInt(value, 1, 60);
    AutoRestartSettings &ar = m_data.autoRestart;
    const int max = clampInt(ar.maxSec, base, 600);
    if (ar.baseSec == base && ar.maxSec == max)
        return;
    ar.baseSec = base;
    ar.maxSec = max;
    emit autoRestartChanged();
}

int Settings::autoRestartMaxSec() const { return m_data.autoRestart.maxSec; }

void Settings::setAutoRestartMaxSec(int value) {
    const int max = clampInt(value, clampInt(m_data.autoRestart.baseSec, 1, 60), 600);
    if (m_data.autoRestart.maxSec == max)
        return;
    m_data.autoRestart.maxSec = max;
    emit autoRestartChanged();
}

int Settings::autoRestartMaxAttempts() const { return m_data.autoRestart.maxAttempts; }

void Settings::setAutoRestartMaxAttempts(int value) {
    const int attempts = clampInt(value, 1, 20);
    if (m_data.autoRestart.maxAttempts == attempts)
        return;
    m_data.autoRestart.maxAttempts = attempts;
    emit autoRestartChanged();
}

int Settings::staggerSec() const { return clampInt(m_data.staggerSec, 0, 60); }

void Settings::setStaggerSec(int value) {
    const int sec = clampInt(value, 0, 60);
    if (m_data.staggerSec == sec)
        return;
    m_data.staggerSec = sec;
    emit staggerSecChanged();
}

int Settings::updateCheckHours() const {
    return m_data.updateCheckHours <= 0 ? 0 : clampInt(m_data.updateCheckHours, 1, 168);
}

void Settings::setUpdateCheckHours(int value) {
    const int hours = value <= 0 ? 0 : clampInt(value, 1, 168);
    if (m_data.updateCheckHours == hours)
        return;
    m_data.updateCheckHours = hours;
    emit updateCheckHoursChanged();
}

bool Settings::trayInstanceIcons() const { return m_data.trayInstanceIcons; }

void Settings::setTrayInstanceIcons(bool value) {
    if (m_data.trayInstanceIcons == value)
        return;
    m_data.trayInstanceIcons = value;
    emit trayInstanceIconsChanged();
}

bool Settings::closeToTray() const { return m_data.closeToTray; }

void Settings::setCloseToTray(bool value) {
    if (m_data.closeToTray == value)
        return;
    m_data.closeToTray = value;
    emit closeToTrayChanged();
}

bool Settings::notifyCrash() const { return m_data.notify.crash; }

void Settings::setNotifyCrash(bool value) {
    if (m_data.notify.crash == value)
        return;
    m_data.notify.crash = value;
    emit notifyChanged();
}

bool Settings::notifyReady() const { return m_data.notify.ready; }

void Settings::setNotifyReady(bool value) {
    if (m_data.notify.ready == value)
        return;
    m_data.notify.ready = value;
    emit notifyChanged();
}

bool Settings::notifyUpdate() const { return m_data.notify.update; }

void Settings::setNotifyUpdate(bool value) {
    if (m_data.notify.update == value)
        return;
    m_data.notify.update = value;
    emit notifyChanged();
}

bool Settings::readOnly() const { return m_store->readOnly(kSettingsFile); }

QString Settings::catalogUrl() const { return m_data.catalogUrl; }

QString Settings::setCatalogUrl(const QString &value) {
    const QString url = value.trimmed();
    if (const auto error = PackageSpec::validateCatalogUrl(url))
        return *error;
    if (m_data.catalogUrl != url) {
        m_data.catalogUrl = url;
        emit catalogUrlChanged();
    }
    return {};
}

QString Settings::pluginChannel(const QString &key) const { return m_data.pluginChannels.value(key); }

void Settings::setPluginChannel(const QString &key, const QString &channel) {
    if (m_data.pluginChannels.value(key) == channel && m_data.pluginChannels.contains(key))
        return;
    m_data.pluginChannels.insert(key, channel);
    save();
}

void Settings::load() {
    const QJsonObject root = m_store->load(kSettingsFile, kSettingsSchema);
    QStringList typeErrors;
    m_data = SettingsCodec::fromJson(root, &typeErrors);
    m_store->reportTypeErrors(typeErrors);
    emit dshExecutableChanged();
    emit nodeExecutableChanged();
    emit startMinimizedChanged();
    emit openUiOnStartChanged();
    emit autoRestartChanged();
    emit staggerSecChanged();
    emit updateCheckHoursChanged();
    emit trayInstanceIconsChanged();
    emit closeToTrayChanged();
    emit notifyChanged();
    emit readOnlyChanged();
    emit catalogUrlChanged();
    emit modelPricesChanged();
    emit proxyChanged();
}

void Settings::setProxyData(const ProxySettings &proxy, const NpmRegistrySettings &registry,
                            bool mirrorFallback, bool instanceInheritProxy) {
    m_data.proxy = proxy;
    m_data.npmRegistry = registry;
    m_data.mirrorFallback = mirrorFallback;
    m_data.instanceInheritProxy = instanceInheritProxy;
    save();
    emit proxyChanged();
}

QVariantList Settings::modelPrices() const {
    QVariantList out;
    for (auto it = m_data.modelPrices.constBegin(); it != m_data.modelPrices.constEnd(); ++it) {
        out.append(QVariantMap{
            {QStringLiteral("model"), it.key()},
            {QStringLiteral("input"), it.value().input},
            {QStringLiteral("output"), it.value().output},
            {QStringLiteral("cache"), it.value().cache},
        });
    }
    return out;
}

QString Settings::setModelPrice(const QString &model, const QString &input, const QString &output,
                                const QString &cache) {
    const QString name = model.trimmed();
    if (name.isEmpty())
        return tr("模型名不能为空");
    const QString *fields[] = {&input, &output, &cache};
    const QString labels[] = {tr("输入单价"), tr("输出单价"), tr("缓存单价")};
    for (int i = 0; i < 3; ++i) {
        if (const auto error = UsageAgg::validatePrice(*fields[i]))
            return labels[i] + QStringLiteral("：") + *error;
    }
    ModelPrice price = m_data.modelPrices.value(name); // 保留无法识别的字段
    price.input = input.trimmed().toDouble();
    price.output = output.trimmed().toDouble();
    price.cache = cache.trimmed().toDouble();
    if (m_data.modelPrices.contains(name) && m_data.modelPrices.value(name) == price)
        return {};
    m_data.modelPrices.insert(name, price);
    save();
    emit modelPricesChanged();
    return {};
}

void Settings::removeModelPrice(const QString &model) {
    if (m_data.modelPrices.remove(model.trimmed()) == 0)
        return;
    save();
    emit modelPricesChanged();
}

void Settings::save() {
    // 只读/损坏时 DataStore 拒绝写回并发出 saveFailed
    m_store->save(kSettingsFile, SettingsCodec::toJson(m_data));
}
