#include "AppController.h"
#include "PluginOps.h"
#include "ProfileOps.h"
#include "core/BatchPlan.h"
#include "core/DataFile.h"
#include "core/EnvTable.h"
#include "core/InstanceCodec.h"
#include "core/InstanceMeta.h"
#include "core/LaunchEnv.h"
#include "core/PortPick.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

namespace {
const QString kInstancesFile = QStringLiteral("instances.json");
const QString kInstancesKey = QStringLiteral("instances");
constexpr int kInstancesSchema = 1;

// 泊位的敏感值（标记为 secret 的环境变量值），写日志标记前用于脱敏
QStringList secretsOf(const Instance &item) {
    QStringList secrets;
    for (const InstanceEnvVar &var : item.env) {
        if (var.secret && !var.value.isEmpty())
            secrets.append(var.value);
    }
    return secrets;
}

// QML 传入的环境变量表 [{key, value, secret}] → InstanceEnvVar（保持行顺序）。
// 同一行号且键未变的行沿用原行的未知字段 extra
QList<InstanceEnvVar> envFromVariant(const QVariantList &rows, const QList<InstanceEnvVar> &old) {
    QList<InstanceEnvVar> out;
    out.reserve(rows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QVariantMap m = rows.at(i).toMap();
        InstanceEnvVar var;
        var.key = m.value(QStringLiteral("key")).toString();
        var.value = m.value(QStringLiteral("value")).toString();
        var.secret = m.value(QStringLiteral("secret")).toBool();
        if (i < old.size() && old.at(i).key == var.key)
            var.extra = old.at(i).extra;
        out.append(var);
    }
    return out;
}

// 行号（0 起）→"第 1、3 行"
QString rowsText(const QList<int> &rows) {
    QStringList parts;
    for (int r : rows)
        parts.append(QString::number(r + 1));
    return QStringLiteral("第 %1 行").arg(parts.join(QStringLiteral("、")));
}

QVariantList rowsVariant(const QList<int> &rows) {
    QVariantList out;
    for (int r : rows)
        out.append(r);
    return out;
}

// 往泊位日志追加一行 Berth 标记（与 Supervisor 共用 LogService::appendMarker，写出前脱敏）
void appendLogMarker(const Instance &item, const QString &message) {
    LogService::appendMarker(item.logPath, message, secretsOf(item));
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent), m_store(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), this),
      m_instances(this), m_settings(&m_store, this), m_supervisor(this),
      m_metrics(&m_supervisor, m_store.dir(), this), m_portChecker(this),
      m_profileTree(this), m_dshUpdate(this), m_autoRestarter(this, &m_supervisor, this),
      m_batch(makeBatchOps(), this), m_proxy(&m_settings, &m_network, this),
      m_plugins(&m_processRunner, this),
      m_restartHint(makeRestartHintOps(), this), m_quarantine(&m_store, this),
      m_preflight(&m_processRunner, &m_quarantine, this), m_versions(&m_processRunner, this),
      m_bundles(&m_processRunner, this), m_env(&m_processRunner, this),
      m_stats(&m_processRunner, this), m_updates(&m_processRunner, &m_network, this),
      m_processRunner(this) {
    m_plugins.setOps({[this](const QString &home) { return resolveHome(home); },
                      [this]() { return m_instances.items(); },
                      [this]() { return m_knownProfiles; },
                      [this]() { return m_settings.resolvedDshExecutable(); },
                      [this]() { return m_pluginBusy || m_profileBusy; },
                      [this]() { return m_settings.catalogUrl(); },
                      [this]() { return m_settings.dataDir(); },
                      [this](const QString &key, const QString &channel) {
                          m_settings.setPluginChannel(key, channel);
                      },
                      [this](const QString &home, const QString &profile) {
                          QHash<QString, QString> out;
                          const QString h = Quarantine::normalizeHome(resolveHome(home));
                          for (const QuarantineEntry &e : m_quarantine.entriesFor(h, profile))
                              out.insert(e.name, Preflight::reasonText(e.reason));
                          return out;
                      }});
    m_plugins.setNetwork(&m_network);
    // 所有后台 npm/pnpm/git 子进程注入代理与 registry 环境变量（每次 run 时读取，保存后对新子进程生效）
    m_processRunner.setEnvHook([this]() { return m_proxy.childEnv(); });
    // 插件变更成功且有启动中/运行中的泊位 → 显示（或合并进）重启提示
    connect(&m_plugins, &PluginManager::restartHintRequested, &m_restartHint, &RestartHintController::raise);
    // 扩展页：MCP 启用状态变更成功后同样走重启提示
    m_extensions.setOps({[this](const QString &home) { return resolveHome(home); },
                         [this]() { return m_instances.items(); }});
    connect(&m_extensions, &ExtensionManager::restartHintRequested, &m_restartHint,
            &RestartHintController::raise);
    connect(&m_store, &DataStore::loadWarnings, this,
            [this](const QStringList &messages) { emit notice(messages.join(QLatin1Char('\n'))); });
    connect(&m_store, &DataStore::saveFailed, this, &AppController::notice);
    connect(&m_supervisor, &Supervisor::statusChanged, this, &AppController::applyStatus);
    connect(&m_supervisor, &Supervisor::crashed, this, &AppController::instanceCrashed);
    // 外部进程监视：进入"运行中（外部）"时开始，离开时解除
    connect(&m_supervisor, &Supervisor::statusChanged, this,
            [this](const QString &id, const QString &status, qint64 pid, const QString &) {
                if (status == QLatin1String("external"))
                    m_portChecker.watchExternal(id, pid, m_instances.item(id).port);
                else
                    m_portChecker.unwatch(id);
            });
    // 外部进程退出或端口不再监听：只解除关联→"已停止"，不记为 crash
    connect(&m_portChecker, &PortChecker::externalGone, this, [this](const QString &id) {
        if (!m_supervisor.isExternal(id))
            return;
        const Instance item = m_instances.item(id);
        appendLogMarker(item, QStringLiteral("外部服务已退出或端口不再监听，已解除关联"));
        m_supervisor.stop(id);
        if (!item.id.isEmpty())
            emit notice(item.name + QStringLiteral("：外部服务已退出，已置为已停止"));
    });
    // 冲突端口的 dsh 特征探测结果 → 仍在等待选择、且端口一致的泊位
    connect(&m_portChecker, &PortChecker::dshProbed, this, [this](int port, bool isDsh) {
        for (auto it = m_pendingConflicts.cbegin(); it != m_pendingConflicts.cend(); ++it) {
            if (m_instances.item(it.key()).port == port)
                emit conflictProbed(it.key(), isDsh);
        }
    });
    connect(&m_autoRestarter, &AutoRestarter::countdown, this, &AppController::updateCountdown);
    // 日志页：按 id 取日志路径与 Active_State；泊位离开 Active_State 时丢弃清空偏移
    m_logs.setOps({[this](const QString &id) { return m_instances.item(id).logPath; },
                   [this](const QString &id) { return BatchPlan::isActiveState(m_instances.item(id).status); }});
    connect(this, &AppController::instanceStatusChanged, &m_logs, &LogService::onStatusChanged);
    connect(&m_autoRestarter, &AutoRestarter::gaveUp, this, &AppController::handleRestartGaveUp);
    m_dshUpdate.setSettings(&m_settings);
    // 泊位列表任何变化都全量重建树（泊位数量级小，整树重建开销可忽略）
    connect(&m_instances, &InstanceModel::dataChanged, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::rowsInserted, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::rowsRemoved, this, [this]() { rebuildTree(); });
    connect(&m_instances, &InstanceModel::modelReset, this, [this]() { rebuildTree(); });
    // 泊位增删时刷新批量入口的可用性
    connect(&m_instances, &InstanceModel::rowsInserted, &m_batch, &BatchLauncher::notifyTargetsChanged);
    connect(&m_instances, &InstanceModel::rowsRemoved, &m_batch, &BatchLauncher::notifyTargetsChanged);
    connect(&m_instances, &InstanceModel::modelReset, &m_batch, &BatchLauncher::notifyTargetsChanged);
    // 多版本仓库：泊位绑定保存或删除后重算引用计数；安装结果经 notice 提示
    connect(&m_instances, &InstanceModel::dataChanged, &m_versions, &DshVersionStore::recountReferences);
    connect(&m_instances, &InstanceModel::rowsInserted, &m_versions, &DshVersionStore::recountReferences);
    connect(&m_instances, &InstanceModel::rowsRemoved, &m_versions, &DshVersionStore::recountReferences);
    connect(&m_instances, &InstanceModel::modelReset, &m_versions, &DshVersionStore::recountReferences);
    connect(&m_versions, &DshVersionStore::installFinished, this,
            [this](const QString &, bool, const QString &message) { emit notice(message); });
    load();
    m_versions.setOps({[this]() { return m_settings.dataDir(); },
                       [this]() { return m_instances.items(); },
                       [this]() { return m_proxy.fallbackRegistry(); }});
    // 环境检测与诊断报告：报告里的泊位摘要、敏感 env 值与代理设置都经这里取
    m_env.setOps({[this]() { return m_settings.resolvedDshExecutable(); },
                  [this]() { return m_settings.nodeExecutable(); },
                  [this]() { return m_instances.items(); },
                  [this](const QString &status) { return statusText(status); },
                  [this]() { return m_settings.data().proxy; },
                  [this]() { return m_proxy.secrets(); },
                  [this](const QString &id) {
                      const QVariantMap t = m_metrics.totals(id);
                      return t.value(QStringLiteral("hasLastExit")).toBool()
                                 ? QString::number(t.value(QStringLiteral("lastExitCode")).toInt())
                                 : QString();
                  }});
    // 用量统计：node 路径跟随设置；结果按当前单价转成 QML 结构，单价变化时重算
    m_stats.setHelpersDir(m_settings.dataDir() + QStringLiteral("/helpers"));
    m_stats.setNodeExecutable(m_settings.nodeExecutable());
    // 会话统计：运行中的泊位每 30 秒重新读取；其他状态保留上次结果，只在打开详情时读取
    auto *sessionTimer = new QTimer(this);
    sessionTimer->setInterval(30000);
    connect(sessionTimer, &QTimer::timeout, this, [this]() {
        for (const Instance &item : m_instances.items()) {
            if (item.status == QLatin1String("running") || item.status == QLatin1String("external"))
                refreshSessionStats(item.id);
        }
    });
    sessionTimer->start();
    connect(&m_settings, &Settings::nodeExecutableChanged, this,
            [this]() { m_stats.setNodeExecutable(m_settings.nodeExecutable()); });
    connect(&m_stats, &StatsService::usageFinished, this, [this](const StatsService::UsageResult &r) {
        if (r.requestId != m_usageRequest)
            return;
        m_lastUsage = r;
        m_usageMap = r.toVariantMap(m_settings.data().modelPrices);
        m_usageLoading = false;
        emit usageChanged();
        emit usageLoadingChanged();
    });
    connect(&m_settings, &Settings::modelPricesChanged, this, [this]() {
        if (!m_lastUsage)
            return;
        m_usageMap = m_lastUsage->toVariantMap(m_settings.data().modelPrices);
        emit usageChanged();
    });
    // 整包导入：新泊位在这里补 id / 日志路径 / 状态并写 instances.json（只读模式时拒绝）
    m_bundles.setOps({[this](const QString &home) { return resolveHome(home); },
                      [this]() { return m_instances.items(); },
                      [this]() { return m_settings.resolvedDshExecutable(); },
                      [](int port) { return PortChecker::check(port).listening; },
                      [this](const Instance &src) {
                          if (instancesReadOnly())
                              return QString();
                          Instance item = src;
                          item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                          item.status = QStringLiteral("stopped");
                          item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id
                                         + QStringLiteral(".log");
                          m_instances.upsert(item);
                          save();
                          return item.id;
                      },
                      [this]() { refreshProfiles(); },
                      [this]() { return version(); }});
    m_quarantine.load();
    refreshProfiles();
    // instances.json 只读/损坏时不自动导入，避免一启动就报"未保存"
    if (m_instances.rowCount() == 0 && !m_knownProfiles.isEmpty() && !instancesReadOnly())
        importDetectedProfiles();
    rebuildTree();
    // 更新中心：registry 跟随镜像设置；插件条目按 PluginManager::targets() 分组
    m_updates.setDeps({&m_dshUpdate, &m_versions, &m_plugins, &m_settings,
                       [this]() { return m_proxy.registry(); },
                       [this](const QString &home) { return resolveHome(home); }});
    // 读取阶段的提示等事件循环启动、QML 已连上 notice 后再发
    QTimer::singleShot(0, this, [this]() { m_store.flushWarnings(); });
}

