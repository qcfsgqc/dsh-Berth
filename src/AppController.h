#pragma once

#include "AutoRestarter.h"
#include "AutostartRegistry.h"
#include "BatchLauncher.h"
#include "BundleIO.h"
#include "DataStore.h"
#include "DshUpdate.h"
#include "DshVersionStore.h"
#include "EnvChecker.h"
#include "ExtensionManager.h"
#include "InstanceModel.h"
#include "LogService.h"
#include "PluginManager.h"
#include "Preflight.h"
#include "Quarantine.h"
#include "RestartHintController.h"

#include "PortChecker.h"
#include "ProcessMetrics.h"
#include "ProcessRunner.h"
#include "ProxyManager.h"
#include "ProfileTreeModel.h"
#include "Settings.h"
#include "StatsService.h"
#include "Supervisor.h"
#include "UpdateCenter.h"
#include "WorkspaceOpener.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

// 启动来源：手动、重启、批量、自动重启、开机自启都走同一条启动流水线
enum class LaunchReason { Manual, Restart, Batch, AutoRestart, Autostart };

// 启动流水线结果：ok 为 false 即 LaunchRejected，reason 为拒绝原因
struct LaunchResult {
    bool ok = false;
    QString reason;
};

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(InstanceModel *instances READ instances CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString defaultHome READ defaultDshHome NOTIFY profilesChanged)
    Q_PROPERTY(QStringList knownProfiles READ knownProfiles NOTIFY profilesChanged)
    Q_PROPERTY(ProfileTreeModel *profileTree READ profileTree CONSTANT)
    Q_PROPERTY(bool profileBusy READ profileBusy NOTIFY profileBusyChanged)
    Q_PROPERTY(DshUpdate *dshUpdate READ dshUpdate CONSTANT)
    Q_PROPERTY(bool pluginBusy READ pluginBusy NOTIFY pluginBusyChanged)
    // instances.json 为只读模式（schema 过高或无法解析）时为 true，界面据此禁用保存
    Q_PROPERTY(bool instancesReadOnly READ instancesReadOnly CONSTANT)
    // 退避等待中的泊位：id → {sec, n, N}；卡片据此显示"将在 X 秒后重启（第 n/N 次）"
    Q_PROPERTY(QVariantMap restartCountdowns READ restartCountdowns NOTIFY restartCountdownsChanged)
    // 批量启停（启动/停止全部、所选，错峰自启）
    Q_PROPERTY(BatchLauncher *batch READ batch CONSTANT)
    // 开机自启 Berth（HKCU Run 启动项）
    Q_PROPERTY(AutostartRegistry *autostart READ autostart CONSTANT)
    // 日志页后端（异步读取尾部/分段、清空、导出、文件大小刷新）
    Q_PROPERTY(LogService *logs READ logs CONSTANT)
    // 插件页后端：(DSH_HOME, profile) 组合、启用开关、批量启用/禁用/卸载
    Q_PROPERTY(PluginManager *plugins READ plugins CONSTANT)
    // 插件变动后的重启提示条与"待重启"标记
    Q_PROPERTY(RestartHintController *restartHint READ restartHint CONSTANT)
    // 隔离记录（quarantine.json）：listFor(home, profile) → [{home, profile, name, at, reason, detail}]
    Q_PROPERTY(Quarantine *quarantine READ quarantine CONSTANT)
    // 多版本 dsh 仓库：versions / installing / install(ver) / remove(ver) / executableFor(ver)
    Q_PROPERTY(DshVersionStore *versions READ versions CONSTANT)
    // 泊位 / profile 整包导出与导入（inspect → importBundle、依赖未就绪与重试）
    Q_PROPERTY(BundleIO *bundles READ bundles CONSTANT)
    // 扩展页后端：load(home, profile, workspace) / mcpState() / setMcpEnabled(path, rowId, on)
    Q_PROPERTY(ExtensionManager *extensions READ extensions CONSTANT)
    // 环境页后端：items / checking / checkAll() / install(id) / exportReport(path)
    Q_PROPERTY(EnvChecker *env READ env CONSTANT)
    // 进程运行指标：current(id) / trend(id) / totals(id) / reset(id)；loadNotice
    Q_PROPERTY(ProcessMetrics *metrics READ metrics CONSTANT)
    // 代理与 npm 镜像：save(draft) / validate(draft) / detect() / test(draft) / candidates / testResults
    Q_PROPERTY(ProxyManager *proxy READ proxy CONSTANT)
    // 会话与用量统计后端
    Q_PROPERTY(StatsService *stats READ stats CONSTANT)
    // 更新中心：items / checking / upgrading / updateCount / checkAll() / upgrade(id) / upgradeAll()
    Q_PROPERTY(UpdateCenter *updates READ updates CONSTANT)
    // 在 VS Code / Cursor / 资源管理器中打开 workspace：availability(id) / open(id, target)
    Q_PROPERTY(WorkspaceOpener *workspaceOpener READ workspaceOpener CONSTANT)
    // 用量页（需求 22）：最近一次成功返回的结果（结构见 StatsService::UsageResult::toVariantMap），
    // 尚无结果时为空 map；单价变化时按新单价重算费用。读取期间 usageLoading 为 true，保留上次结果
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
    Q_PROPERTY(bool usageLoading READ usageLoading NOTIFY usageLoadingChanged)



