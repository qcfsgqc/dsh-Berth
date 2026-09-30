#include "EnvChecker.h"

#include "ProcessRunner.h"
#include "core/Redact.h"
#include "core/SemVer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <string>

namespace {

constexpr int kCheckTimeoutMs = 10 * 1000;
constexpr int kInstallTimeoutMs = 30 * 60 * 1000;
constexpr int kInstallTailLines = 20;

const QString kNodeRange = QStringLiteral("^22.19.0 || >=24.0.0");
const QString kWebView2Guid = QStringLiteral("{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}");

QString statusLabel(const QString &status) {
    if (status == QLatin1String("checking"))
        return QStringLiteral("检测中");
    if (status == QLatin1String("ok"))
        return QStringLiteral("可用");
    if (status == QLatin1String("missing"))
        return QStringLiteral("缺失");
    if (status == QLatin1String("outdated"))
        return QStringLiteral("版本过低");
    if (status == QLatin1String("failed"))
        return QStringLiteral("检测失败");
    return QStringLiteral("未检测");
}

QString expandEnv(const QString &text) {
#ifdef Q_OS_WIN
    const std::wstring in = text.toStdWString();
    const DWORD n = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (n == 0)
        return text;
    std::wstring out(n, L'\0');
    if (ExpandEnvironmentStringsW(in.c_str(), out.data(), n) == 0)
        return text;
    return QString::fromWCharArray(out.c_str());
#else
    return text;
#endif
}

// 注册表里最新的系统 + 用户 PATH：winget 刚装完的工具不在 Berth 进程的 PATH 里，重测时补查
QStringList registryPathDirs() {
    QStringList dirs;
#ifdef Q_OS_WIN
    const QStringList keys = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"),
        QStringLiteral("HKEY_CURRENT_USER\\Environment"),
    };
    for (const QString &key : keys) {
        const QSettings reg(key, QSettings::NativeFormat);
        const QString value = expandEnv(reg.value(QStringLiteral("Path")).toString());
        for (const QString &d : value.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString dir = QDir::cleanPath(d.trimmed());
            if (!dir.isEmpty() && !dirs.contains(dir, Qt::CaseInsensitive))
                dirs.append(dir);
        }
    }
#endif
    return dirs;
}

// 先查当前 PATH，再查注册表中的最新 PATH
QString findTool(const QString &name) {
    QString found = QStandardPaths::findExecutable(name);
    if (found.isEmpty()) {
        const QStringList dirs = registryPathDirs();
        if (!dirs.isEmpty())
            found = QStandardPaths::findExecutable(name, dirs);
    }
    return found.isEmpty() ? QString() : QDir::toNativeSeparators(found);
}

// 用户设置的可执行文件：是现存文件就直接用，否则按名称在 PATH 中找
QString resolveConfigured(const QString &configured, const QString &fallbackName) {
    const QString exe = configured.trimmed();
    if (exe.isEmpty())
        return findTool(fallbackName);
    const QFileInfo fi(exe);
    if (fi.isFile())
        return QDir::toNativeSeparators(fi.absoluteFilePath());
    if (exe.contains(QLatin1Char('/')) || exe.contains(QLatin1Char('\\')))
        return QString();
    return findTool(exe);
}