bool AppController::instancesReadOnly() const {
    return m_store.readOnly(kInstancesFile);
}

InstanceModel *AppController::instances() { return &m_instances; }
Settings *AppController::settings() { return &m_settings; }
QString AppController::version() const { return QStringLiteral(BERTH_VERSION); }

QString AppController::createInstance() {
    Instance item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int port = 3080;
    while (m_instances.containsPort(port))
        port += 1;
    item.port = port;
    item.name = QStringLiteral("泊位 %1").arg(m_instances.rowCount() + 1);
    item.profile = QStringLiteral("web");
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

QString AppController::createInstanceFrom(const QString &sourceId) {
    const Instance src = m_instances.item(sourceId);
    Instance item;
    if (!src.id.isEmpty()) {
        item.name = src.name + QStringLiteral(" - 副本");
        item.port = 3080;
        while (m_instances.containsPort(item.port))
            item.port += 1;
        item.profile = src.profile;
        item.dshHome = src.dshHome;
        item.workspace = src.workspace;
        item.autostart = src.autostart;
    } else {
        item.name = QStringLiteral("泊位 %1").arg(m_instances.rowCount() + 1);
        item.port = 3080;
        while (m_instances.containsPort(item.port))
            item.port += 1;
        item.profile = QStringLiteral("web");
    }
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

void AppController::removeInstance(const QString &id) {
    if (m_supervisor.isRunning(id))
        m_supervisor.stop(id);
    m_autoRestarter.forget(id);
    // 自检进行中的泊位：结果回来时不再启动
    m_preflightPending.remove(id);
    m_batch.onRemoved(id);
    m_restartHint.onRemoved(id);
    m_metrics.forget(id);
    m_instances.remove(id);
    save();
}

bool AppController::updateInstance(const QString &id, const QString &name, int port,
                                   const QString &profile, const QString &dshHome,
                                   const QString &workspace, bool autostart, bool autoRestart,
                                   const QVariantList &env, const QStringList &extraArgs,
                                   const QString &dshVersion) {
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return false;
    if (port < 1 || port > 65535) {
        emit notice(QStringLiteral("端口必须在 1–65535"));
        return false;
    }
    // 端口重复时拒绝保存并列出泊位名；不改模型，表单内容保留
    const QStringList dupNames = PortPick::duplicates(m_instances.items(), id, port);
    if (!dupNames.isEmpty()) {
        emit notice(QStringLiteral("端口 %1 重复，已被泊位使用：%2；未保存")
                        .arg(QString::number(port), dupNames.join(QStringLiteral("、"))));
        return false;
    }
    // 环境变量键非法/重复或行数、参数超上限：拒绝保存，表单内容由 QML 保留
    const QList<InstanceEnvVar> envRows = envFromVariant(env, item.env);
    const EnvTable::Validation v = EnvTable::validate(envRows, extraArgs);
    if (!v.ok()) {
        QStringList reasons;
        if (!v.invalidRows.isEmpty())
            reasons.append(rowsText(v.invalidRows)
                           + QStringLiteral("环境变量键名非法（不能为空、不能含 = 或空白、不超过 %1 个字符）")
                                 .arg(EnvTable::kMaxKeyLength));
        if (!v.duplicateRows.isEmpty())
            reasons.append(rowsText(v.duplicateRows) + QStringLiteral("环境变量键名重复（不区分大小写）"));
        if (v.tooManyRows)
            reasons.append(QStringLiteral("环境变量最多 %1 行").arg(EnvTable::kMaxRows));
        if (v.tooManyArgs)
            reasons.append(QStringLiteral("额外启动参数最多 %1 项").arg(EnvTable::kMaxArgs));
        emit notice(reasons.join(QStringLiteral("；")) + QStringLiteral("；未保存"));
        return false;
    }
    item.name = name.trimmed().isEmpty() ? item.name : name.trimmed();
    item.port = port;
    item.profile = profile.trimmed().isEmpty() ? QStringLiteral("web") : profile.trimmed();
    item.dshHome = dshHome.trimmed();
    item.workspace = workspace.trimmed();
    item.autostart = autostart;
    const bool toggled = item.autoRestart != autoRestart;
    item.autoRestart = autoRestart;
    item.env = envRows;
    item.extraArgs = extraArgs;
    const bool versionChanged = item.dshVersion != dshVersion.trimmed();
    item.dshVersion = dshVersion.trimmed();
    m_instances.upsert(item);
    save();
    // 绑定变化后立即重算版本引用计数（不依赖模型是否发出 dataChanged）
    if (versionChanged)
        m_versions.recountReferences();
    // 关闭开关时取消尚未执行的重启计划、n 清零并移除倒计时
    if (toggled)
        m_autoRestarter.onToggle(id, autoRestart);
    // 与 Berth 生成参数冲突的额外参数：允许保存，只提示；启动时由 LaunchEnv::filterArgs 剔除
    QStringList conflicts;
    LaunchEnv::filterArgs(extraArgs, &conflicts);
    if (!conflicts.isEmpty())
        emit notice(QStringLiteral("已保存。以下额外参数与 --port/--profile 冲突，启动时不传递：%1")
                        .arg(conflicts.join(QLatin1Char(' '))));
    return true;
}

QVariantMap AppController::checkEnvTable(const QVariantList &env, const QStringList &extraArgs) const {
    const EnvTable::Validation v = EnvTable::validate(envFromVariant(env, {}), extraArgs);
    QStringList conflicts;
    LaunchEnv::filterArgs(extraArgs, &conflicts);
    QVariantMap map;
    map.insert(QStringLiteral("invalidRows"), rowsVariant(v.invalidRows));
    map.insert(QStringLiteral("duplicateRows"), rowsVariant(v.duplicateRows));
    map.insert(QStringLiteral("dshHomeRows"), rowsVariant(v.dshHomeRows));
    map.insert(QStringLiteral("conflicts"), conflicts);
    map.insert(QStringLiteral("tooManyRows"), v.tooManyRows);
    map.insert(QStringLiteral("tooManyArgs"), v.tooManyArgs);
    map.insert(QStringLiteral("ok"), v.ok());
    return map;
}

void AppController::startInstance(const QString &id) {
    // 手动启动：先取消自动重启计划并将 n 清零，再走启动流水线
    m_autoRestarter.onManualStart(id);
    launch(id, LaunchReason::Manual);
}

void AppController::stopInstance(const QString &id) {
    // 手动停止：取消退避中的重启计划、n 清零并立即移除倒计时
    m_autoRestarter.onManualStop(id);
    // 启动前自检进行中：取消本次启动（已开始的修复进程继续跑完，以免 node_modules 半截）
    if (m_preflightPending.remove(id)) {
        appendLogMarker(m_instances.item(id), QStringLiteral("已取消启动（启动前自检进行中）"));
        applyStatus(id, QStringLiteral("stopped"), 0, {});
        return;
    }
    const bool external = m_supervisor.isExternal(id);
    m_supervisor.stop(id);
    // 外部服务只解除关联，进程保持运行
    if (external)
        emit notice(m_instances.item(id).name + QStringLiteral("：已解除关联，外部进程未被结束"));
}

void AppController::cancelConflict(const QString &id) {
    // 状态在冲突时未改动，配置也未动，这里只丢弃待处理记录
    m_pendingConflicts.remove(id);
}

void AppController::useFreePort(const QString &id) {
    m_pendingConflicts.remove(id);
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    QSet<int> configured;
    for (const Instance &other : m_instances.items())
        configured.insert(other.port);
    const auto port = PortPick::next(item.port,
        [](int p) { return PortChecker::check(p).listening; }, configured);
    if (!port) {
        rejectLaunch(id, QStringLiteral("端口 %1 之后没有可用的空闲端口，未启动").arg(item.port));
        return;
    }
    item.port = *port;
    m_instances.upsert(item);
    save();
    emit notice(item.name + QStringLiteral("：已改用端口 %1").arg(*port));
    launch(id, LaunchReason::Manual);
}

void AppController::reuseExisting(const QString &id) {
    const Occupant pending = m_pendingConflicts.take(id);
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || m_supervisor.isRunning(id))
        return;
    // 选择期间外部服务可能已退出：重新查一次监听与 PID
    const Occupant now = PortChecker::check(item.port);
    if (!now.listening) {
        rejectLaunch(id, QStringLiteral("端口 %1 上的服务已退出，无法复用").arg(item.port));
        return;
    }
    const qint64 pid = now.pid ? now.pid : pending.pid;
    appendLogMarker(item, QStringLiteral("复用现有服务：端口 %1，PID %2")
                                      .arg(QString::number(item.port), pid ? QString::number(pid) : QStringLiteral("未知")));
    // 不启动新进程、不纳入 Job Object；状态置为 external 并由 statusChanged 连接开始 watchExternal
    m_supervisor.attachExternal(id, pid, item.port);
}

void AppController::restartInstance(const QString &id) {
    // Supervisor::stop 同步结束进程树（最多等 3 秒），此后端口已释放，再走启动流水线
    stopInstance(id);
    launch(id, LaunchReason::Restart);
}

void AppController::refreshUsage(const QString &range) {
    QList<StatsService::Target> targets;
    for (const Instance &item : m_instances.items())
        targets.append({item.id, item.name, resolveHome(item.dshHome), item.workspace});
    // 读取期间保留上次结果（需求 22.7）；结果总是异步经 usageFinished 发出
    m_usageRequest = m_stats.usage(StatsService::rangeFromString(range), targets);
    if (!m_usageLoading) {
        m_usageLoading = true;
        emit usageLoadingChanged();
    }
}

void AppController::refreshSessionStats(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    m_stats.sessionStats(item.id, resolveHome(item.dshHome), item.workspace);
}

LaunchResult AppController::launch(const QString &id, LaunchReason reason) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {false, QStringLiteral("泊位不存在")};
    // 已在运行时不动状态，只返回拒绝
    if (m_supervisor.isRunning(id))
        return {false, QStringLiteral("泊位已在运行")};
    if (m_preflightPending.contains(id))
        return {false, QStringLiteral("泊位正在进行启动前自检")};

    // 1. 端口检查（含冲突对话框的挂接点）
    if (const auto rejected = checkPort(item, reason)) {
        // 已弹冲突对话框：不改状态、不提示，由对话框选项收尾
        if (m_pendingConflicts.contains(id))
            return {false, *rejected};
        return rejectLaunch(id, *rejected);
    }

    // 2. 版本可执行文件解析
    QString exe;
    if (const auto rejected = resolveExecutable(item, &exe))
        return rejectLaunch(id, *rejected);

    // 3. Preflight（开关关闭时直接放行，不做任何修复或隔离）。
    // 开启时泊位先进入"启动中"并返回 ok；自检结束后继续 4、5，中止时置为"已停止"并带原因，
    // BatchLauncher 经 onStatus 计为失败
    if (m_settings.data().preflightEnabled)
        return runPreflight(item, exe);
    return startProcess(id, exe);
}

