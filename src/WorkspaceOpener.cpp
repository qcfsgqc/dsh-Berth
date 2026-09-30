#include "WorkspaceOpener.h"

#include "InstanceModel.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

struct EditorSpec {
    QString exeName;       // 实际的 GUI 可执行文件
    QString shimName;      // PATH 中的命令名（通常是 bin 下的 .cmd 包装）
    QString displayName;
    QStringList installDirs; // 常见安装目录（相对各根目录）
};

EditorSpec specFor(const QString &target) {
    if (target == QLatin1String("vscode"))
        return {QStringLiteral("Code.exe"), QStringLiteral("code"), QStringLiteral("VS Code"),
                {QStringLiteral("Microsoft VS Code")}};
    if (target == QLatin1String("cursor"))
        return {QStringLiteral("Cursor.exe"), QStringLiteral("cursor"), QStringLiteral("Cursor"),
                {QStringLiteral("cursor"), QStringLiteral("Cursor")}};
    return {};
}

QString displayNameFor(const QString &target) {
    if (target == QLatin1String("explorer"))
        return QStringLiteral("资源管理器");
    return specFor(target).displayName;
}

bool isFile(const QString &path) {
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile();
}

QVariantMap entry(bool enabled, const QString &reason) {
    return {{QStringLiteral("enabled"), enabled}, {QStringLiteral("reason"), reason}};
}

} // namespace

WorkspaceOpener::WorkspaceOpener(const InstanceModel *instances, QObject *parent)
    : QObject(parent), m_instances(instances) {}

QString WorkspaceOpener::checkWorkspace(const QString &workspace, QString *absPath) {
    const QString trimmed = workspace.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("未配置工作区");
    const QFileInfo fi(trimmed);
    if (!fi.exists())
        return QStringLiteral("工作区路径不存在：%1").arg(QDir::toNativeSeparators(trimmed));
    if (!fi.isDir())
        return QStringLiteral("工作区路径不是目录：%1").arg(QDir::toNativeSeparators(trimmed));
    if (absPath)
        *absPath = QDir::toNativeSeparators(QDir::cleanPath(fi.absoluteFilePath()));
    return {};
}

QString WorkspaceOpener::findEditor(const QString &target) {
    const EditorSpec spec = specFor(target);
    if (spec.exeName.isEmpty())
        return {};

    // 1) 常见安装路径：用户级（%LOCALAPPDATA%\Programs）与系统级（Program Files）
    QStringList roots;
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty())
        roots << local + QStringLiteral("/Programs");
    for (const char *var : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty() && !roots.contains(v))
            roots << v;
    }
    for (const QString &root : roots)
        for (const QString &dir : spec.installDirs) {
            const QString candidate = root + QLatin1Char('/') + dir + QLatin1Char('/') + spec.exeName;
            if (isFile(candidate))
                return QDir::toNativeSeparators(candidate);
        }

    // 2) PATH：命中的通常是 <安装目录>/bin/code.cmd 或 <安装目录>/resources/app/bin/cursor.cmd，
    //    向上最多找 4 层定位 GUI 可执行文件（直接启动 exe，避免经 cmd 转义空格与非 ASCII 路径）
    const QString shim = QStandardPaths::findExecutable(spec.shimName);
    if (!shim.isEmpty()) {
        const QFileInfo shimInfo(shim);
        if (shimInfo.fileName().compare(spec.exeName, Qt::CaseInsensitive) == 0)
            return QDir::toNativeSeparators(shimInfo.absoluteFilePath());
        QDir dir = shimInfo.absoluteDir();
        for (int i = 0; i < 4; ++i) {
            const QString candidate = dir.filePath(spec.exeName);
            if (isFile(candidate))
                return QDir::toNativeSeparators(candidate);
            if (!dir.cdUp())
                break;
        }
    }
    const QString exeOnPath = QStandardPaths::findExecutable(spec.exeName);
    if (!exeOnPath.isEmpty())
        return QDir::toNativeSeparators(exeOnPath);
    return {};
}

QVariantMap WorkspaceOpener::availability(const QString &id) const {
    const QString workspace = m_instances ? m_instances->item(id).workspace : QString();
    const QString pathError = checkWorkspace(workspace, nullptr);

    QVariantMap result;
    for (const QString &target : {QStringLiteral("vscode"), QStringLiteral("cursor")}) {
        if (findEditor(target).isEmpty())
            result.insert(target, entry(false, QStringLiteral("未检测到 %1").arg(displayNameFor(target))));
        else if (!pathError.isEmpty())
            result.insert(target, entry(false, pathError));
        else
            result.insert(target, entry(true, {}));
    }
    result.insert(QStringLiteral("explorer"), entry(pathError.isEmpty(), pathError));
    return result;
}

QString WorkspaceOpener::open(const QString &id, const QString &target) const {
    const QString name = displayNameFor(target);
    if (name.isEmpty())
        return QStringLiteral("未知的打开方式：%1").arg(target);

    const QString workspace = m_instances ? m_instances->item(id).workspace : QString();
    QString absPath;
    const QString pathError = checkWorkspace(workspace, &absPath);
    if (!pathError.isEmpty())
        return QStringLiteral("无法在 %1 中打开：%2").arg(name, pathError);

    QString program;
    if (target == QLatin1String("explorer")) {
        program = QStringLiteral("explorer.exe");
    } else {
        program = findEditor(target);
        if (program.isEmpty())
            return QStringLiteral("启动 %1 失败：未检测到 %1").arg(name);
    }

    // 参数数组形式启动，Qt 负责按 Windows 规则加引号；不等待外部程序退出
    if (!QProcess::startDetached(program, {absPath}, absPath))
        return QStringLiteral("启动 %1 失败：%2").arg(name, QDir::toNativeSeparators(program));
    return {};
}