// "v22.19.0" / "git version 2.45.1.windows.1" / "10.12.1" → "22.19.0"（缺补丁号补 0）
QString extractVersion(const QString &text) {
    static const QRegularExpression re(QStringLiteral("(\\d+)\\.(\\d+)(?:\\.(\\d+))?"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return QString();
    const QString patch = m.captured(3).isEmpty() ? QStringLiteral("0") : m.captured(3);
    return QStringLiteral("%1.%2.%3")
        .arg(m.captured(1).toULongLong())
        .arg(m.captured(2).toULongLong())
        .arg(patch.toULongLong());
}

// WebView2 Runtime：EdgeUpdate Clients 的 pv（HKLM WOW6432Node / HKLM / HKCU）
QString webView2Version(QString *where) {
#ifdef Q_OS_WIN
    const QStringList keys = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\") + kWebView2Guid,
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\") + kWebView2Guid,
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\EdgeUpdate\\Clients\\") + kWebView2Guid,
    };
    for (const QString &key : keys) {
        const QSettings reg(key, QSettings::NativeFormat);
        const QString pv = reg.value(QStringLiteral("pv")).toString().trimmed();
        if (!pv.isEmpty() && pv != QLatin1String("0.0.0.0")) {
            if (where)
                *where = key;
            return pv;
        }
    }
#else
    Q_UNUSED(where);
#endif
    return QString();
}

QString windowsVersion() {
    const QString pretty = QSysInfo::prettyProductName();
#ifdef Q_OS_WIN
    using RtlGetVersionFn = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        const auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")));
        RTL_OSVERSIONINFOW info{};
        info.dwOSVersionInfoSize = sizeof(info);
        if (fn && fn(&info) == 0) {
            return QStringLiteral("%1.%2 build %3（%4）")
                .arg(info.dwMajorVersion)
                .arg(info.dwMinorVersion)
                .arg(info.dwBuildNumber)
                .arg(pretty);
        }
    }
#endif
    return pretty;
}

// 代理 URL 中的 user:pass@ 与 token 查询参数一律掩码
QString redactPatterns(QString text) {
    static const QRegularExpression cred(
        QStringLiteral("([A-Za-z][A-Za-z0-9+.-]*://)[^/\\s@]+@"));
    text.replace(cred, QStringLiteral("\\1***@"));
    static const QRegularExpression token(
        QStringLiteral("((?:token|access_token|password|passwd|pwd)=)[^&\\s]+"),
        QRegularExpression::CaseInsensitiveOption);
    text.replace(token, QStringLiteral("\\1***"));
    return text;
}

} // namespace

EnvChecker::EnvChecker(ProcessRunner *runner, QObject *parent) : QObject(parent), m_runner(runner) {
    auto add = [this](const QString &id, const QString &name, const QString &minRange,
                      const QString &minText, const QString &url, const QString &guide,
                      const QString &wingetId) {
        Item item;
        item.id = id;
        item.name = name;
        item.minRange = minRange;
        item.minText = minText;
        item.url = url;
        item.guide = guide;
        item.wingetId = wingetId;
        m_items.append(item);
    };
    add(QStringLiteral("node"), QStringLiteral("Node.js"), kNodeRange,
        QStringLiteral("22.19.0（或 24.0.0 及以上）"), QStringLiteral("https://nodejs.org/zh-cn/download"),
        QStringLiteral("安装 Node.js LTS（22.19 及以上，或 24 及以上），安装后重新检测。"),
        QStringLiteral("OpenJS.NodeJS.LTS"));
    add(QStringLiteral("npm"), QStringLiteral("npm"), QString(), QString(),
        QStringLiteral("https://docs.npmjs.com/downloading-and-installing-node-js-and-npm"),
        QStringLiteral("npm 随 Node.js 一起安装；重新安装 Node.js 即可恢复。"), QString());
    add(QStringLiteral("pnpm"), QStringLiteral("pnpm"), QString(), QString(),
        QStringLiteral("https://pnpm.io/zh/installation"),
        QStringLiteral("在终端运行 npm install -g pnpm（或 corepack enable pnpm）。"),
        QStringLiteral("pnpm.pnpm"));
    add(QStringLiteral("git"), QStringLiteral("Git"), QString(), QString(),
        QStringLiteral("https://git-scm.com/download/win"),
        QStringLiteral("下载 Git for Windows 安装包，安装时保留\"添加到 PATH\"选项。"),
        QStringLiteral("Git.Git"));
    add(QStringLiteral("dsh"), QStringLiteral("dsh（系统）"), QString(), QString(),
        QStringLiteral("https://www.npmjs.com/package/@deepseek-ai/dsh"),
        QStringLiteral("在终端运行 npm install -g @deepseek-ai/dsh，或在设置里指定 dsh 可执行文件。"),
        QString());
    add(QStringLiteral("webview2"), QStringLiteral("WebView2 Runtime"), QString(), QString(),
        QStringLiteral("https://developer.microsoft.com/zh-cn/microsoft-edge/webview2/"),
        QStringLiteral("下载并运行 Evergreen Bootstrapper 安装 WebView2 Runtime。"),
        QStringLiteral("Microsoft.EdgeWebView2Runtime"));
}