LaunchResult AppController::runPreflight(const Instance &item, const QString &exe) {
    Preflight::Request req;
    req.id = item.id;
    req.home = Quarantine::normalizeHome(resolveHome(item.dshHome));
    req.profile = item.profile.isEmpty() ? QStringLiteral("web") : item.profile;
    req.profileDir = profileDirForHome(item.dshHome, req.profile);
    req.exe = exe;
    req.logPath = item.logPath;
    req.secrets = secretsOf(item);
    m_preflightPending.insert(item.id);
    applyStatus(item.id, QStringLiteral("starting"), 0, {});
    const QString id = item.id;
    m_preflight.run(req, [this, id, exe](const Preflight::Result &result) { onPreflightDone(id, exe, result); });
    return {true, {}};
}

void AppController::onPreflightDone(const QString &id, const QString &exe, const Preflight::Result &result) {
    // 自检期间被停止或删除：不再启动（隔离已生效，记录保留）
    if (!m_preflightPending.remove(id))
        return;
    if (m_quitting) {
        applyStatus(id, QStringLiteral("stopped"), 0, {});
        return;
    }
    if (result.aborted) {
        rejectLaunch(id, QStringLiteral("profile 配置无法读取，未启动：%1").arg(result.error));
        return;
    }
    if (!result.quarantined.isEmpty()) {
        QStringList summary;
        for (const QuarantineEntry &e : result.quarantined)
            summary.append(QStringLiteral("%1（%2）").arg(e.name, Preflight::reasonText(e.reason)));
        emit pluginsQuarantined(id, summary);
    }
    startProcess(id, exe);
}

