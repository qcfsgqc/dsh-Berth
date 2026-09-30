#include "core/SettingsCodec.h"

#include "core/DataFile.h"

#include <QJsonValue>

namespace SettingsCodec {

namespace {

const QString kDshExecutable = QStringLiteral("dshExecutable");
const QString kNodeExecutable = QStringLiteral("nodeExecutable");
const QString kStartMinimized = QStringLiteral("startMinimized");
const QString kOpenUiOnStart = QStringLiteral("openUiOnStart");
const QString kCatalogUrl = QStringLiteral("catalogUrl");
const QString kPreflightEnabled = QStringLiteral("preflightEnabled");
const QString kAutoRestart = QStringLiteral("autoRestart");
const QString kStaggerSec = QStringLiteral("staggerSec");
const QString kUpdateCheckHours = QStringLiteral("updateCheckHours");
const QString kTrayInstanceIcons = QStringLiteral("trayInstanceIcons");
const QString kCloseToTray = QStringLiteral("closeToTray");
const QString kNotify = QStringLiteral("notify");
const QString kProxy = QStringLiteral("proxy");
const QString kNpmRegistry = QStringLiteral("npmRegistry");
const QString kMirrorFallback = QStringLiteral("mirrorFallback");
const QString kInstanceInheritProxy = QStringLiteral("instanceInheritProxy");
const QString kModelPrices = QStringLiteral("modelPrices");
const QString kPluginChannels = QStringLiteral("pluginChannels");

const QString kBaseSec = QStringLiteral("baseSec");
const QString kMaxSec = QStringLiteral("maxSec");
const QString kMaxAttempts = QStringLiteral("maxAttempts");

const QString kCrash = QStringLiteral("crash");
const QString kReady = QStringLiteral("ready");
const QString kUpdate = QStringLiteral("update");

const QString kMode = QStringLiteral("mode");
const QString kScheme = QStringLiteral("scheme");
const QString kHost = QStringLiteral("host");
const QString kPort = QStringLiteral("port");
const QString kUser = QStringLiteral("user");
const QString kPasswordDpapi = QStringLiteral("passwordDpapi");

const QString kKind = QStringLiteral("kind");
const QString kUrl = QStringLiteral("url");

const QString kInput = QStringLiteral("input");
const QString kOutput = QStringLiteral("output");
const QString kCache = QStringLiteral("cache");

void addError(QStringList *errors, const QString &path) {
    if (errors && !errors->contains(path))
        errors->append(path);
}

QJsonObject unknownFields(const QJsonObject &obj, const QStringList &known) {
    QJsonObject out;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!known.contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

// 取值必须属于 allowed；否则取默认值并记录错误
QString enumString(const DataFile::FieldReader &r, const QString &key, const QString &def,
                   const QStringList &allowed, QStringList *errors) {
    const QString v = r.string(key, def);
    if (allowed.contains(v))
        return v;
    addError(errors, DataFile::fieldPath(r.prefix(), key));
    return def;
}

AutoRestartSettings readAutoRestart(const DataFile::FieldReader &parent) {
    static const QStringList known = {kBaseSec, kMaxSec, kMaxAttempts};
    const DataFile::FieldReader r = parent.child(kAutoRestart);
    AutoRestartSettings out;
    out.baseSec = r.integer(kBaseSec, out.baseSec);
    out.maxSec = r.integer(kMaxSec, out.maxSec);
    out.maxAttempts = r.integer(kMaxAttempts, out.maxAttempts);
    out.extra = unknownFields(r.raw(), known);
    return out;
}

NotifySettings readNotify(const DataFile::FieldReader &parent) {
    static const QStringList known = {kCrash, kReady, kUpdate};
    const DataFile::FieldReader r = parent.child(kNotify);
    NotifySettings out;
    out.crash = r.boolean(kCrash, out.crash);
    out.ready = r.boolean(kReady, out.ready);
    out.update = r.boolean(kUpdate, out.update);
    out.extra = unknownFields(r.raw(), known);
    return out;
}

ProxySettings readProxy(const DataFile::FieldReader &parent, QStringList *errors) {
    static const QStringList known = {kMode, kScheme, kHost, kPort, kUser, kPasswordDpapi};
    static const QStringList modes = {QStringLiteral("none"), QStringLiteral("system"), QStringLiteral("manual")};
    const DataFile::FieldReader r = parent.child(kProxy);
    ProxySettings out;
    out.mode = enumString(r, kMode, out.mode, modes, errors);
    out.scheme = r.string(kScheme, out.scheme);
    out.host = r.string(kHost);
    out.port = r.integer(kPort, out.port);
    out.user = r.string(kUser);
    out.passwordDpapi = r.string(kPasswordDpapi);
    out.extra = unknownFields(r.raw(), known);
    return out;
}

NpmRegistrySettings readNpmRegistry(const DataFile::FieldReader &parent, QStringList *errors) {
    static const QStringList known = {kKind, kUrl};
    static const QStringList kinds = {QStringLiteral("official"), QStringLiteral("preset"), QStringLiteral("custom")};
    const DataFile::FieldReader r = parent.child(kNpmRegistry);
    NpmRegistrySettings out;
    out.kind = enumString(r, kKind, out.kind, kinds, errors);
    out.url = r.string(kUrl);
    out.extra = unknownFields(r.raw(), known);
    return out;
}

// 每个条目必须是对象；不是对象的条目记错误并跳过
QMap<QString, ModelPrice> readModelPrices(const DataFile::FieldReader &parent, QStringList *errors) {
    static const QStringList known = {kInput, kOutput, kCache};
    const DataFile::FieldReader map = parent.child(kModelPrices);
    QMap<QString, ModelPrice> out;
    const QJsonObject &raw = map.raw();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        const QString entryPrefix = DataFile::fieldPath(map.prefix(), it.key());
        if (!it.value().isObject()) {
            addError(errors, entryPrefix);
            continue;
        }
        const QJsonObject obj = it.value().toObject();
        const DataFile::FieldReader r(obj, entryPrefix, errors);
        ModelPrice price;
        price.input = r.number(kInput, price.input);
        price.output = r.number(kOutput, price.output);
        price.cache = r.number(kCache, price.cache);
        price.extra = unknownFields(obj, known);
        out.insert(it.key(), price);
    }
    return out;
}

// 每个值必须是字符串；不是字符串的条目记错误并跳过
QMap<QString, QString> readPluginChannels(const DataFile::FieldReader &parent, QStringList *errors) {
    const DataFile::FieldReader map = parent.child(kPluginChannels);
    QMap<QString, QString> out;
    const QJsonObject &raw = map.raw();
    for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
        if (!it.value().isString()) {
            addError(errors, DataFile::fieldPath(map.prefix(), it.key()));
            continue;
        }
        out.insert(it.key(), it.value().toString());
    }
    return out;
}

} // namespace

