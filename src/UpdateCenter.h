#pragma once

// dsh 与插件更新中心（需求 14）。
// - dsh：System_Dsh 一项 + Dsh_Version_Store 中每个版本一项；最新版本取 npm registry 上 @deepseek-ai/dsh 的 latest
// - 插件：按 (DSH_HOME, profile) 分组，每个已安装的非核心插件一项；最新版本取其 Release_Channel
//   （Settings::pluginChannel，未记录视为 stable）对应 dist-tag
// - registry 地址跟随 ProxyManager::registry()（镜像设置）；请求走共享 QNetworkAccessManager（已应用代理），
//   单次请求 30 秒超时；同一包名在一次全部检查中只请求一次
// - 版本比较用 SemVer；任一侧不是合法 SemVer 时显示"版本无法比较"，不标记有更新
// - 升级：System_Dsh 走 DshUpdate；多版本走 DshVersionStore::install 新目录；插件走 PluginManager::install
//   （成功后由 PluginManager 自己发 restartHintRequested）。同一时刻只跑一个升级；一键升级先 dsh 后插件

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class DshUpdate;
class DshVersionStore;
class PluginManager;
class ProcessRunner;
class ProcessTask;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class Settings;

class UpdateCenter : public QObject {
    Q_OBJECT
    // 全部条目（先 dsh 后插件），元素见 itemMap()：
    // {id, kind: systemDsh|storeDsh|plugin, section: dsh|plugin, group, groupLabel, name, label,
    //  home, profile, channel, current, latest, lastChecked(ISO，未检查为空), lastCheckedText,
    //  state: unchecked|checking|upgrading|upToDate|hasUpdate|incomparable|latestInstalled|failed,
    //  stateText, hasUpdate, busy, error}
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    // 全部检查进行中（此时 checkAll 不重复发起）
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    // 有升级（单项或一键）正在进行
    Q_PROPERTY(bool upgrading READ upgrading NOTIFY upgradingChanged)
    // 标记"有更新"的条目数（SummaryPanel 用）
    Q_PROPERTY(int updateCount READ updateCount NOTIFY itemsChanged)
    // 最近一次全部检查完成时间（ISO / 显示文本；从未检查为空）
    Q_PROPERTY(QString lastCheckAt READ lastCheckAt NOTIFY checkingChanged)
    Q_PROPERTY(QString lastCheckText READ lastCheckText NOTIFY checkingChanged)
    // "一键升级"可用：有更新数 > 0 且没有检查或升级在进行
    Q_PROPERTY(bool canUpgradeAll READ canUpgradeAll NOTIFY itemsChanged)

public:
    struct Deps {
        DshUpdate *dshUpdate = nullptr;
        DshVersionStore *versions = nullptr;
        PluginManager *plugins = nullptr;
        Settings *settings = nullptr;
        std::function<QString()> registry;                      // 当前实际使用的 registry
        std::function<QString(const QString &home)> resolveHome; // 空串 → 默认 DSH_HOME
    };

    UpdateCenter(ProcessRunner *runner, QNetworkAccessManager *network, QObject *parent = nullptr);

    // AppController 构造函数体内注入；注入后建立条目并按 updateCheckHours 启动定时检查
    void setDeps(Deps deps);

    QVariantList items() const;
    bool checking() const { return m_checking; }
    bool upgrading() const { return m_upgrading; }
    int updateCount() const;
    QString lastCheckAt() const;
    QString lastCheckText() const;
    bool canUpgradeAll() const;

    // 全部检查（异步）。检查或升级进行中时不发起，返回 false
    Q_INVOKABLE bool checkAll();
    // 单项升级（异步）：返回 {ok, error}；ok=false 表示被拒绝（不存在、无更新、检查/升级进行中）。
    // 结束时发 upgradeFinished
    Q_INVOKABLE QVariantMap upgrade(const QString &itemId);
    // 一键升级：只处理 hasUpdate 的条目，先 dsh 后插件逐项执行；返回 {ok, error, total}。
    // 每项结束发 upgradeFinished，全部结束发 upgradeAllFinished
    Q_INVOKABLE QVariantMap upgradeAll();
    // 重新读取条目来源（版本仓库、插件列表），保留已有检查结果
    Q_INVOKABLE void refresh();

signals:
    void itemsChanged();
    void checkingChanged();
    void upgradingChanged();
    // 一次全部检查结束：failed 为检查失败的条目数
    void checkFinished(int updates, int failed);
    // 单项升级结束（32.2 据此调用 Notifier::notifyUpdate(label, ok, detail)）：
    // ok=true 时 newVersion 为新版本、detail 为成功说明；ok=false 时 detail 为失败原因
    void upgradeFinished(const QString &itemId, const QString &label, bool ok, const QString &detail,
                         const QString &newVersion);
    // 一键升级结束：summary = {total, succeeded, failed, failedNames: [label]}
    void upgradeAllFinished(const QVariantMap &summary);

private:
    enum class Kind { SystemDsh, StoreDsh, Plugin };
    struct Item {
        QString id;
        Kind kind = Kind::Plugin;
        QString name;       // npm 包名
        QString label;      // 显示名
        QString home;
        QString profile;
        QString group;      // dsh 为 "dsh"；插件为 PluginManager::keyOf(home, profile)
        QString groupLabel;
        QString channel;    // 插件渠道（stable/beta/alpha）
        QString tag;        // dist-tag
        QString current;
        QString latest;
        QString checkError;
        QString upgradeError;
        QDateTime lastChecked;
        int pendingParts = 0; // >0 表示检查中（System_Dsh 需等 --version 与 registry 两部分）
        bool upgrading = false;
    };
    enum class State { Unchecked, Checking, Upgrading, UpToDate, HasUpdate, Incomparable, LatestInstalled, Failed };

    void rebuild();
    QString installedPluginVersion(const QString &home, const QString &profile, const QString &name,
                                   const QString &declared) const;
    bool hasUpdate(const Item &item) const;
    State stateOf(const Item &item) const;
    QVariantMap itemMap(const Item &item) const;
    int indexOf(const QString &id) const;

    void fetchPackage(const QString &name);
    void onPackageReply(QNetworkReply *reply, const QString &name);
    void applyPackage(const QString &name, bool ok, const QHash<QString, QString> &tags, const QString &error);
    void checkSystemDsh();
    void partDone(Item &item);
    void finishCheckIfIdle();

    void applySchedule();
    void startNextUpgrade();
    void startUpgrade(const QString &id);
    void finishUpgrade(bool ok, const QString &detail, const QString &newVersion);
    void setUpgrading(bool on);

    ProcessRunner *m_runner;
    QNetworkAccessManager *m_network;
    Deps m_deps;
    QList<Item> m_items;
    QTimer *m_timer = nullptr;

    bool m_checking = false;
    int m_pending = 0;               // 未完成的检查请求数（registry 请求 + dsh --version）
    QHash<QNetworkReply *, bool> m_timedOut;
    bool m_dirty = false;            // 检查期间来源有变，结束后重建
    QDateTime m_lastCheck;

    bool m_upgrading = false;
    QStringList m_queue;             // 待升级条目 id
    QString m_currentId;             // 正在升级的条目
    QString m_upgradeTarget;         // 正在升级到的版本
    bool m_batch = false;            // 当前是一键升级
    int m_batchTotal = 0;
    int m_batchOk = 0;
    QStringList m_batchFailed;
};