void EnvChecker::setOps(Ops ops) {
    m_ops = std::move(ops);
}

int EnvChecker::indexOf(const QString &id) const {
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].id == id)
            return i;
    }
    return -1;
}

QVariantList EnvChecker::items() const {
    QVariantList out;
    for (const Item &it : m_items) {
        const QString cmd = installCommand(it.id);
        QVariantMap m;
        m.insert(QStringLiteral("id"), it.id);
        m.insert(QStringLiteral("name"), it.name);
        m.insert(QStringLiteral("status"), it.status);
        m.insert(QStringLiteral("statusText"), statusLabel(it.status));
        m.insert(QStringLiteral("version"), it.version);
        m.insert(QStringLiteral("path"), it.path);
        m.insert(QStringLiteral("minVersion"), it.minText);
        m.insert(QStringLiteral("reason"), it.reason);
        m.insert(QStringLiteral("url"), it.url);
        m.insert(QStringLiteral("guide"), it.guide);
        m.insert(QStringLiteral("wingetId"), it.wingetId);
        m.insert(QStringLiteral("installCommand"), cmd);
        m.insert(QStringLiteral("canInstall"), !cmd.isEmpty() && !it.installing
                                                   && it.status == QLatin1String("missing"));
        m.insert(QStringLiteral("installing"), it.installing);
        m.insert(QStringLiteral("hasInstallResult"), it.hasInstallResult);
        m.insert(QStringLiteral("installOk"), it.installOk);
        m.insert(QStringLiteral("installCode"), it.installCode);
        m.insert(QStringLiteral("installMessage"), it.installMessage);
        out.append(m);
    }
    return out;
}

bool EnvChecker::checkedOnce() const {
    for (const Item &it : m_items) {
        if (it.status == QLatin1String("unchecked"))
            return false;
    }
    return true;
}

void EnvChecker::checkAll() {
    for (int i = 0; i < m_items.size(); ++i)
        startCheck(i);
    detectWinget();
    emit itemsChanged();
    updateChecking();
}

void EnvChecker::checkItem(const QString &id) {
    const int i = indexOf(id);
    if (i < 0)
        return;
    startCheck(i);
    emit itemsChanged();
    updateChecking();
}

QString EnvChecker::resolveProgram(const Item &item) const {
    if (item.id == QLatin1String("dsh"))
        return resolveConfigured(m_ops.dshExecutable ? m_ops.dshExecutable() : QString(),
                                 QStringLiteral("dsh"));
    if (item.id == QLatin1String("node"))
        return resolveConfigured(m_ops.nodeExecutable ? m_ops.nodeExecutable() : QString(),
                                 QStringLiteral("node"));
    return findTool(item.id);
}

void EnvChecker::startCheck(int index) {
    Item &it = m_items[index];
    const int gen = ++it.generation;
    it.version.clear();
    it.path.clear();
    it.reason.clear();

    if (it.id == QLatin1String("webview2")) {
        QString where;
        const QString pv = webView2Version(&where);
        if (pv.isEmpty()) {
            it.status = QStringLiteral("missing");
            it.reason = QStringLiteral("注册表中未找到 WebView2 Runtime 的版本信息");
        } else {
            it.status = QStringLiteral("ok");
            it.version = pv;
            it.path = where;
        }
        return;
    }

    const QString program = resolveProgram(it);
    if (program.isEmpty() || !m_runner) {
        it.status = QStringLiteral("missing");
        it.reason = QStringLiteral("未找到 %1（不在 PATH 中，也未在设置中指定）").arg(it.name);
        return;
    }
    it.status = QStringLiteral("checking");
    it.path = program;

    ProcessOptions opts;
    opts.timeoutMs = kCheckTimeoutMs;
    opts.tailLines = 5;
    ProcessTask *task = m_runner->run(program, {QStringLiteral("--version")}, opts);
    const QString id = it.id;
    connect(task, &ProcessTask::finished, this,
            [this, id, gen, task](bool ok, int code, const QString &tail, bool timedOut) {
                onVersionResult(id, gen, ok, code, tail, timedOut, task->failedToStart(),
                                task->errorString());
            });
}