SettingsData fromJson(const QJsonObject &obj, QStringList *typeErrors, const QString &prefix) {
    static const QStringList known = {
        QString::fromLatin1(DataFile::kSchemaKey),
        kDshExecutable, kNodeExecutable, kStartMinimized, kOpenUiOnStart,
        kCatalogUrl, kPreflightEnabled, kAutoRestart, kStaggerSec, kUpdateCheckHours,
        kTrayInstanceIcons, kCloseToTray, kNotify, kProxy, kNpmRegistry,
        kMirrorFallback, kInstanceInheritProxy, kModelPrices, kPluginChannels,
    };
    const DataFile::FieldReader r(obj, prefix, typeErrors);
    SettingsData d;
    d.dshExecutable = r.string(kDshExecutable, d.dshExecutable);
    d.nodeExecutable = r.string(kNodeExecutable, d.nodeExecutable);
    d.startMinimized = r.boolean(kStartMinimized, d.startMinimized);
    d.openUiOnStart = r.boolean(kOpenUiOnStart, d.openUiOnStart);

    d.catalogUrl = r.string(kCatalogUrl, d.catalogUrl);
    d.preflightEnabled = r.boolean(kPreflightEnabled, d.preflightEnabled);
    d.autoRestart = readAutoRestart(r);
    d.staggerSec = r.integer(kStaggerSec, d.staggerSec);
    d.updateCheckHours = r.integer(kUpdateCheckHours, d.updateCheckHours);
    d.trayInstanceIcons = r.boolean(kTrayInstanceIcons, d.trayInstanceIcons);
    d.closeToTray = r.boolean(kCloseToTray, d.closeToTray);
    d.notify = readNotify(r);
    d.proxy = readProxy(r, typeErrors);
    d.npmRegistry = readNpmRegistry(r, typeErrors);
    d.mirrorFallback = r.boolean(kMirrorFallback, d.mirrorFallback);
    d.instanceInheritProxy = r.boolean(kInstanceInheritProxy, d.instanceInheritProxy);
    d.modelPrices = readModelPrices(r, typeErrors);
    d.pluginChannels = readPluginChannels(r, typeErrors);

    d.extra = unknownFields(obj, known);
    return d;
}

