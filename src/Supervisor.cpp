#include "Supervisor.h"
#include "LogService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {
// 往实例日志里追加一行 Berth 自己的标记，终端面板据此区分每次运行
// 写出前按 secrets 脱敏（见 LogService::appendMarker）
void appendMarker(const QString &logPath, const QString &message, const QStringList &secrets = {}) {
    LogService::appendMarker(logPath, message, secrets);
}
}

Supervisor::Supervisor(QObject *parent) : QObject(parent) {}

Supervisor::~Supervisor() {
    // 退出应用也属于"请求停止"；析构时不再发 statusChanged，只管把进程树杀干净
    const auto ids = m_processes.keys();
    for (const QString &id : ids) {
        QProcess *process = m_processes.take(id);
        markStopRequested(process);
        killTree(id, process);
    }
    const auto jobIds = m_jobs.keys();
    for (const QString &id : jobIds)
        releaseJob(id);
    m_externals.clear();
}

void Supervisor::markStopRequested(QProcess *process) {
    if (process)
        process->setProperty("berthStopRequested", true);
}

bool Supervisor::stopRequested(const QProcess *process) {
    return process && process->property("berthStopRequested").toBool();
}

bool Supervisor::start(const LaunchSpec &spec) {
    const QString id = spec.id;
    if (id.isEmpty() || m_processes.contains(id) || m_externals.contains(id))
        return false;

    auto *process = new QProcess(this);
    process->setProgram(spec.exe.isEmpty() ? QStringLiteral("dsh") : spec.exe);
    process->setArguments(spec.args);
    process->setProcessEnvironment(spec.env);
    if (!spec.cwd.isEmpty())
        process->setWorkingDirectory(spec.cwd);

    const QString logPath = spec.logPath;
    if (!logPath.isEmpty()) {
        QDir().mkpath(QFileInfo(logPath).absolutePath());
        process->setStandardOutputFile(logPath, QIODevice::Append);
        process->setStandardErrorFile(logPath, QIODevice::Append);
    }
    process->setProcessChannelMode(QProcess::SeparateChannels);
    appendMarker(logPath, QStringLiteral("启动: %1 %2").arg(process->program(), process->arguments().join(QLatin1Char(' '))),
                 spec.secrets);

#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    connect(process, &QProcess::errorOccurred, this, [this, process, logPath, id](QProcess::ProcessError error) {
        const QString message = process->errorString();
        appendMarker(logPath, QStringLiteral("错误: %1").arg(message));
        // FailedToStart 由下面的 waitForStarted 分支处理；Crashed 随后会触发 finished，由那里判定是否为 crash
        if (error == QProcess::FailedToStart || error == QProcess::Crashed)
            return;
        emit statusChanged(id, QStringLiteral("failed"), process->processId(), message);
    });
    connect(process, &QProcess::finished, this, [this, process, logPath, id](int code, QProcess::ExitStatus status) {
        const bool requested = stopRequested(process);
        const QString exitText = status == QProcess::CrashExit
            ? QStringLiteral("异常终止，exit %1").arg(code)
            : QStringLiteral("exit %1").arg(code);
        appendMarker(logPath, requested
            ? QStringLiteral("进程已退出（请求停止），%1").arg(exitText)
            : QStringLiteral("crash: 进程在未请求停止时退出，%1").arg(exitText));
        if (m_processes.value(id) == process)
            m_processes.remove(id);
        // dsh 自己退出时，顺手清掉它留下的子进程
        releaseJob(id);
        if (auto *timer = m_probes.take(id))
            timer->deleteLater();
        process->deleteLater();
        emit processExited(id, code, !requested);
        if (requested) {
            emit statusChanged(id, QStringLiteral("stopped"), 0, {});
            return;
        }
        // 未请求的退出一律视为 crash，不看退出码
        emit statusChanged(id, QStringLiteral("failed"), 0, QStringLiteral("进程意外退出（%1）").arg(exitText));
        emit crashed(id, code);
    });

    emit statusChanged(id, QStringLiteral("starting"), 0, {});
    process->start();
    if (!process->waitForStarted(5000)) {
        const QString error = process->errorString();
        process->disconnect(this);
        process->deleteLater();
        emit statusChanged(id, QStringLiteral("failed"), 0, error);
        return false;
    }
    process->setProperty("berthLogPath", logPath);
    m_processes.insert(id, process);
    attachJob(id, process);
    emit processStarted(id);
    emit statusChanged(id, QStringLiteral("starting"), process->processId(), {});
    probe(id, spec.port);
    return true;
}