void EnvChecker::onVersionResult(const QString &id, int generation, bool ok, int code,
                                 const QString &tail, bool timedOut, bool failedToStart,
                                 const QString &error) {
    const int i = indexOf(id);
    if (i < 0 || m_items[i].generation != generation)
        return;
    Item &it = m_items[i];
    const QString lastLine = tail.section(QLatin1Char('\n'), -1).trimmed();

    if (timedOut) {
        it.status = QStringLiteral("failed");
        it.reason = QStringLiteral("检测超时（%1 秒未返回）").arg(kCheckTimeoutMs / 1000);
    } else if (failedToStart) {
        it.status = QStringLiteral("failed");
        it.reason = QStringLiteral("无法启动：%1").arg(error);
    } else if (!ok) {
        it.status = QStringLiteral("failed");
        it.reason = error.isEmpty() ? QStringLiteral("进程异常退出（退出码 %1）").arg(code)
                                    : QStringLiteral("进程异常退出：%1").arg(error);
        if (!lastLine.isEmpty())
            it.reason += QStringLiteral("：") + lastLine;
    } else {
        const QString ver = extractVersion(tail);
        if (ver.isEmpty()) {
            it.status = QStringLiteral("failed");
            it.reason = QStringLiteral("无法识别版本号：%1").arg(lastLine);
        } else {
            it.version = ver;
            const auto parsed = SemVer::parse(ver);
            if (!it.minRange.isEmpty() && parsed && !SemVer::satisfies(*parsed, it.minRange)) {
                it.status = QStringLiteral("outdated");
                it.reason = QStringLiteral("当前 %1，最低要求 %2").arg(ver, it.minText);
            } else {
                it.status = QStringLiteral("ok");
            }
        }
    }
    emit itemsChanged();
    updateChecking();
}

void EnvChecker::detectWinget() {
    const int gen = ++m_wingetGeneration;
    const QString path = findTool(QStringLiteral("winget"));
    if (path.isEmpty() || !m_runner) {
        m_wingetPath.clear();
        m_wingetAvailable = false;
        m_wingetVersion.clear();
        m_wingetChecking = false;
        emit wingetChanged();
        return;
    }
    m_wingetChecking = true;
    emit wingetChanged();
    ProcessOptions opts;
    opts.timeoutMs = kCheckTimeoutMs;
    opts.tailLines = 5;
    ProcessTask *task = m_runner->run(path, {QStringLiteral("--version")}, opts);
    connect(task, &ProcessTask::finished, this,
            [this, gen, path](bool ok, int, const QString &tail, bool) {
                if (gen != m_wingetGeneration)
                    return;
                const QString ver = ok ? extractVersion(tail) : QString();
                m_wingetChecking = false;
                m_wingetAvailable = !ver.isEmpty();
                m_wingetVersion = m_wingetAvailable ? tail.section(QLatin1Char('\n'), -1).trimmed()
                                                    : QString();
                m_wingetPath = m_wingetAvailable ? path : QString();
                emit wingetChanged();
                emit itemsChanged(); // canInstall 依赖 winget
            });
}

QStringList EnvChecker::wingetArgs(const QString &packageId) const {
    return {QStringLiteral("install"),
            QStringLiteral("--id"),
            packageId,
            QStringLiteral("--exact"),
            QStringLiteral("--source"),
            QStringLiteral("winget"),
            QStringLiteral("--accept-source-agreements"),
            QStringLiteral("--accept-package-agreements"),
            QStringLiteral("--disable-interactivity")};
}

QString EnvChecker::installCommand(const QString &id) const {
    const int i = indexOf(id);
    if (i < 0 || !m_wingetAvailable || m_wingetPath.isEmpty() || m_items[i].wingetId.isEmpty())
        return QString();
    return QStringLiteral("winget ") + wingetArgs(m_items[i].wingetId).join(QLatin1Char(' '));
}