public:
    explicit AppController(QObject *parent = nullptr);

    InstanceModel *instances();
    Settings *settings();
    QString version() const;

    Q_INVOKABLE QString createInstance();
    // 从已有泊位复制配置新建（端口自动避让，id/状态/日志路径重新生成）
    Q_INVOKABLE QString createInstanceFrom(const QString &sourceId);
    Q_INVOKABLE void removeInstance(const QString &id);
    // env：[{key, value, secret}]（保持行顺序）；extraArgs：额外启动参数（保持顺序）。
    // 端口非法/重复、环境变量键非法/重复或超上限时拒绝保存并返回 false（模型与 instances.json 不变）；
    // 额外参数与 --port/--profile 冲突只提示，照常保存。
    // dshVersion：绑定的 dsh 版本，空表示系统 dsh；未安装的版本号也照常保存（选择器标记"缺失"，启动时拒绝）
    Q_INVOKABLE bool updateInstance(const QString &id, const QString &name, int port,
                                    const QString &profile, const QString &dshHome,
                                    const QString &workspace, bool autostart, bool autoRestart,
                                    const QVariantList &env, const QStringList &extraArgs,
                                    const QString &dshVersion);
    // 编辑界面实时校验：{invalidRows, duplicateRows, dshHomeRows, conflicts, tooManyRows, tooManyArgs, ok}
    Q_INVOKABLE QVariantMap checkEnvTable(const QVariantList &env, const QStringList &extraArgs) const;
    // —— 图标、分组、标签、备注（需求 9）；以下校验均返回 {ok, error} ——
    // 保存：iconKind 为 none | builtin | file；file 时 iconValue 为已保存的相对路径（不变）
    // 或新选的本地路径/URL（校验后复制到 <dataDir>/icons/ 并保存相对路径）。校验失败时不改任何字段
    Q_INVOKABLE QVariantMap setInstanceMeta(const QString &id, const QString &iconKind,
                                            const QString &iconValue, const QString &group,
                                            const QStringList &tags, const QString &notes);
    Q_INVOKABLE QVariantMap checkInstanceMeta(const QString &group, const QStringList &tags,
                                              const QString &notes) const;
    Q_INVOKABLE QVariantMap checkTag(const QStringList &existing, const QString &tag) const;
    Q_INVOKABLE QVariantMap checkImage(const QString &pathOrUrl) const;
    // icons/ 下相对路径 → file URL；文件不存在时返回空串（界面显示默认图标）
    Q_INVOKABLE QString iconFileUrl(const QString &relPath) const;
    // 可折叠分组列表：[{name, ungrouped, count, items:[{id, name, port, profile, status, iconKind, iconValue, tags}]}]；
    // 按名称或标签过滤，隐藏空分组；keyword 为空时返回全部
    Q_INVOKABLE QVariantList groupedInstances(const QString &keyword) const;
    Q_INVOKABLE void startInstance(const QString &id);
    Q_INVOKABLE void stopInstance(const QString &id);
    Q_INVOKABLE void restartInstance(const QString &id);
    // —— 端口冲突对话框的三个选项（对应 portConflict 信号） ——
    // 取消启动：丢弃待处理冲突，状态与配置不变
    Q_INVOKABLE void cancelConflict(const QString &id);
    // 改用空闲端口：从当前端口 +1 起找第一个未监听且未被其他泊位配置的端口，保存后启动；找不到时报错
    Q_INVOKABLE void useFreePort(const QString &id);
    // 复用现有服务：不启动新进程，attachExternal 标记为"运行中（外部）"
    Q_INVOKABLE void reuseExisting(const QString &id);
    // 在内嵌 WebView2 窗口里打开（发 uiRequested 给 QML）
    Q_INVOKABLE void openUi(const QString &id);
    Q_INVOKABLE void openInBrowser(const QString &id);
    // 带 token 的界面地址；本次运行还没打印时退回 http://127.0.0.1:<port>/
    Q_INVOKABLE QString uiUrl(const QString &id) const;
    Q_INVOKABLE void openLog(const QString &id);
    // 读取日志尾部（默认 64KB），供终端面板显示
    Q_INVOKABLE QString readLog(const QString &id, int maxBytes = 65536) const;

    // 退出确认：处于 Active_State（启动中/运行中/停止中）的泊位名，按列表顺序
    Q_INVOKABLE QStringList activeInstanceNames() const;
    // 顶部汇总面板（需求 24）：{total, running, failed, memMb, cpuPercent, memText, cpuText}
    Q_INVOKABLE QVariantMap summary() const;
    // 停止并退出：取消批量、停止全部 Active_State 泊位，都停下后退出；
    // 10 秒后仍未停止的经 Supervisor::forceKill 强制结束进程树再退出
    Q_INVOKABLE void stopActiveAndQuit();

    Q_INVOKABLE QString statusText(const QString &status) const;
    Q_INVOKABLE QVariantMap instance(const QString &id) const;
    Q_INVOKABLE QString defaultDshHome() const;
    Q_INVOKABLE QStringList detectProfiles(const QString &dshHome) const;
    Q_INVOKABLE int importDetectedProfiles();
    // —— profile 管理 ——
    // profile 信息：{ok, error, name, dir, exists, bundles, pluginCount}
    Q_INVOKABLE QVariantMap profileInfo(const QString &home, const QString &name) const;
    // 从模板创建 profile（dsh --profile <n> --from-default-profile <t> --dump-config，boot-free），异步
    Q_INVOKABLE void createProfile(const QString &name, const QString &fromTemplate);
    // 删除 profile 目录（被任何泊位引用时拒绝）
    Q_INVOKABLE void deleteProfile(const QString &home, const QString &name);
    // 重命名 profile 目录并同步更新引用它的泊位
    Q_INVOKABLE void renameProfile(const QString &home, const QString &oldName, const QString &newName);
    // 复制配置文件 + pnpm install 重建依赖，异步
    Q_INVOKABLE void copyProfile(const QString &home, const QString &srcName, const QString &newName);
    // 为指定 profile 新建泊位（默认 home 时 dshHome 留空），返回新泊位 id
    Q_INVOKABLE QString createInstanceFor(const QString &home, const QString &profile);
    Q_INVOKABLE void openProfileDir(const QString &home, const QString &name);
    // 按 (home, profile) 直接读写插件，不经过泊位
    Q_INVOKABLE QVariantMap listPlugins(const QString &id) const;
    Q_INVOKABLE QVariantMap listPluginsForProfile(const QString &home, const QString &profile) const;
    Q_INVOKABLE void uninstallPluginForProfile(const QString &home, const QString &profile,
                                               const QString &name, bool removePackage);
    Q_INVOKABLE void uninstallPlugin(const QString &id, const QString &name, bool removePackage);
    // 解除隔离：先恢复启用（PluginManager::setEnabled，必要时触发 Restart_Hint），成功后删除 quarantine.json 中的记录。
    // 返回 {ok, error}
    Q_INVOKABLE QVariantMap unquarantine(const QString &home, const QString &profile, const QString &name);
    // 泊位详情用：该泊位 (home, profile) 下被隔离的插件 [{name, reason, reasonText, at, detail}]
    Q_INVOKABLE QVariantList quarantinedFor(const QString &id) const;
    QStringList knownProfiles() const;
    ProfileTreeModel *profileTree() { return &m_profileTree; }
    DshUpdate *dshUpdate() { return &m_dshUpdate; }
    bool profileBusy() const;
    bool pluginBusy() const;
    bool instancesReadOnly() const;
    QVariantMap restartCountdowns() const { return m_restartCountdowns; }
    BatchLauncher *batch() { return &m_batch; }
    AutostartRegistry *autostart() { return &m_autostart; }
    LogService *logs() { return &m_logs; }
    PluginManager *plugins() { return &m_plugins; }
    RestartHintController *restartHint() { return &m_restartHint; }
    Quarantine *quarantine() { return &m_quarantine; }
    DshVersionStore *versions() { return &m_versions; }
    BundleIO *bundles() { return &m_bundles; }
    ExtensionManager *extensions() { return &m_extensions; }
    EnvChecker *env() { return &m_env; }
    ProcessMetrics *metrics() { return &m_metrics; }
    ProxyManager *proxy() { return &m_proxy; }
    StatsService *stats() { return &m_stats; }
    UpdateCenter *updates() { return &m_updates; }
    WorkspaceOpener *workspaceOpener() { return &m_workspaceOpener; }
    QVariantMap usage() const { return m_usageMap; }
    bool usageLoading() const { return m_usageLoading; }
    // 按全部泊位重新读取用量：range 为 "today" | "7d" | "30d" | "all"
    Q_INVOKABLE void refreshUsage(const QString &range);
    // 会话统计（需求 21）：打开详情时读取一次；运行中的泊位另由 30 秒定时器刷新。
    // 结果经 stats.sessionStatsFor(id) 取，完成时发 stats.sessionStatsChanged(id)
    Q_INVOKABLE void refreshSessionStats(const QString &id);



    // 统一启动流水线：端口检查 → 版本可执行文件解析 → Preflight → LaunchEnv 组装 → Supervisor::start。
    // 任一步拒绝返回 ok=false 与原因，泊位保持"已停止"并提示原因。供 BatchLauncher / AutoRestarter 复用。
    LaunchResult launch(const QString &id, LaunchReason reason);