LaunchResult AppController::startProcess(const QString &id, const QString &exe) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {false, QStringLiteral("泊位不存在")};

    // 4. LaunchEnv 组装。参数顺序：--profile <p> --port <n> --no-open + 过滤后的 extraArgs
    // dsh --profile <name> 启动指定 profile；其后首个启动器不认识的参数起，全部交给 web app
    LaunchSpec spec;
    spec.id = item.id;
    spec.exe = exe;
    spec.args = {
        QStringLiteral("--profile"), item.profile.isEmpty() ? QStringLiteral("web") : item.profile,
        QStringLiteral("--port"), QString::number(item.port),
        QStringLiteral("--no-open")
    };
    spec.args += LaunchEnv::filterArgs(item.extraArgs);
    // 泊位环境变量表按行写入（DSH_HOME 始终被 Berth 计算值覆盖）；
    // "泊位继承代理"开启时注入代理环境（不使用代理时 proxyEnv 为空），表中同名键优先
    spec.env = LaunchEnv::build(QProcessEnvironment::systemEnvironment(), item.env, resolveHome(item.dshHome),
                                m_proxy.proxyEnv(), m_settings.data().instanceInheritProxy);
    spec.cwd = item.workspace;
    spec.port = item.port;
    spec.logPath = item.logPath;
    spec.secrets = secretsOf(item);

    // 5. Supervisor::start；返回 false 时 Supervisor 已发 failed（并经 applyStatus 提示），这里只回报拒绝
    if (!m_supervisor.start(spec)) {
        const QString error = m_instances.item(id).lastError;
        return {false, error.isEmpty() ? QStringLiteral("进程启动失败") : error};
    }
    return {true, {}};
}

std::optional<QString> AppController::checkPort(const Instance &item, LaunchReason reason) {
    // 查系统 TCP 监听表（IPv4/IPv6），同步完成，远低于 1 秒。
    // 手动启动/重启弹冲突对话框（复用选项由 probeDsh 决定），批量与自动重启不弹框直接拒绝
    m_pendingConflicts.remove(item.id);
    const Occupant occupant = PortChecker::check(item.port);
    if (!occupant.listening)
        return std::nullopt;
    if (reason == LaunchReason::Manual || reason == LaunchReason::Restart) {
        const QString unknown = QStringLiteral("未知");
        m_pendingConflicts.insert(item.id, occupant);
        emit portConflict(item.id, item.name, item.port,
                          occupant.pid ? QString::number(occupant.pid) : unknown,
                          occupant.name.isEmpty() ? unknown : occupant.name,
                          occupant.path.isEmpty() ? unknown : occupant.path);
        m_portChecker.probeDsh(item.port);
    }
    return QStringLiteral("端口 %1 已被占用（%2），未启动").arg(QString::number(item.port), occupant.describe());
}

std::optional<QString> AppController::resolveExecutable(const Instance &item, QString *exe) const {
    // 未绑定版本用 System_Dsh；绑定版本缺失时返回含版本号与原因的拒绝（不创建进程）
    if (item.dshVersion.trimmed().isEmpty()) {
        *exe = m_settings.resolvedDshExecutable();
        return std::nullopt;
    }
    return m_versions.resolve(item.dshVersion.trimmed(), exe);
}

LaunchResult AppController::rejectLaunch(const QString &id, const QString &reason) {
    // 泊位保持"已停止"，lastError 记录原因；applyStatus 会把原因作为 notice 发出
    applyStatus(id, QStringLiteral("stopped"), 0, reason);
    return {false, reason};
}

void AppController::openUi(const QString &id) {
    if (m_instances.item(id).id.isEmpty())
        return;
    requestUi(id, 0);
}

void AppController::requestUi(const QString &id, int attempt) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    // 运行中但 token 行还没刷进日志：最多等 5 秒
    if (tokenUrl(id).isEmpty() && item.status == QLatin1String("running") && attempt < 20) {
        QTimer::singleShot(250, this, [this, id, attempt]() { requestUi(id, attempt + 1); });
        return;
    }
    emit uiRequested(id, uiUrl(id));
}

void AppController::openInBrowser(const QString &id) {
    if (m_instances.item(id).id.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl(uiUrl(id)));
}

QString AppController::uiUrl(const QString &id) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {};
    const QString url = tokenUrl(id);
    return url.isEmpty() ? QStringLiteral("http://127.0.0.1:%1/").arg(item.port) : url;
}

QString AppController::tokenUrl(const QString &id) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return {};
    const QString text = readLog(id, 262144);
    // 只看最后一次启动之后的输出，避免拿到上次运行的过期 token
    const qsizetype start = text.lastIndexOf(QStringLiteral("] 启动: "));
    const QString recent = start >= 0 ? text.mid(start) : text;
    // dsh web 启动后打印：dsh web: http://127.0.0.1:<port>/?token=...
    const QRegularExpression re(QStringLiteral("https?://(?:127\\.0\\.0\\.1|localhost):%1/\\S*").arg(item.port));
    QRegularExpressionMatchIterator it = re.globalMatch(recent);
    QString last;
    while (it.hasNext())
        last = it.next().captured(0);
    return last;
}

void AppController::openLog(const QString &id) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || item.logPath.isEmpty())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item.logPath));
}

QString AppController::readLog(const QString &id, int maxBytes) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty() || item.logPath.isEmpty())
        return {};
    QFile file(item.logPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = file.size();
    const bool truncated = maxBytes > 0 && size > maxBytes;
    if (truncated)
        file.seek(size - maxBytes);
    QByteArray data = file.readAll();
    // 从中间截断时丢掉第一行残片，避免半个 UTF-8 字符
    if (truncated) {
        const int nl = data.indexOf('\n');
        if (nl >= 0)
            data.remove(0, nl + 1);
    }
    QString text = QString::fromUtf8(data);
    // 去掉 ANSI 颜色/光标控制序列，终端面板只显示纯文本
    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    text.remove(ansi);
    text.remove(QLatin1Char('\r'));
    return text;
}


QVariantMap AppController::instance(const QString &id) const {
    const Instance item = m_instances.item(id);
    QVariantMap map;
    if (item.id.isEmpty())
        return map;
    map.insert(QStringLiteral("id"), item.id);
    map.insert(QStringLiteral("name"), item.name);
    map.insert(QStringLiteral("port"), item.port);
    map.insert(QStringLiteral("profile"), item.profile);
    map.insert(QStringLiteral("dshHome"), item.dshHome);
    map.insert(QStringLiteral("workspace"), item.workspace);
    map.insert(QStringLiteral("autostart"), item.autostart);
    map.insert(QStringLiteral("autoRestart"), item.autoRestart);
    map.insert(QStringLiteral("status"), item.status);
    map.insert(QStringLiteral("pid"), item.pid);
    map.insert(QStringLiteral("lastError"), item.lastError);
    map.insert(QStringLiteral("logPath"), item.logPath);
    // 编辑界面需要原值；敏感值由 QML 以掩码显示
    QVariantList env;
    for (const InstanceEnvVar &var : item.env) {
        QVariantMap row;
        row.insert(QStringLiteral("key"), var.key);
        row.insert(QStringLiteral("value"), var.value);
        row.insert(QStringLiteral("secret"), var.secret);
        env.append(row);
    }
    map.insert(QStringLiteral("env"), env);
    map.insert(QStringLiteral("extraArgs"), item.extraArgs);
    map.insert(QStringLiteral("dshVersion"), item.dshVersion);
    map.insert(QStringLiteral("iconKind"), item.icon.kind);
    map.insert(QStringLiteral("iconValue"), item.icon.value);
    map.insert(QStringLiteral("group"), item.group);
    map.insert(QStringLiteral("tags"), item.tags);
    map.insert(QStringLiteral("notes"), item.notes);
    return map;
}