QVariantMap EnvChecker::install(const QString &id) {
    auto fail = [](const QString &error) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    };
    const int i = indexOf(id);
    if (i < 0)
        return fail(QStringLiteral("未知的检测项：%1").arg(id));
    Item &it = m_items[i];
    if (it.installing)
        return fail(QStringLiteral("%1 正在安装").arg(it.name));
    if (it.status != QLatin1String("missing"))
        return fail(QStringLiteral("只有\"缺失\"的项可以一键安装"));
    if (installCommand(id).isEmpty())
        return fail(it.wingetId.isEmpty() ? QStringLiteral("%1 不支持通过 winget 安装").arg(it.name)
                                          : QStringLiteral("本机无法使用 winget"));

    ProcessOptions opts;
    opts.timeoutMs = kInstallTimeoutMs;
    opts.tailLines = kInstallTailLines;
    ProcessTask *task = m_runner->run(m_wingetPath, wingetArgs(it.wingetId), opts);
    it.installing = true;
    it.hasInstallResult = false;
    connect(task, &ProcessTask::finished, this,
            [this, id, task](bool ok, int code, const QString &tail, bool timedOut) {
                const int idx = indexOf(id);
                if (idx < 0)
                    return;
                Item &item = m_items[idx];
                item.installing = false;
                item.hasInstallResult = true;
                item.installOk = ok;
                item.installCode = code;
                if (ok) {
                    item.installMessage = QStringLiteral("%1 安装完成").arg(item.name);
                } else {
                    QString head;
                    if (timedOut)
                        head = QStringLiteral("winget 安装超时");
                    else if (task->failedToStart())
                        head = QStringLiteral("无法启动 winget：%1").arg(task->errorString());
                    else
                        head = QStringLiteral("winget 退出码 %1").arg(code);
                    item.installMessage = tail.isEmpty() ? head : head + QLatin1Char('\n') + tail;
                }
                const QString message = item.installMessage;
                emit installFinished(id, ok, code, message);
                // 只重测该项（checkItem 内发 itemsChanged）
                checkItem(id);
            });
    emit itemsChanged();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()}};
}

void EnvChecker::updateChecking() {
    bool any = false;
    for (const Item &it : m_items) {
        if (it.status == QLatin1String("checking"))
            any = true;
    }
    if (any != m_checking) {
        m_checking = any;
        emit checkingChanged();
    }
    if (!any && !m_pendingReport.isEmpty()) {
        const QString path = m_pendingReport;
        writeReport(path);
    }
}

QVariantMap EnvChecker::exportReport(const QString &path) {
    QString local = path.trimmed();
    const QUrl url(local);
    if (url.isLocalFile())
        local = url.toLocalFile();
    if (local.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("未选择保存位置")}, {QStringLiteral("pending"), false}};
    if (!m_pendingReport.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("正在导出诊断报告")}, {QStringLiteral("pending"), false}};

    m_pendingReport = local;
    emit exportingChanged();
    const bool needCheck = !checkedOnce();
    if (needCheck)
        checkAll(); // 检测都完成后由 updateChecking 写出
    else
        updateChecking(); // 正在检测则等其结束，否则立即写出
    return {{QStringLiteral("ok"), true}, {QStringLiteral("error"), QString()},
            {QStringLiteral("pending"), needCheck || m_checking}};
}

void EnvChecker::writeReport(const QString &path) {
    m_pendingReport.clear();
    emit exportingChanged();

    const QString text = Redact::redact(redactPatterns(buildReport()), collectSecrets());
    QSaveFile file(path);
    QString error;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        error = file.errorString();
    } else {
        const QByteArray bytes = text.toUtf8();
        if (file.write(bytes) != bytes.size()) {
            error = file.errorString();
            file.cancelWriting();
        } else if (!file.commit()) {
            error = file.errorString();
        }
    }
    const QString native = QDir::toNativeSeparators(path);
    if (error.isEmpty())
        emit reportExported(true, native, QStringLiteral("诊断报告已导出到 %1").arg(native));
    else
        emit reportExported(false, native, QStringLiteral("无法写入 %1：%2").arg(native, error));
}

