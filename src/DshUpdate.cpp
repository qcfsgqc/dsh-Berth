#include "DshUpdate.h"
#include "Settings.h"

#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// 取输出末尾最多 maxLines 行（去掉空行），作为结果摘要（与 AppController 相同的做法）
QString tailLines(const QString &text, int maxLines) {
    QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

// 取第一个非空行（去首尾空白）；dsh --version 一般是纯文本一行
QString firstNonEmptyLine(const QString &text) {
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return {};
}

// npm view 的输出前后可能带更新提醒横幅，版本取最后一个非空行
QString lastNonEmptyLine(const QString &text) {
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r?\n")), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString trimmed = lines.at(i).trimmed();
        if (!trimmed.isEmpty())
            return trimmed;
    }
    return {};
}
}

DshUpdate::DshUpdate(QObject *parent) : QObject(parent) {}

void DshUpdate::setSettings(Settings *settings) { m_settings = settings; }

bool DshUpdate::busy() const { return m_busy; }

QString DshUpdate::installedVersion() const { return m_installedVersion; }

QString DshUpdate::latestVersion() const { return m_latestVersion; }

void DshUpdate::checkInstalled() {
    if (!m_settings) {
        reportCheckInstalled(false, {}, QStringLiteral("内部错误：Settings 未注入"));
        return;
    }
    // dsh 路径交给设置解析（npm 全局安装只有 dsh.cmd，Settings 会先在 PATH 里找到）
    startTask(Task::CheckInstalled, m_settings->resolvedDshExecutable(),
              { QStringLiteral("--version") });
}

void DshUpdate::checkLatest() {
    const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
    if (npm.isEmpty()) {
        emit checkLatestFinished(false, {}, QStringLiteral("未找到 npm，无法检查或更新"));
        return;
    }
    startTask(Task::CheckLatest, npm,
              { QStringLiteral("view"), QStringLiteral("@deepseek-ai/dsh"), QStringLiteral("version") });
}

void DshUpdate::updateDsh() {
    const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
    if (npm.isEmpty()) {
        emit updateFinished(false, QStringLiteral("未找到 npm，无法更新"));
        return;
    }
    startTask(Task::Update, npm,
              { QStringLiteral("install"), QStringLiteral("-g"),
                QStringLiteral("@deepseek-ai/dsh@latest") });
}

void DshUpdate::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void DshUpdate::setInstalledVersion(const QString &version) {
    if (m_installedVersion == version)
        return;
    m_installedVersion = version;
    emit installedVersionChanged();
}

void DshUpdate::setLatestVersion(const QString &version) {
    if (m_latestVersion == version)
        return;
    m_latestVersion = version;
    emit latestVersionChanged();
}

void DshUpdate::reportCheckInstalled(bool ok, const QString &version, const QString &error) {
    if (ok)
        setInstalledVersion(version);
    // 这是更新成功后的自动复查：不再单独发 checkInstalledFinished（避免设置页的结果
    // Label 先显示“本机版本”再被覆盖），最终结论改用 updateFinished 一次发出
    if (m_awaitPostUpdateCheck) {
        m_awaitPostUpdateCheck = false;
        emit updateFinished(ok, ok ? QStringLiteral("已更新到 ") + version
                                   : QStringLiteral("更新完成，但读取新版本失败\n") + error);
        return;
    }
    emit checkInstalledFinished(ok, version, error);
}

bool DshUpdate::startTask(Task task, const QString &program, const QStringList &args) {
    // 同一时段只跑一个 QProcess；busy 时把新任务放进单槽队列，收尾后续跑
    // （设置页每次连发 checkInstalled + checkLatest，直接丢弃会丢掉第二个）
    if (m_busy || m_process) {
        if (!m_pending) {
            m_pending = true;
            m_pendingTask = task;
            m_pendingProgram = program;
            m_pendingArgs = args;
        }
        return false;
    }

    setBusy(true);
    auto *process = new QProcess(this);
    m_process = process;
    process->setProgram(program);
    process->setArguments(args);
    // stdout/stderr 合并，方便取尾部输出当错误摘要
    process->setProcessChannelMode(QProcess::MergedChannels);

#ifdef Q_OS_WIN
    // Windows 上不要弹出控制台黑窗（同 AppController 的做法）
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *procArgs) {
        procArgs->flags |= CREATE_NO_WINDOW;
        procArgs->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        procArgs->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    connect(process, &QProcess::finished, this,
            [this, process, task](int code, QProcess::ExitStatus status) {
        const QString output = QString::fromLocal8Bit(process->readAll());
        const QString summary = tailLines(output, 20);
        const bool ok = status == QProcess::NormalExit && code == 0;
        // 正常退出但没有可用输出时给出退出码；崩溃等异常用 errorString 说明原因
        // （正常退出时 errorString 是 "Unknown error"，对用户没有信息量）
        const QString failReason = (status == QProcess::NormalExit)
            ? QStringLiteral("退出码 %1").arg(code)
            : process->errorString();
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        switch (task) {
        case Task::CheckInstalled: {
            QString version;
            QString error;
            if (ok) {
                // 不用去 ANSI：--version 输出一般是纯文本，直接截第一行为准
                version = firstNonEmptyLine(output);
                if (version.isEmpty())
                    error = QStringLiteral("dsh --version 没有输出版本信息");
            } else if (!summary.isEmpty()) {
                error = summary;
            } else {
                error = failReason;
            }
            reportCheckInstalled(ok && !version.isEmpty(), version, error);
            break;
        }
        case Task::CheckLatest: {
            QString version;
            QString error;
            if (ok) {
                version = lastNonEmptyLine(output);
                if (version.isEmpty())
                    error = QStringLiteral("npm view 没有输出版本信息");
            } else if (!summary.isEmpty()) {
                error = summary;
            } else {
                error = failReason;
            }
            if (!version.isEmpty())
                setLatestVersion(version);
            emit checkLatestFinished(!version.isEmpty(), version, error);
            break;
        }
        case Task::Update:
            if (ok) {
                // 安装完成后立即复查本地版本号，读到新版本再发 updateFinished（消息里能带上版本）
                m_awaitPostUpdateCheck = true;
                checkInstalled();
            } else {
                // 失败时把 npm 的尾部输出带给用户（同插件卸载的摘要风格）
                emit updateFinished(false, QStringLiteral("npm 更新失败\n")
                                               + (summary.isEmpty() ? failReason : summary));
            }
            break;
        }
        // 排队中的任务在整段任务链空闲后补跑；更新 OK 分支已立即复查（进程又在跑），自动跳过
        drainPending();
    });
    // 启动失败时不会有 finished 信号，这里单独收尾（同插件卸载的处理）
    connect(process, &QProcess::errorOccurred, this,
            [this, process, task](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        if (m_process == process)
            m_process = nullptr;
        process->deleteLater();
        setBusy(false);
        switch (task) {
        case Task::CheckInstalled:
            reportCheckInstalled(false, {}, QStringLiteral("无法运行 dsh --version\n") + reason);
            break;
        case Task::CheckLatest:
            emit checkLatestFinished(false, {}, QStringLiteral("无法运行 npm\n") + reason);
            break;
        case Task::Update:
            emit updateFinished(false, QStringLiteral("npm 更新失败\n") + reason);
            break;
        }
        drainPending();
    });
    process->start();
    return true;
}

// busy 期间排入的任务在这里补跑；没有空闲时（刚又启动了新任务）不动
void DshUpdate::drainPending() {
    if (!m_pending)
        return;
    m_pending = false;
    startTask(m_pendingTask, m_pendingProgram, m_pendingArgs);
}