// ---- 泊位图标、分组、标签、备注（需求 9） ----

namespace {

QVariantMap metaResult(const std::optional<QString> &err) {
    QVariantMap r;
    r.insert(QStringLiteral("ok"), !err.has_value());
    r.insert(QStringLiteral("error"), err.value_or(QString()));
    return r;
}

// QML 传来的可能是 file:/// URL，也可能是本地路径
QString localPathOf(const QString &pathOrUrl) {
    const QUrl url(pathOrUrl);
    return url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
}

QStringList trimmedTags(const QStringList &tags) {
    QStringList out;
    out.reserve(tags.size());
    for (const QString &t : tags)
        out.append(t.trimmed());
    return out;
}

} // namespace

QVariantMap AppController::checkInstanceMeta(const QString &group, const QStringList &tags,
                                             const QString &notes) const {
    if (auto e = InstanceMeta::validateGroup(group))
        return metaResult(e);
    if (auto e = InstanceMeta::validateTags(trimmedTags(tags)))
        return metaResult(e);
    return metaResult(InstanceMeta::validateNotes(notes));
}

QVariantMap AppController::checkTag(const QStringList &existing, const QString &tag) const {
    return metaResult(InstanceMeta::validateNewTag(existing, tag));
}

QVariantMap AppController::checkImage(const QString &pathOrUrl) const {
    const QFileInfo fi(localPathOf(pathOrUrl));
    QFile f(fi.absoluteFilePath());
    const bool readable = fi.isFile() && f.open(QIODevice::ReadOnly);
    return metaResult(InstanceMeta::validateImage(fi.fileName(), fi.isFile() ? fi.size() : 0, readable));
}

QString AppController::iconFileUrl(const QString &relPath) const {
    if (relPath.trimmed().isEmpty())
        return {};
    const QString path = QDir(m_settings.dataDir() + QStringLiteral("/icons")).filePath(relPath);
    if (!QFileInfo(path).isFile())
        return {};
    return QUrl::fromLocalFile(path).toString();
}

QVariantMap AppController::setInstanceMeta(const QString &id, const QString &iconKind,
                                           const QString &iconValue, const QString &group,
                                           const QStringList &tags, const QString &notes) {
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return metaResult(QStringLiteral("泊位不存在"));
    const QVariantMap check = checkInstanceMeta(group, tags, notes);
    if (!check.value(QStringLiteral("ok")).toBool())
        return check;

    InstanceIcon icon = item.icon;   // 保留 extra
    if (iconKind == QLatin1String("none")) {
        icon.kind = iconKind;
        icon.value.clear();
    } else if (iconKind == QLatin1String("builtin")) {
        if (iconValue.trimmed().isEmpty())
            return metaResult(QStringLiteral("请选择一个内置图标"));
        icon.kind = iconKind;
        icon.value = iconValue.trimmed();
    } else if (iconKind == QLatin1String("file")) {
        const bool unchanged = item.icon.kind == QLatin1String("file") && item.icon.value == iconValue;
        if (!unchanged) {
            const QVariantMap img = checkImage(iconValue);
            if (!img.value(QStringLiteral("ok")).toBool())
                return img;
            const QString src = localPathOf(iconValue);
            const QString dirPath = m_settings.dataDir() + QStringLiteral("/icons");
            if (!QDir().mkpath(dirPath))
                return metaResult(QStringLiteral("无法创建图标目录：%1").arg(QDir::toNativeSeparators(dirPath)));
            // 每次复制用新文件名，避免界面缓存旧图
            const QString rel = item.id + QLatin1Char('-')
                                + QUuid::createUuid().toString(QUuid::Id128).left(8) + QLatin1Char('.')
                                + QFileInfo(src).suffix().toLower();
            if (!QFile::copy(src, QDir(dirPath).filePath(rel)))
                return metaResult(QStringLiteral("复制图标失败：%1").arg(QDir::toNativeSeparators(src)));
            icon.kind = iconKind;
            icon.value = rel;
        }
    } else {
        return metaResult(QStringLiteral("未知的图标类型：%1").arg(iconKind));
    }

    item.icon = icon;
    item.group = group.trimmed();
    item.tags = trimmedTags(tags);
    item.notes = notes;
    m_instances.upsert(item);
    save();
    return metaResult(std::nullopt);
}

QVariantList AppController::groupedInstances(const QString &keyword) const {
    const QList<Instance> items = m_instances.items();
    QVariantList out;
    for (const InstanceMeta::Group &g : InstanceMeta::search(items, keyword)) {
        QVariantList rows;
        for (int i : g.indices) {
            const Instance &it = items.at(i);
            QVariantMap r;
            r.insert(QStringLiteral("id"), it.id);
            r.insert(QStringLiteral("name"), it.name);
            r.insert(QStringLiteral("port"), it.port);
            r.insert(QStringLiteral("profile"), it.profile);
            r.insert(QStringLiteral("status"), it.status);
            r.insert(QStringLiteral("iconKind"), it.icon.kind);
            r.insert(QStringLiteral("iconValue"), it.icon.value);
            r.insert(QStringLiteral("tags"), it.tags);
            rows.append(r);
        }
        QVariantMap gm;
        gm.insert(QStringLiteral("name"), g.name);
        gm.insert(QStringLiteral("ungrouped"), g.ungrouped);
        gm.insert(QStringLiteral("count"), rows.size());
        gm.insert(QStringLiteral("items"), rows);
        out.append(gm);
    }
    return out;
}

QString AppController::defaultDshHome() const {
    return resolveHome({});
}

QString AppController::resolveHome(const QString &dshHome) const {
    const QString given = dshHome.trimmed();
    if (!given.isEmpty())
        return QDir::cleanPath(given);
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("DSH_HOME"));
    if (!env.trimmed().isEmpty())
        return QDir::cleanPath(env.trimmed());
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.dsh"));
}

QString AppController::profileDirFor(const QString &id, QString *error) const {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty()) {
        if (error)
            *error = QStringLiteral("找不到泊位");
        return {};
    }
    return resolveHome(item.dshHome) + QStringLiteral("/profiles/") + item.profile;
}

QVariantMap AppController::listPlugins(const QString &id) const {
    QString error;
    const QString dir = profileDirFor(id, &error);
    if (dir.isEmpty()) {
        QVariantMap result;
        result.insert(QStringLiteral("ok"), false);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("plugins"), QVariantList());
        return result;
    }
    return PluginOps::readPlugins(dir);
}

QVariantMap AppController::listPluginsForProfile(const QString &home, const QString &profile) const {
    const QString dir = profileDirForHome(home, profile);
    return PluginOps::readPlugins(dir);
}

QVariantMap AppController::unquarantine(const QString &home, const QString &profile, const QString &name) {
    QVariantMap r = m_plugins.setEnabled(home, profile, name, true);
    if (!r.value(QStringLiteral("ok")).toBool())
        return r;
    m_quarantine.remove(Quarantine::normalizeHome(resolveHome(home)), profile, name);
    // 启用时已发过 pluginsChanged，但当时记录尚未删除；再发一次让列表去掉"已隔离"标记
    emit m_plugins.pluginsChanged(home, profile);
    return r;
}

QVariantList AppController::quarantinedFor(const QString &id) const {
    QVariantList out;
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return out;
    const QString profile = item.profile.isEmpty() ? QStringLiteral("web") : item.profile;
    const QString home = Quarantine::normalizeHome(resolveHome(item.dshHome));
    for (const QuarantineEntry &e : m_quarantine.entriesFor(home, profile)) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), e.name);
        map.insert(QStringLiteral("reason"), e.reason);
        map.insert(QStringLiteral("reasonText"), Preflight::reasonText(e.reason));
        map.insert(QStringLiteral("at"), e.at);
        map.insert(QStringLiteral("detail"), e.detail);
        out.append(map);
    }
    return out;
}

