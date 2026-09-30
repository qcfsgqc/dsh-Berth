#include "DshVersionStore.h"
#include "ProcessRunner.h"
#include "core/SemVer.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace {
const QString kVersionsDir = QStringLiteral("dsh-versions");
const QString kPartialSuffix = QStringLiteral(".partial");
const QString kPackage = QStringLiteral("@deepseek-ai/dsh");
// npm 下载 + 安装依赖可能较慢，给足时间
constexpr int kInstallTimeoutMs = 15 * 60 * 1000;

#ifdef Q_OS_WIN
const QString kExeName = QStringLiteral("dsh.cmd");
#else
const QString kExeName = QStringLiteral("bin/dsh");
#endif

// SemVer 合法时返回规范文本（去掉 v 前缀），否则返回空。
// 目录名只由规范文本构成，SemVer 字符集不含路径分隔符，天然防止路径穿越
QString canonical(const QString &version) {
    const auto v = SemVer::parse(version.trimmed());
    return v ? v->toString() : QString();
}
} // namespace

DshVersionStore::DshVersionStore(ProcessRunner *runner, QObject *parent)
    : QObject(parent), m_runner(runner) {}

void DshVersionStore::setOps(Ops ops) {
    m_ops = std::move(ops);
    refresh();
}

QString DshVersionStore::rootDir() const {
    const QString data = m_ops.dataDir ? m_ops.dataDir() : QString();
    return data.isEmpty() ? QString() : QDir(data).filePath(kVersionsDir);
}

QString DshVersionStore::versionDir(const QString &version) const {
    const QString root = rootDir();
    const QString ver = canonical(version);
    if (root.isEmpty() || ver.isEmpty())
        return {};
    return QDir::toNativeSeparators(QDir(root).filePath(ver));
}

QString DshVersionStore::executableFor(const QString &version) const {
    const QString dir = versionDir(version);
    if (dir.isEmpty())
        return {};
    const QString exe = QDir::toNativeSeparators(QDir(dir).filePath(kExeName));
    return QFileInfo(exe).isFile() ? exe : QString();
}

std::optional<QString> DshVersionStore::resolve(const QString &version, QString *exe) const {
    const QString dir = versionDir(version);
    if (dir.isEmpty())
        return QStringLiteral("dsh 版本 %1 无效（不是合法的 SemVer 版本号），未启动").arg(version);
    if (!QFileInfo(dir).isDir())
        return QStringLiteral("dsh 版本 %1 缺失：版本目录不存在（%2），未启动").arg(version, dir);
    const QString path = executableFor(version);
    if (path.isEmpty())
        return QStringLiteral("dsh 版本 %1 缺失：目录中找不到 dsh 可执行文件（%2），未启动")
            .arg(version, QDir::toNativeSeparators(QDir(dir).filePath(kExeName)));
    *exe = path;
    return std::nullopt;
}

bool DshVersionStore::isInstalled(const QString &version) const {
    const QString ver = canonical(version);
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&ver](const Entry &e) { return e.version == ver; });
}

QStringList DshVersionStore::referencingInstances(const QString &version) const {
    const QString ver = canonical(version);
    QStringList names;
    if (ver.isEmpty() || !m_ops.instances)
        return names;
    for (const Instance &item : m_ops.instances()) {
        if (!item.dshVersion.isEmpty() && canonical(item.dshVersion) == ver)
            names.append(item.name);
    }
    return names;
}

QVariantList DshVersionStore::versions() const {
    QVariantList out;
    out.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        QVariantMap m;
        m.insert(QStringLiteral("version"), e.version);
        m.insert(QStringLiteral("path"), e.path);
        m.insert(QStringLiteral("refCount"), e.refCount);
        m.insert(QStringLiteral("installing"), false);
        out.append(m);
    }
    return out;
}

QHash<QString, int> DshVersionStore::countReferences() const {
    QHash<QString, int> counts;
    if (!m_ops.instances)
        return counts;
    for (const Instance &item : m_ops.instances()) {
        const QString ver = canonical(item.dshVersion);
        if (!ver.isEmpty())
            counts[ver] += 1;
    }
    return counts;
}