signals:
    void notice(const QString &message);
    void uiRequested(const QString &id, const QString &url);
    void profilesChanged();
    void profileBusyChanged();
    void profileOpFinished(bool ok, const QString &message, const QString &profileName);
    void pluginBusyChanged();
    void pluginUninstallFinished(const QString &id, bool ok, const QString &message);
    void restartCountdownsChanged();
    // 手动启动/重启时端口被占用：弹冲突对话框；取不到的字段已替换为"未知"
    void portConflict(const QString &id, const QString &name, int port, const QString &pid,
                      const QString &processName, const QString &processPath);
    // 冲突端口的 dsh 特征探测结果（3 秒内），isDsh 为 true 时对话框显示"复用现有服务"
    void conflictProbed(const QString &id, bool isDsh);
    // 泊位状态写入模型后发出（Notifier 据此判定"启动中→运行中"）
    void instanceStatusChanged(const QString &id, const QString &status);
    // 进程未经请求退出（转发 Supervisor::crashed）
    void instanceCrashed(const QString &id, int exitCode);
    // 自动重启达到上限，泊位已置为"崩溃已停止"
    void restartGaveUp(const QString &id, int attempts);
    // 本次启动前自检隔离了至少 1 个插件（每次启动至多 1 次）；summary 为"名称（原因）"列表，Notifier 据此发汇总通知
    void pluginsQuarantined(const QString &id, const QStringList &summary);
    void usageChanged();
    void usageLoadingChanged();