bool AppController::profileBusy() const {
    return m_profileBusy;
}

bool AppController::pluginBusy() const {
    return m_pluginBusy;
}

void AppController::setPluginBusy(bool busy) {
    if (m_pluginBusy == busy)
        return;
    m_pluginBusy = busy;
    emit pluginBusyChanged();
}

void AppController::setProfileBusy(bool busy) {
    if (m_profileBusy == busy)
        return;
    m_profileBusy = busy;
    emit profileBusyChanged();
}

void AppController::uninstallPlugin(const QString &id, const QString &name, bool removePackage) {
    QString error;
    const QString dir = profileDirFor(id, &error);
    if (dir.isEmpty()) {
        emit pluginUninstallFinished(id, false, error);
        return;
    }
    const Instance target = m_instances.item(id);
    if (target.id.isEmpty()) {
        emit pluginUninstallFinished(id, false, QStringLiteral("找不到泊位"));
        return;
    }
    uninstallFromProfile(id, target.dshHome, target.profile, name, removePackage);
}

void AppController::uninstallPluginForProfile(const QString &home, const QString &profile,
                                              const QString &name, bool removePackage) {
    // id 为空：按 (home, profile) 直接操作，结果信号照发 pluginUninstallFinished（id 为空串）
    uninstallFromProfile(QString(), home, profile, name, removePackage);
}

QStringList AppController::detectProfiles(const QString &dshHome) const {
    const QString home = resolveHome(dshHome);
    QDir dir(home + QStringLiteral("/profiles"));
    if (!dir.exists())
        return {};
    QStringList names;
    const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString path = info.absoluteFilePath();
        if (QFile::exists(path + QStringLiteral("/package.json"))
            || QFile::exists(path + QStringLiteral("/cordis.patch.yml")))
            names.push_back(info.fileName());
    }
    return names;
}

QStringList AppController::knownProfiles() const {
    return m_knownProfiles;
}

void AppController::refreshProfiles() {
    const QStringList found = detectProfiles({});
    if (found != m_knownProfiles) {
        m_knownProfiles = found;
        emit profilesChanged();
    }
    rebuildTree();
}

int AppController::importDetectedProfiles() {
    refreshProfiles();
    const QString home = defaultDshHome();
    int added = 0;
    for (const QString &name : m_knownProfiles) {
        bool exists = false;
        for (const Instance &item : m_instances.items()) {
            if (item.profile == name && resolveHome(item.dshHome) == home) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;
        Instance item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        int port = 3080;
        while (m_instances.containsPort(port))
            port += 1;
        item.port = port;
        item.name = name;
        item.profile = name;
        item.dshHome = home;
        item.status = QStringLiteral("stopped");
        item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
        m_instances.upsert(item);
        ++added;
    }
    if (added > 0)
        save();
    emit notice(added > 0
                    ? QStringLiteral("已识别 %1 个 profile，新增 %2 个泊位").arg(m_knownProfiles.size()).arg(added)
                    : QStringLiteral("已识别 %1 个 profile，没有新泊位").arg(m_knownProfiles.size()));
    return added;
}

QString AppController::profileDirForHome(const QString &home, const QString &profile) const {
    return resolveHome(home) + QStringLiteral("/profiles/") + profile;
}

// 卸载插件共用流程（id 为空表示按 (home, profile) 直接操作）：
// 1. 忙 / 引用泊位在跑就拒绝；2. 有 pnpm 才能拆包；3. 先改 bundles 再跑 dsh plugin remove
void AppController::uninstallFromProfile(const QString &id, const QString &home, const QString &profile,
                                         const QString &name, bool removePackage) {
    // 1. 已有卸载或其它 profile 包操作在进行
    if (m_pluginBusy || m_profileBusy || m_plugins.isBusy(home, profile)) {
        emit pluginUninstallFinished(id, false, QStringLiteral("有其它 profile 操作在进行"));
        return;
    }

    const QString resolvedHome = resolveHome(home);
    const QString dir = resolvedHome + QStringLiteral("/profiles/") + profile;

    // 2. 同一 DSH_HOME + profile 的泊位必须都已停止（删包/改 bundles 才不会被进程占用）
    QStringList busyNames;
    for (const Instance &item : m_instances.items()) {
        if (item.profile != profile || resolveHome(item.dshHome) != resolvedHome)
            continue;
        if (item.status == QLatin1String("running") || item.status == QLatin1String("starting")
            || item.status == QLatin1String("stopping"))
            busyNames.push_back(item.name);
    }
    if (!busyNames.isEmpty()) {
        emit pluginUninstallFinished(id, false,
                                     QStringLiteral("先停止：") + busyNames.join(QStringLiteral("、")));
        return;
    }

    // 3. 要卸载插件包时先确认本机有 pnpm，没有就不做任何修改
    if (removePackage && QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty()) {
        emit pluginUninstallFinished(id, false, QStringLiteral("需要安装 pnpm"));
        return;
    }

    // 4. 从 dsh.profile.bundles 移除（不在其中时 removeFromBundles 直接成功、不写文件）
    QString error;
    if (!PluginOps::removeFromBundles(dir, name, &error)) {
        emit pluginUninstallFinished(id, false, error);
        return;
    }

    // 5. 只改配置
    if (!removePackage) {
        emit pluginUninstallFinished(id, true, QStringLiteral("已从配置移除"));
        return;
    }

    // 6. 异步执行 dsh plugin --profile <p> remove <name>；失败不回滚 bundles
    setPluginBusy(true);
    ProcessOptions options;
    if (!resolvedHome.isEmpty())
        options.env.insert(QStringLiteral("DSH_HOME"), resolvedHome);
    options.cwd = dir;
    ProcessTask *task = m_processRunner.run(m_settings.resolvedDshExecutable(), {
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), profile,
        QStringLiteral("remove"), name
    }, options);

    const QString failPrefix = QStringLiteral("bundles 已移除，pnpm 失败");
    connect(task, &ProcessTask::finished, this,
            [this, task, id, failPrefix](bool ok, int, const QString &tail, bool) {
        setPluginBusy(false);
        // 启动失败：没有输出，附上启动失败原因
        if (task->failedToStart()) {
            emit pluginUninstallFinished(id, false, failPrefix + QLatin1Char('\n') + task->errorString());
            return;
        }
        QString message = ok ? QStringLiteral("已卸载") : failPrefix;
        if (!tail.isEmpty())
            message += QLatin1Char('\n') + tail;
        emit pluginUninstallFinished(id, ok, message);
    });
}

void AppController::startProfileProcess(const QString &program, const QStringList &args, const QString &dshHome,
                                        const QString &workingDir, const QString &failPrefix,
                                        const QString &finishProfileName) {
    m_profileFinishName = finishProfileName;
    ProcessOptions options;
    if (!dshHome.isEmpty())
        options.env.insert(QStringLiteral("DSH_HOME"), dshHome);
    options.cwd = workingDir;
    ProcessTask *task = m_processRunner.run(program, args, options);

    connect(task, &ProcessTask::finished, this,
            [this, task, failPrefix](bool ok, int, const QString &summary, bool) {
        setProfileBusy(false);
        // 启动失败：没有输出，附上启动失败原因
        if (task->failedToStart()) {
            emit profileOpFinished(false, failPrefix + QLatin1Char('\n') + task->errorString(),
                                   m_profileFinishName);
            return;
        }
        // 成功后目录内容已变化（新建/复制出 profile），树的探测结果与 selectedProfile 刷新都依赖它
        if (ok)
            refreshProfiles();
        emit profileOpFinished(ok, ok ? QStringLiteral("完成")
                                      : failPrefix + (summary.isEmpty() ? QString()
                                            : QLatin1Char('\n') + summary), m_profileFinishName);
    });
}

QVariantMap AppController::profileInfo(const QString &home, const QString &name) const {
    return ProfileOps::readProfileInfo(profileDirForHome(home, name));
}