void DshVersionStore::refresh() {
    QList<Entry> entries;
    const QString root = rootDir();
    if (!root.isEmpty()) {
        const QFileInfoList dirs = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : dirs) {
            // .partial 等非规范名字（安装中或残留）不算已安装
            const QString name = fi.fileName();
            if (canonical(name) != name)
                continue;
            Entry e;
            e.version = name;
            e.path = QDir::toNativeSeparators(fi.absoluteFilePath());
            entries.append(e);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return SemVer::compare(*SemVer::parse(a.version), *SemVer::parse(b.version)) > 0;
    });
    const QHash<QString, int> counts = countReferences();
    for (Entry &e : entries)
        e.refCount = counts.value(e.version);
    m_entries = entries;
    emit versionsChanged();
}

void DshVersionStore::recountReferences() {
    const QHash<QString, int> counts = countReferences();
    bool changed = false;
    for (Entry &e : m_entries) {
        const int n = counts.value(e.version);
        if (e.refCount != n) {
            e.refCount = n;
            changed = true;
        }
    }
    if (changed)
        emit versionsChanged();
}

QVariantMap DshVersionStore::install(const QString &version) {
    const auto fail = [&version](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false},
                           {QStringLiteral("error"), error},
                           {QStringLiteral("version"), version}};
    };
    const QString ver = canonical(version);
    if (ver.isEmpty())
        return fail(QStringLiteral("版本号 %1 不是合法的 SemVer 2.0 完整版本号").arg(version));
    if (m_installing.contains(ver))
        return fail(QStringLiteral("dsh %1 正在安装中").arg(ver));
    const QString root = rootDir();
    if (root.isEmpty() || !m_runner)
        return fail(QStringLiteral("内部错误：版本仓库未初始化"));
    const QString finalDir = QDir(root).filePath(ver);
    if (isInstalled(ver) || QFileInfo::exists(finalDir))
        return fail(QStringLiteral("dsh %1 已安装").arg(ver));
    const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
    if (npm.isEmpty())
        return fail(QStringLiteral("未找到 npm，无法安装 dsh %1").arg(ver));

    // 清掉上次中断遗留的不完整目录，再重新创建
    const QString partial = finalDir + kPartialSuffix;
    QDir(partial).removeRecursively();
    if (!QDir().mkpath(partial))
        return fail(QStringLiteral("无法创建安装目录：%1").arg(QDir::toNativeSeparators(partial)));

    m_retryMirror.remove(ver);
    startNpm(ver, npm, partial, QString());
    emit installingChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()},
            {QStringLiteral("version"), ver}};
}

void DshVersionStore::startNpm(const QString &ver, const QString &npm, const QString &partial,
                               const QString &registry) {
    ProcessOptions options;
    options.timeoutMs = kInstallTimeoutMs;
    options.tailLines = 20;
    // 镜像兜底重试：覆盖 ProxyManager 注入的 registry（ProcessOptions.env 优先级最高）
    if (!registry.isEmpty())
        options.env.insert(QStringLiteral("npm_config_registry"), registry);
    ProcessTask *task = m_runner->run(npm,
                                      {QStringLiteral("install"), QStringLiteral("-g"),
                                       QStringLiteral("--prefix"), QDir::toNativeSeparators(partial),
                                       kPackage + QLatin1Char('@') + ver},
                                      options);
    m_installing.insert(ver, task);
    connect(task, &ProcessTask::finished, this,
            [this, ver, task](bool ok, int code, const QString &tail, bool timedOut) {
                onInstallFinished(ver, task, ok, code, tail, timedOut);
            });
}