private:
    void load();
    void save();
    void applyStatus(const QString &id, const QString &status, qint64 pid, const QString &error);
    // AutoRestarter 倒计时 → m_restartCountdowns（sec <= 0 时移除）
    void updateCountdown(const QString &id, int sec, int n, int N);
    // BatchLauncher 的回调：泊位列表顺序、状态、启动流水线（Batch/Autostart）与停止
    BatchLauncher::Ops makeBatchOps();
    // RestartHintController 的回调：状态、名称、重启（停止后按批量方式走启动流水线，端口占用直接拒绝）
    RestartHintController::Ops makeRestartHintOps();
    // 自动重启达到上限：置为"崩溃已停止"，写日志并发 restartGaveUp（由 Notifier 发系统通知）
    void handleRestartGaveUp(const QString &id, int attempts);
    // 从日志里取本次运行 dsh web 打印的 URL（含 token），没有则返回空
    QString tokenUrl(const QString &id) const;
    // token 行可能比端口就绪晚一点，短暂轮询后再发 uiRequested
    void requestUi(const QString &id, int attempt);

    // —— 启动流水线各步，返回值为拒绝原因（无值表示通过） ——
    // 端口检查：PortChecker::check 查监听与占用进程；手动启动/重启被占用时发 portConflict 并记入
    // m_pendingConflicts（由对话框选项收尾），批量与自动重启直接拒绝
    std::optional<QString> checkPort(const Instance &item, LaunchReason reason);
    // 版本可执行文件解析：dshVersion 为空用 System_Dsh（Settings::resolvedDshExecutable），
    // 否则经 DshVersionStore::resolve 取版本目录中的 dsh；目录或可执行文件缺失时拒绝
    std::optional<QString> resolveExecutable(const Instance &item, QString *exe) const;
    // Preflight（preflightEnabled 开启时）：泊位置为"启动中"后异步自检/修复/隔离，
    // 完成后在 onPreflightDone 里继续 startProcess；package.json 无法读取时中止并置为"已停止"
    LaunchResult runPreflight(const Instance &item, const QString &exe);
    void onPreflightDone(const QString &id, const QString &exe, const Preflight::Result &result);
    // LaunchEnv 组装 → Supervisor::start
    LaunchResult startProcess(const QString &id, const QString &exe);
    // 拒绝时的统一收尾：泊位保持"已停止"，记录错误并提示
    LaunchResult rejectLaunch(const QString &id, const QString &reason);

    QString resolveHome(const QString &dshHome) const;
    // 泊位 id → <DSH_HOME>/profiles/<profile>；找不到泊位时返回空并写 error
    QString profileDirFor(const QString &id, QString *error) const;
    void refreshProfiles();
    void setPluginBusy(bool busy);
    void setProfileBusy(bool busy);
    // profile 目录路径：home 不给时用默认 home
    QString profileDirForHome(const QString &home, const QString &profile) const;
    // 全量重建泊位树（home 归一 + 目录存在性 + knownProfiles 合并）
    void rebuildTree();
    // 卸载插件共用流程：id 为空表示按 (home, profile) 操作（不经泊位）
    void uninstallFromProfile(const QString &id, const QString &home, const QString &profile,
                              const QString &name, bool removePackage);
    // profile 包操作经 ProcessRunner 异步执行，收尾时给摘要并复位 busy
    void startProfileProcess(const QString &program, const QStringList &args, const QString &dshHome,
                             const QString &workingDir, const QString &failPrefix,
                             const QString &finishProfileName);

    // 放在最前：Settings 构造时经它读取 settings.json
    DataStore m_store;
    InstanceModel m_instances;
    Settings m_settings;
    Supervisor m_supervisor;
    // 须在 m_supervisor 之后构造（构造时连接其信号）、之前析构
    ProcessMetrics m_metrics;
    // 端口检查与外部进程监视；external 状态时开始 watchExternal，离开时 unwatch
    PortChecker m_portChecker;
    ProfileTreeModel m_profileTree;
    DshUpdate m_dshUpdate;
    // 须在 m_supervisor 之后构造（构造时连接其信号）
    AutoRestarter m_autoRestarter;
    BatchLauncher m_batch;
    AutostartRegistry m_autostart;
    LogService m_logs;
    // 共享的网络访问（插件目录拉取等）；须在 m_plugins 之前构造、之后析构
    QNetworkAccessManager m_network;
    // 须在 m_settings / m_network 之后构造（构造时读取代理设置并应用到 m_network）
    ProxyManager m_proxy;
    // 只保存 m_processRunner 的地址，构造时不使用
    PluginManager m_plugins;
    // 回调只捕获 this，构造时不使用其它成员
    RestartHintController m_restartHint;
    // 隔离记录经 m_store 读写；Preflight 只保存 m_processRunner / m_quarantine 的地址
    Quarantine m_quarantine;
    Preflight m_preflight;
    // 只保存 m_processRunner 的地址，构造时不使用；setOps 在构造函数体内注入
    DshVersionStore m_versions;
    // 只保存 m_processRunner 的地址，构造时不使用；setOps 在构造函数体内注入
    BundleIO m_bundles;
    // setOps 在构造函数体内注入
    ExtensionManager m_extensions;
    // 只保存 m_processRunner 的地址，构造时不使用；setOps 在构造函数体内注入
    EnvChecker m_env;
    // 只保存 m_processRunner 的地址，构造时不使用；任务经 QPointer 持有，析构顺序安全
    StatsService m_stats;
    // 只保存 m_processRunner / m_network 的地址，构造时不使用；setDeps 在构造函数体内注入
    UpdateCenter m_updates;
    // 只保存 m_instances 的地址（已在前面构造）
    WorkspaceOpener m_workspaceOpener{&m_instances};
    // 最近一次用量结果（单价变化时据此重算 m_usageMap）
    std::optional<StatsService::UsageResult> m_lastUsage;
    QVariantMap m_usageMap;
    bool m_usageLoading = false;
    int m_usageRequest = 0;

    // 正在启动前自检的泊位；停止/删除时移除，自检结束后不在其中则不再启动
    QSet<QString> m_preflightPending;

    QVariantMap m_restartCountdowns;
    // 等待冲突对话框选择的泊位：id → 占用者
    QHash<QString, Occupant> m_pendingConflicts;
    QStringList m_knownProfiles;
    QString m_profileFinishName;
    bool m_pluginBusy = false;
    bool m_profileBusy = false;
    bool m_quitting = false;
    // 放在最后：最先析构，先关闭全部 Job 杀掉后台进程树，再销毁其它成员
    ProcessRunner m_processRunner;
};