void AppController::createProfile(const QString &name, const QString &fromTemplate) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString trimmed = name.trimmed();
    const QString invalid = ProfileOps::validateProfileName(trimmed);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, trimmed);
        return;
    }
    const QString dir = profileDirForHome({}, trimmed);
    if (QFileInfo(dir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dir)), trimmed);
        return;
    }
    // dsh 的官方创建路径：--from-default-profile 初始化目录，--dump-config 打印配置树即退出，
    // 不会拉起任何 app（boot-free）
    setProfileBusy(true);
    startProfileProcess(m_settings.resolvedDshExecutable(), {
        QStringLiteral("--profile"), trimmed,
        QStringLiteral("--from-default-profile"), fromTemplate,
        QStringLiteral("--dump-config")
    }, defaultDshHome(), {}, QStringLiteral("profile 创建失败"), trimmed);
}

void AppController::deleteProfile(const QString &home, const QString &name) {
    if (m_profileBusy || m_pluginBusy)
        return;
    if (!ProfileOps::validateProfileName(name).isEmpty()) {
        // 名字不合法通常意味着已不存在，直接按成功收尾让 UI 清掉选中态
        emit profileOpFinished(true, QString(), name);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString dir = resolvedHome + QStringLiteral("/profiles/") + name;
    // 被任何泊位引用（无论运行与否）都拒绝删除，提示先删泊位
    QStringList referenced;
    for (const Instance &item : m_instances.items()) {
        if (item.profile == name && resolveHome(item.dshHome) == resolvedHome)
            referenced.push_back(item.name);
    }
    if (!referenced.isEmpty()) {
        emit profileOpFinished(false, QStringLiteral("先删除引用它的泊位：")
                                     + referenced.join(QStringLiteral("、")), name);
        return;
    }
    QString error;
    if (!ProfileOps::removeProfileTree(dir, &error)) {
        emit profileOpFinished(false, error, name);
        return;
    }
    refreshProfiles();
    emit profileOpFinished(true, QStringLiteral("已删除 profile「%1」").arg(name), name);
}

void AppController::renameProfile(const QString &home, const QString &oldName, const QString &newName) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString renamed = newName.trimmed();
    if (renamed == oldName) {
        emit profileOpFinished(true, QString(), renamed);
        return;
    }
    const QString invalid = ProfileOps::validateProfileName(renamed);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, oldName);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString srcDir = resolvedHome + QStringLiteral("/profiles/") + oldName;
    const QString dstDir = resolvedHome + QStringLiteral("/profiles/") + renamed;
    if (!QFileInfo(srcDir).isDir()) {
        emit profileOpFinished(false, QStringLiteral("profile 目录不存在：%1")
                                     .arg(QDir::toNativeSeparators(srcDir)), oldName);
        return;
    }
    if (QFileInfo(dstDir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目标目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dstDir)), oldName);
        return;
    }
    // 相关泊位在跑时改目录名，进程会占用 node_modules 导致 rename 失败，先拒绝
    QStringList busyNames;
    for (const Instance &item : m_instances.items()) {
        if (item.profile != oldName || resolveHome(item.dshHome) != resolvedHome)
            continue;
        if (item.status == QLatin1String("running") || item.status == QLatin1String("starting")
            || item.status == QLatin1String("stopping"))
            busyNames.push_back(item.name);
    }
    if (!busyNames.isEmpty()) {
        emit profileOpFinished(false, QStringLiteral("先停止：")
                                     + busyNames.join(QStringLiteral("、")), oldName);
        return;
    }
    if (!QDir().rename(srcDir, dstDir)) {
        emit profileOpFinished(false, QStringLiteral("重命名目录失败（可能被占用）"), oldName);
        return;
    }
    // 同步引用它的泊位（instances.json 里存的是 profile 名）
    for (Instance &item : m_instances.mutableItems()) {
        if (item.profile == oldName && resolveHome(item.dshHome) == resolvedHome)
            item.profile = renamed;
    }
    refreshProfiles();
    emit profileOpFinished(true, QStringLiteral("已重命名为「%1」").arg(renamed), renamed);
}

void AppController::copyProfile(const QString &home, const QString &srcName, const QString &newName) {
    if (m_profileBusy || m_pluginBusy)
        return;
    const QString target = newName.trimmed();
    const QString invalid = ProfileOps::validateProfileName(target);
    if (!invalid.isEmpty()) {
        emit profileOpFinished(false, invalid, srcName);
        return;
    }
    const QString resolvedHome = resolveHome(home);
    const QString srcDir = resolvedHome + QStringLiteral("/profiles/") + srcName;
    const QString dstDir = resolvedHome + QStringLiteral("/profiles/") + target;
    if (!QFileInfo(srcDir).isDir()) {
        emit profileOpFinished(false, QStringLiteral("profile 目录不存在：%1")
                                     .arg(QDir::toNativeSeparators(srcDir)), srcName);
        return;
    }
    if (QFileInfo(dstDir).exists()) {
        emit profileOpFinished(false, QStringLiteral("目标目录已存在：%1")
                                     .arg(QDir::toNativeSeparators(dstDir)), srcName);
        return;
    }
    QString error;
    if (!ProfileOps::copyProfileFiles(srcDir, dstDir, &error)) {
        emit profileOpFinished(false, error, srcName);
        return;
    }
    // 找不到 pnpm 时配置已复制完，不再装依赖（纯 dsh 内置 bundle 的 profile 本就不需要）
    if (QStandardPaths::findExecutable(QStringLiteral("pnpm")).isEmpty()) {
        emit profileOpFinished(true, QStringLiteral("已复制配置文件；未找到 pnpm，依赖未重建"), target);
        return;
    }
    // 异步在新目录跑 dsh plugin install（转发 pnpm install），从 lockfile 重建依赖
    setProfileBusy(true);
    startProfileProcess(m_settings.resolvedDshExecutable(), {
        QStringLiteral("plugin"),
        QStringLiteral("--profile"), target,
        QStringLiteral("install")
    }, resolvedHome, dstDir, QStringLiteral("目录已复制，但依赖安装失败"), target);
}

QString AppController::createInstanceFor(const QString &home, const QString &profile) {
    Instance item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int port = 3080;
    while (m_instances.containsPort(port))
        port += 1;
    item.port = port;
    // 命名沿用「泊位 N」惯例，名字里带上 profile 方便区分
    item.name = QStringLiteral("%1 · 泊位 %2").arg(profile, QString::number(m_instances.rowCount() + 1));
    item.profile = profile.trimmed().isEmpty() ? QStringLiteral("web") : profile.trimmed();
    // 默认 home 的泊位 dshHome 留空，保持数据整洁（resolveHome(空) == defaultHome）
    item.dshHome = resolveHome(home) == defaultDshHome() ? QString() : resolveHome(home);
    item.status = QStringLiteral("stopped");
    item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
    m_instances.upsert(item);
    save();
    return item.id;
}