void DshVersionStore::onInstallFinished(const QString &version, ProcessTask *task, bool ok,
                                        int code, const QString &tail, bool timedOut) {
    const QString root = rootDir();
    const QString finalDir = QDir(root).filePath(version);
    const QString partial = finalDir + kPartialSuffix;

    if (!ok) {
        // 镜像兜底：官方源失败（非启动失败）且开启兜底时，用镜像重试且只重试一次
        const QString mirror = m_ops.fallbackRegistry ? m_ops.fallbackRegistry() : QString();
        const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
        if (!task->failedToStart() && !mirror.isEmpty() && !npm.isEmpty()
            && !m_retryMirror.contains(version)) {
            m_retryMirror.insert(version, mirror);
            QDir(partial).removeRecursively();
            if (QDir().mkpath(partial)) {
                startNpm(version, npm, partial, mirror);
                return;
            }
        }
        QString reason;
        if (timedOut)
            reason = QStringLiteral("安装超时");
        else if (task->failedToStart())
            reason = QStringLiteral("无法运行 npm：") + task->errorString();
        else if (!task->errorString().isEmpty())
            reason = task->errorString();
        else
            reason = QStringLiteral("npm 退出码 %1").arg(code);
        if (m_retryMirror.contains(version))
            reason += QStringLiteral("（官方源与镜像 %1 都已尝试）").arg(m_retryMirror.value(version));
        QDir(partial).removeRecursively();
        finishInstall(version, false,
                      QStringLiteral("dsh %1 安装失败：%2").arg(version, reason)
                          + (tail.isEmpty() ? QString() : QStringLiteral("\n") + tail));
        return;
    }
    if (!QFileInfo(QDir(partial).filePath(kExeName)).isFile()) {
        QDir(partial).removeRecursively();
        finishInstall(version, false,
                      QStringLiteral("dsh %1 安装失败：安装后找不到 dsh 可执行文件（%2）")
                          .arg(version, kExeName)
                          + (tail.isEmpty() ? QString() : QStringLiteral("\n") + tail));
        return;
    }
    if (!QDir().rename(partial, finalDir)) {
        QDir(partial).removeRecursively();
        finishInstall(version, false,
                      QStringLiteral("dsh %1 安装失败：无法把安装目录改名为 %2")
                          .arg(version, QDir::toNativeSeparators(finalDir)));
        return;
    }
    finishInstall(version, true,
                  QStringLiteral("dsh %1 已安装到 %2").arg(version, QDir::toNativeSeparators(finalDir)));
}

void DshVersionStore::finishInstall(const QString &version, bool ok, const QString &message) {
    m_installing.remove(version);
    m_retryMirror.remove(version);
    emit installingChanged();
    refresh();
    emit installFinished(version, ok, message);
}

QVariantMap DshVersionStore::remove(const QString &version) {
    const auto result = [](bool ok, const QString &error, const QStringList &names = {}) {
        return QVariantMap{{QStringLiteral("ok"), ok},
                           {QStringLiteral("error"), error},
                           {QStringLiteral("instances"), names}};
    };
    const QString ver = canonical(version);
    if (ver.isEmpty())
        return result(false, QStringLiteral("版本号 %1 无效").arg(version));
    if (m_installing.contains(ver))
        return result(false, QStringLiteral("dsh %1 正在安装中，不能删除").arg(ver));
    if (!isInstalled(ver))
        return result(false, QStringLiteral("dsh %1 未安装").arg(ver));
    const QStringList names = referencingInstances(ver);
    if (!names.isEmpty())
        return result(false,
                      QStringLiteral("dsh %1 仍被以下泊位使用，不能删除：%2")
                          .arg(ver, names.join(QStringLiteral("、"))),
                      names);

    // 先改名再删：改名是原子的，失败时目录保持不变；删除中途失败也不会留下半个\"已安装\"版本
    const QString dir = QDir(rootDir()).filePath(ver);
    const QString trash = dir + QStringLiteral(".removing");
    QDir(trash).removeRecursively();
    if (!QDir().rename(dir, trash))
        return result(false, QStringLiteral("无法删除 dsh %1：目录可能正被占用（%2）")
                                 .arg(ver, QDir::toNativeSeparators(dir)));
    refresh();
    QDir(trash).removeRecursively();
    return result(true, {});
}