void Supervisor::stop(const QString &id) {
    if (m_externals.remove(id)) {
        // 外部进程不归 Berth 管，只解除关联
        emit statusChanged(id, QStringLiteral("stopped"), 0, {});
        return;
    }
    auto *process = m_processes.take(id);
    if (auto *timer = m_probes.take(id))
        timer->deleteLater();
    if (!process) {
        releaseJob(id);
        return;
    }
    markStopRequested(process);
    emit statusChanged(id, QStringLiteral("stopping"), process->processId(), {});
    const QString logPath = process->property("berthLogPath").toString();
    killTree(id, process);
    // killTree 已断开 finished，这里补发退出事件（process 仅 deleteLater，仍可读取退出码）
    emit processExited(id, process->exitCode(), false);
    appendMarker(logPath, QStringLiteral("已手动停止"));
    emit statusChanged(id, QStringLiteral("stopped"), 0, {});
}

void Supervisor::forceKill(const QString &id) {
    if (m_externals.contains(id)) {
        stop(id);
        return;
    }
    QProcess *process = m_processes.value(id);
    if (!process) {
        releaseJob(id);
        return;
    }
    markStopRequested(process);
    appendMarker(process->property("berthLogPath").toString(), QStringLiteral("强制结束进程树"));
#ifdef Q_OS_WIN
    if (void *job = m_jobs.value(id))
        TerminateJobObject(static_cast<HANDLE>(job), 1);
#endif
    // 不等待；进程退出后 finished 回调按"请求停止"发 stopped 并清理
    if (process->state() != QProcess::NotRunning)
        process->kill();
}

void Supervisor::attachExternal(const QString &id, qint64 pid, int port) {
    if (id.isEmpty() || m_processes.contains(id))
        return;
    m_externals.insert(id, External{pid, port});
    emit statusChanged(id, QStringLiteral("external"), pid, {});
}

bool Supervisor::isExternal(const QString &id) const {
    return m_externals.contains(id);
}

void *Supervisor::jobHandle(const QString &id) const {
    return m_jobs.value(id, nullptr);
}
void Supervisor::attachJob(const QString &id, QProcess *process) {
#ifdef Q_OS_WIN
    // KILL_ON_JOB_CLOSE：句柄关闭（包括 Berth 崩溃/被杀）时，系统连带结束整棵进程树
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
    HANDLE handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                static_cast<DWORD>(process->processId()));
    if (handle && AssignProcessToJobObject(job, handle))
        m_jobs.insert(id, job);
    else
        CloseHandle(job);
    if (handle)
        CloseHandle(handle);
#else
    Q_UNUSED(id);
    Q_UNUSED(process);
#endif
}

void Supervisor::releaseJob(const QString &id) {
#ifdef Q_OS_WIN
    if (void *job = m_jobs.take(id))
        CloseHandle(static_cast<HANDLE>(job));
#else
    Q_UNUSED(id);
#endif
}

void Supervisor::killTree(const QString &id, QProcess *process) {
    if (!process) {
        releaseJob(id);
        return;
    }
    // 先断开信号，避免主动停止被 finished 回调误报成 failed
    process->disconnect(this);
#ifdef Q_OS_WIN
    // dsh 可能是 .cmd 包装，真正占端口的是子进程 node；只杀直接子进程会留下孤儿
    if (void *job = m_jobs.take(id)) {
        TerminateJobObject(static_cast<HANDLE>(job), 1);
        CloseHandle(static_cast<HANDLE>(job));
    }
#endif
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(3000);
    }
    process->deleteLater();
}

bool Supervisor::isRunning(const QString &id) const {
    return m_processes.contains(id) || m_externals.contains(id);
}

void Supervisor::probe(const QString &id, int port) {
    auto *timer = new QTimer(this);
    timer->setInterval(500);
    int attempts = 0;
    connect(timer, &QTimer::timeout, this, [this, id, port, timer, attempts]() mutable {
        if (!m_processes.contains(id)) {
            timer->stop();
            return;
        }
        QTcpSocket socket;
        socket.connectToHost(QStringLiteral("127.0.0.1"), port);
        if (socket.waitForConnected(200)) {
            socket.disconnectFromHost();
            timer->stop();
            m_probes.remove(id);
            timer->deleteLater();
            emit statusChanged(id, QStringLiteral("running"), m_processes.value(id)->processId(), {});
            return;
        }
        if (++attempts >= 40) {
            timer->stop();
            m_probes.remove(id);
            timer->deleteLater();
            emit statusChanged(id, QStringLiteral("failed"), m_processes.value(id)->processId(),
                               QStringLiteral("port %1 did not open").arg(port));
        }
    });
    m_probes.insert(id, timer);
    timer->start();
}