void AppController::openProfileDir(const QString &home, const QString &name) {
    const QString dir = profileDirForHome(home, name);
    if (!QFileInfo(dir).isDir()) {
        emit notice(QStringLiteral("profile 目录不存在：%1").arg(QDir::toNativeSeparators(dir)));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString AppController::statusText(const QString &status) const {
    return InstanceModel::statusText(status);
}

// 全量重建泊位树：home 归一成 resolve 后的路径（profile 面板的操作也按归一值进行），
// knownProfiles 属于默认 home；目录缺失的组合进 missingKeys 只影响 DirExistsRole 展示
void AppController::rebuildTree() {
    QList<Instance> normalized;
    for (const Instance &item : m_instances.items()) {
        Instance copy = item;
        copy.dshHome = resolveHome(item.dshHome);
        normalized.push_back(copy);
    }
    const QString home = defaultDshHome();
    QSet<QString> missing;
    QSet<QString> seen;
    auto checkDir = [&](const QString &profileHome, const QString &profileName) {
        const QString key = profileHome + QChar(u'\u0001') + profileName;
        if (seen.contains(key))
            return;
        seen.insert(key);
        if (!QFileInfo(profileHome + QStringLiteral("/profiles/") + profileName).isDir())
            missing.insert(key);
    };
    for (const Instance &item : normalized)
        checkDir(item.dshHome, item.profile);
    for (const QString &name : m_knownProfiles)
        checkDir(home, name);
    m_profileTree.rebuild(normalized, m_knownProfiles, home, missing);
}

void AppController::load() {
    // schema 0 的顶层数组由 DataFile 包装成 {instances: [...]}
    const QJsonObject root = m_store.load(kInstancesFile, kInstancesSchema, kInstancesKey);
    QStringList typeErrors;
    const DataFile::FieldReader reader(root, kInstancesFile, &typeErrors);
    const QList<Instance> all = InstanceCodec::listFromJson(reader.array(kInstancesKey), &typeErrors);
    m_store.reportTypeErrors(typeErrors);

    QList<Instance> items;
    for (Instance item : all) {
        if (item.id.isEmpty())
            continue;
        item.status = QStringLiteral("stopped");
        if (item.logPath.isEmpty())
            item.logPath = m_settings.dataDir() + QStringLiteral("/logs/") + item.id + QStringLiteral(".log");
        items.push_back(item);
    }
    m_instances.setItems(items);
}

void AppController::save() {
    // InstanceCodec 写出全部持久化字段与 extra；DataStore 负责备份、只读拒写与顶层未知字段合并
    QJsonObject known;
    known.insert(kInstancesKey, InstanceCodec::listToJson(m_instances.items()));
    m_store.save(kInstancesFile, known);
}

void AppController::applyStatus(const QString &id, const QString &status, qint64 pid, const QString &error) {
    Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    item.status = status;
    item.pid = pid;
    item.lastError = error;
    m_instances.upsert(item);
    if (!error.isEmpty())
        emit notice(item.name + QStringLiteral("：") + error);
    if (status == QLatin1String("running") && m_settings.openUiOnStart())
        openUi(id);
    // 批量启停据此判定在途泊位是否完成
    m_batch.onStatus(id, status, error);
    m_restartHint.onStatus(id, status, error);
    emit instanceStatusChanged(id, status);
}

BatchLauncher::Ops AppController::makeBatchOps() {
    BatchLauncher::Ops ops;
    // 泊位列表顺序即 instances.json 中的顺序
    ops.allIds = [this]() {
        QStringList ids;
        for (const Instance &item : m_instances.items())
            ids.append(item.id);
        return ids;
    };
    ops.autostartIds = [this]() {
        QStringList ids;
        for (const Instance &item : m_instances.items()) {
            if (item.autostart)
                ids.append(item.id);
        }
        return ids;
    };
    ops.statusOf = [this](const QString &id) { return m_instances.item(id).status; };
    ops.nameOf = [this](const QString &id) {
        const Instance item = m_instances.item(id);
        return item.name.isEmpty() ? id : item.name;
    };
    ops.intervalSec = [this]() { return m_settings.staggerSec(); };
    ops.start = [this](const QString &id, bool autostart, QString *reason) {
        // 与手动启动一致：先取消自动重启计划；端口占用时流水线直接拒绝（不弹框）
        m_autoRestarter.onManualStart(id);
        const LaunchResult r = launch(id, autostart ? LaunchReason::Autostart : LaunchReason::Batch);
        if (!r.ok && reason)
            *reason = r.reason;
        return r.ok;
    };
    ops.stop = [this](const QString &id) { stopInstance(id); };
    return ops;
}

RestartHintController::Ops AppController::makeRestartHintOps() {
    RestartHintController::Ops ops;
    ops.statusOf = [this](const QString &id) { return m_instances.item(id).status; };
    ops.nameOf = [this](const QString &id) {
        const Instance item = m_instances.item(id);
        return item.name.isEmpty() ? id : item.name;
    };
    ops.restart = [this](const QString &id, QString *reason) {
        // 与 restartInstance 一致先同步停止；按批量方式启动，端口占用时不弹冲突框、直接计为失败
        stopInstance(id);
        m_autoRestarter.onManualStart(id);
        const LaunchResult r = launch(id, LaunchReason::Batch);
        if (!r.ok && reason)
            *reason = r.reason;
        return r.ok;
    };
    return ops;
}

void AppController::updateCountdown(const QString &id, int sec, int n, int N) {
    if (sec <= 0) {
        if (m_restartCountdowns.remove(id) > 0)
            emit restartCountdownsChanged();
        return;
    }
    QVariantMap entry;
    entry.insert(QStringLiteral("sec"), sec);
    entry.insert(QStringLiteral("n"), n);
    entry.insert(QStringLiteral("N"), N);
    m_restartCountdowns.insert(id, entry);
    emit restartCountdownsChanged();
}

void AppController::handleRestartGaveUp(const QString &id, int attempts) {
    const Instance item = m_instances.item(id);
    if (item.id.isEmpty())
        return;
    const QString message = QStringLiteral("连续崩溃，已自动重启 %1 次，不再重启").arg(attempts);
    appendLogMarker(item, QStringLiteral("崩溃已停止：") + message);
    qWarning().noquote() << QStringLiteral("[AutoRestarter]") << item.name << message;
    // 不走 applyStatus：提醒改由 Notifier 发崩溃通知，不再发主窗口 notice
    Instance updated = item;
    updated.status = QStringLiteral("crashStopped");
    updated.pid = 0;
    updated.lastError = message;
    m_instances.upsert(updated);
    m_batch.onStatus(id, updated.status, message);
    m_restartHint.onStatus(id, updated.status, message);
    emit instanceStatusChanged(id, updated.status);
    emit restartGaveUp(id, attempts);
}

QVariantMap AppController::summary() const {
    QMap<QString, QString> statuses;
    for (const Instance &item : m_instances.items())
        statuses.insert(item.id, item.status);
    const Summary::Result r = Summary::compute(statuses, m_metrics.latestSamples());
    QVariantMap out;
    out.insert(QStringLiteral("total"), r.total);
    out.insert(QStringLiteral("running"), r.running);
    out.insert(QStringLiteral("failed"), r.failed);
    out.insert(QStringLiteral("memMb"), r.memMb);
    out.insert(QStringLiteral("cpuPercent"), r.cpuPercent);
    out.insert(QStringLiteral("memText"), Summary::formatMem(r.memMb));
    out.insert(QStringLiteral("cpuText"), Summary::formatCpu(r.cpuPercent));
    return out;
}

QStringList AppController::activeInstanceNames() const {
    QStringList names;
    for (const Instance &item : m_instances.items()) {
        if (BatchPlan::isActiveState(item.status))
            names.append(item.name);
    }
    return names;
}

void AppController::stopActiveAndQuit() {
    if (m_quitting)
        return;
    m_quitting = true;
    // 先取消进行中的批量，避免退出过程中又发起新的启动
    if (m_batch.busy())
        m_batch.cancel();

    QStringList ids;
    for (const Instance &item : m_instances.items()) {
        if (BatchPlan::isActiveState(item.status))
            ids.append(item.id);
    }
    // stopInstance 同时取消自动重启计划；停止中的再次 stop 无副作用
    for (const QString &id : std::as_const(ids))
        stopInstance(id);

    auto remaining = [this, ids]() {
        QStringList left;
        for (const QString &id : ids) {
            if (BatchPlan::isActiveState(m_instances.item(id).status))
                left.append(id);
        }
        return left;
    };
    if (remaining().isEmpty()) {
        QCoreApplication::quit();
        return;
    }

    // 每 200ms 检查一次；10 秒到期仍未停止的强制结束进程树后退出
    auto *poll = new QTimer(this);
    poll->setInterval(200);
    auto *deadline = new QTimer(this);
    deadline->setSingleShot(true);
    connect(poll, &QTimer::timeout, this, [remaining]() {
        if (remaining().isEmpty())
            QCoreApplication::quit();
    });
    connect(deadline, &QTimer::timeout, this, [this, poll, remaining]() {
        poll->stop();
        for (const QString &id : remaining()) {
            qWarning().noquote() << QStringLiteral("[Quit]") << m_instances.item(id).name
                                 << QStringLiteral("10 秒内未停止，强制结束进程树");
            m_supervisor.forceKill(id);
        }
        QCoreApplication::quit();
    });
    poll->start();
    deadline->start(10000);
}
