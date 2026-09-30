#pragma once

// settings.json 的纯数据结构与 JSON 互转（schema 1）。
// - 缺少的字段取默认值；字段类型不对时取默认值，并把 "settings.json.字段名"（嵌套字段逐级串联）追加到 typeErrors
// - 无法识别的字段（顶层与各嵌套对象内）放进对应的 extra，toJson 时原样写回
// - schemaVersion 由 DataFile 管理，这里既不读也不写
// Settings（QObject）后续持有/转换 SettingsData；本文件只依赖 Qt6::Core。

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct AutoRestartSettings {
    int baseSec = 2;
    int maxSec = 60;
    int maxAttempts = 5;
    QJsonObject extra;

    bool operator==(const AutoRestartSettings &) const = default;
};

struct NotifySettings {
    bool crash = true;
    bool ready = true;
    bool update = true;
    QJsonObject extra;

    bool operator==(const NotifySettings &) const = default;
};

struct ProxySettings {
    QString mode = QStringLiteral("none");   // none | system | manual
    QString scheme = QStringLiteral("http");
    QString host;
    int port = 0;
    QString user;
    QString passwordDpapi;                   // DPAPI 加密后的密码（base64）
    QJsonObject extra;

    bool operator==(const ProxySettings &) const = default;
};

struct NpmRegistrySettings {
    QString kind = QStringLiteral("official"); // official | preset | custom
    QString url;
    QJsonObject extra;

    bool operator==(const NpmRegistrySettings &) const = default;
};

struct ModelPrice {
    double input = 0;
    double output = 0;
    double cache = 0;
    QJsonObject extra;

    bool operator==(const ModelPrice &) const = default;
};

struct SettingsData {
    // ---- 现有字段 ----
    QString dshExecutable = QStringLiteral("dsh");
    QString nodeExecutable;
    bool startMinimized = false;
    bool openUiOnStart = true;

    // ---- 新增字段 ----
    QString catalogUrl = QStringLiteral("https://dsh-plug.in/api/plugins.json");
    bool preflightEnabled = true;
    AutoRestartSettings autoRestart;
    int staggerSec = 3;
    int updateCheckHours = 24;
    bool trayInstanceIcons = false;
    bool closeToTray = true;
    NotifySettings notify;
    ProxySettings proxy;
    NpmRegistrySettings npmRegistry;
    bool mirrorFallback = false;
    bool instanceInheritProxy = false;
    QMap<QString, ModelPrice> modelPrices;     // 模型名 → 单价
    QMap<QString, QString> pluginChannels;     // "<home>|<profile>|<name>" → 渠道（如 stable）

    QJsonObject extra; // 无法识别的顶层字段

    bool operator==(const SettingsData &) const = default;
};

namespace SettingsCodec {

SettingsData fromJson(const QJsonObject &obj, QStringList *typeErrors,
                      const QString &prefix = QStringLiteral("settings.json"));

QJsonObject toJson(const SettingsData &data);

} // namespace SettingsCodec