QStringList EnvChecker::collectSecrets() const {
    QStringList out;
    if (m_ops.instances) {
        for (const Instance &inst : m_ops.instances()) {
            for (const InstanceEnvVar &v : inst.env) {
                if (v.secret && !v.value.isEmpty())
                    out.append(v.value);
            }
        }
    }
    if (m_ops.proxy) {
        const ProxySettings p = m_ops.proxy();
        if (!p.user.isEmpty())
            out.append(p.user);
        if (!p.passwordDpapi.isEmpty())
            out.append(p.passwordDpapi);
    }
    if (m_ops.extraSecrets)
        out.append(m_ops.extraSecrets());
    return out;
}

QString EnvChecker::buildReport() const {
    QStringList lines;
    const QDateTime now = QDateTime::currentDateTime();
#ifdef BERTH_VERSION
    const QString berthVersion = QStringLiteral(BERTH_VERSION);
#else
    const QString berthVersion = QCoreApplication::applicationVersion();
#endif
    lines << QStringLiteral("DSH Berth 诊断报告")
          << QStringLiteral("生成时间：%1").arg(now.toString(Qt::ISODate))
          << QStringLiteral("Berth 版本：%1").arg(berthVersion)
          << QStringLiteral("Windows 版本：%1").arg(windowsVersion())
          << QStringLiteral("Qt 版本：%1").arg(QString::fromLatin1(qVersion())) << QString();

    lines << QStringLiteral("[环境检测]");
    for (const Item &it : m_items) {
        QString line = QStringLiteral("- %1：%2").arg(it.name, statusLabel(it.status));
        if (!it.version.isEmpty())
            line += QStringLiteral("｜版本 %1").arg(it.version);
        if (!it.path.isEmpty())
            line += QStringLiteral("｜路径 %1").arg(it.path);
        if (!it.minText.isEmpty())
            line += QStringLiteral("｜最低要求 %1").arg(it.minText);
        if (!it.reason.isEmpty())
            line += QStringLiteral("｜%1").arg(it.reason);
        lines << line;
    }
    lines << (m_wingetAvailable ? QStringLiteral("- winget：可用｜%1").arg(m_wingetVersion)
                                : QStringLiteral("- winget：不可用"))
          << QString();

    lines << QStringLiteral("[代理设置]");
    if (m_ops.proxy) {
        const ProxySettings p = m_ops.proxy();
        if (p.mode == QLatin1String("manual")) {
            lines << QStringLiteral("模式：手动")
                  << QStringLiteral("地址：%1://%2:%3").arg(p.scheme, p.host, QString::number(p.port))
                  << QStringLiteral("用户名：%1").arg(p.user.isEmpty() ? QStringLiteral("未设置") : QStringLiteral("***"))
                  << QStringLiteral("密码：%1").arg(p.passwordDpapi.isEmpty() ? QStringLiteral("未设置") : QStringLiteral("***"));
        } else if (p.mode == QLatin1String("system")) {
            lines << QStringLiteral("模式：使用系统代理");
        } else {
            lines << QStringLiteral("模式：不使用代理");
        }
    } else {
        lines << QStringLiteral("（未提供）");
    }
    lines << QString();

    const QList<Instance> list = m_ops.instances ? m_ops.instances() : QList<Instance>();
    lines << QStringLiteral("[泊位摘要]（%1 个）").arg(list.size());
    for (const Instance &inst : list) {
        const QString status = m_ops.statusText ? m_ops.statusText(inst.status) : inst.status;
        QString exit = m_ops.lastExitCode ? m_ops.lastExitCode(inst.id) : QString();
        if (exit.isEmpty())
            exit = QStringLiteral("无");
        lines << QStringLiteral("- %1｜端口 %2｜状态 %3｜最近退出码 %4")
                     .arg(inst.name, QString::number(inst.port), status, exit);
    }
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}