QJsonObject toJson(const SettingsData &d) {
    QJsonObject obj = d.extra;
    obj.insert(kDshExecutable, d.dshExecutable);
    obj.insert(kNodeExecutable, d.nodeExecutable);
    obj.insert(kStartMinimized, d.startMinimized);
    obj.insert(kOpenUiOnStart, d.openUiOnStart);

    obj.insert(kCatalogUrl, d.catalogUrl);
    obj.insert(kPreflightEnabled, d.preflightEnabled);

    QJsonObject autoRestart = d.autoRestart.extra;
    autoRestart.insert(kBaseSec, d.autoRestart.baseSec);
    autoRestart.insert(kMaxSec, d.autoRestart.maxSec);
    autoRestart.insert(kMaxAttempts, d.autoRestart.maxAttempts);
    obj.insert(kAutoRestart, autoRestart);

    obj.insert(kStaggerSec, d.staggerSec);
    obj.insert(kUpdateCheckHours, d.updateCheckHours);
    obj.insert(kTrayInstanceIcons, d.trayInstanceIcons);
    obj.insert(kCloseToTray, d.closeToTray);

    QJsonObject notify = d.notify.extra;
    notify.insert(kCrash, d.notify.crash);
    notify.insert(kReady, d.notify.ready);
    notify.insert(kUpdate, d.notify.update);
    obj.insert(kNotify, notify);

    QJsonObject proxy = d.proxy.extra;
    proxy.insert(kMode, d.proxy.mode);
    proxy.insert(kScheme, d.proxy.scheme);
    proxy.insert(kHost, d.proxy.host);
    proxy.insert(kPort, d.proxy.port);
    proxy.insert(kUser, d.proxy.user);
    proxy.insert(kPasswordDpapi, d.proxy.passwordDpapi);
    obj.insert(kProxy, proxy);

    QJsonObject npm = d.npmRegistry.extra;
    npm.insert(kKind, d.npmRegistry.kind);
    npm.insert(kUrl, d.npmRegistry.url);
    obj.insert(kNpmRegistry, npm);

    obj.insert(kMirrorFallback, d.mirrorFallback);
    obj.insert(kInstanceInheritProxy, d.instanceInheritProxy);

    QJsonObject prices;
    for (auto it = d.modelPrices.constBegin(); it != d.modelPrices.constEnd(); ++it) {
        QJsonObject p = it.value().extra;
        p.insert(kInput, it.value().input);
        p.insert(kOutput, it.value().output);
        p.insert(kCache, it.value().cache);
        prices.insert(it.key(), p);
    }
    obj.insert(kModelPrices, prices);

    QJsonObject channels;
    for (auto it = d.pluginChannels.constBegin(); it != d.pluginChannels.constEnd(); ++it)
        channels.insert(it.key(), it.value());
    obj.insert(kPluginChannels, channels);
    return obj;
}

} // namespace SettingsCodec
