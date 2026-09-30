#include "ProcessMetrics.h"

#include "Supervisor.h"
#include "core/DataFile.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>

#include <cmath>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace {
const QString kFile = QStringLiteral("metrics.json");
constexpr int kSampleMs = 2000;
constexpr int kFlushMs = 60000;
constexpr int kSaveDelayMs = 300; // 事件后合并写盘，保证 1 秒内落盘
constexpr int kTrendMax = 150;

double round1(double v)
{
    if (!std::isfinite(v) || v < 0)
        return 0.0;
    return std::round(v * 10.0) / 10.0;
}

double bytesToMb(quint64 bytes)
{
    return round1(double(bytes) / (1024.0 * 1024.0));
}

bool isActive(const QString &status)
{
    return status == QLatin1String("starting") || status == QLatin1String("running")
        || status == QLatin1String("stopping");
}

int logicalCores()
{
#ifdef Q_OS_WIN
    const DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n > 0)
        return int(n);
#endif
    return qMax(1, QThread::idealThreadCount());
}

#ifdef Q_OS_WIN
qint64 fileTimeTo100ns(const FILETIME &ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return qint64(u.QuadPart);
}

qint64 systemNow100ns()
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    return fileTimeTo100ns(now);
}

// 单个进程的工作集（字节）；失败返回 false
bool workingSet(HANDLE process, quint64 *bytes)
{
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!K32GetProcessMemoryInfo(process, &pmc, sizeof(pmc)))
        return false;
    *bytes = quint64(pmc.WorkingSetSize);
    return true;
}

// Job 内全部进程：CPU 时间（100ns，含已退出进程）与工作集之和
bool queryJob(HANDLE job, qint64 *cpu100ns, quint64 *memBytes)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acct{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &acct, sizeof(acct), nullptr))
        return false;
    *cpu100ns = qint64(acct.TotalUserTime.QuadPart + acct.TotalKernelTime.QuadPart);

    constexpr DWORD kMaxIds = 512;
    QByteArray buf(int(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + (kMaxIds - 1) * sizeof(ULONG_PTR)), 0);
    auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buf.data());
    if (!QueryInformationJobObject(job, JobObjectBasicProcessIdList, list, DWORD(buf.size()), nullptr)
        && GetLastError() != ERROR_MORE_DATA)
        return false;

    quint64 sum = 0;
    DWORD measured = 0;
    for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
        const DWORD pid = DWORD(list->ProcessIdList[i]);
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
            continue;
        quint64 ws = 0;
        if (workingSet(h, &ws)) {
            sum += ws;
            ++measured;
        }
        CloseHandle(h);
    }
    if (measured == 0)
        return false;
    *memBytes = sum;
    return true;
}

// 单个 PID：CPU 时间（100ns）、创建时间（系统时间 100ns）与工作集；进程已退出或打不开返回 false
bool queryPid(qint64 pid, qint64 *cpu100ns, qint64 *created100ns, quint64 *memBytes)
{
    if (pid <= 0)
        return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    bool ok = false;
    DWORD code = 0;
    FILETIME c, e, k, u;
    if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE && GetProcessTimes(h, &c, &e, &k, &u)
        && workingSet(h, memBytes)) {
        *cpu100ns = fileTimeTo100ns(k) + fileTimeTo100ns(u);
        *created100ns = fileTimeTo100ns(c);
        ok = true;
    }
    CloseHandle(h);
    return ok;
}
#endif
} // namespace

ProcessMetrics::ProcessMetrics(Supervisor *supervisor, const QString &dataDir, QObject *parent)
    : QObject(parent), m_supervisor(supervisor), m_path(dataDir + QLatin1Char('/') + kFile),
      m_cores(logicalCores())
{
    m_clock.start();
    load();

    m_sampleTimer.setInterval(kSampleMs);
    m_sampleTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sampleTimer, &QTimer::timeout, this, &ProcessMetrics::sampleAll);
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &ProcessMetrics::periodicFlush);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &ProcessMetrics::saveNow);

    if (m_supervisor) {
        connect(m_supervisor, &Supervisor::statusChanged, this,
                [this](const QString &id, const QString &status, qint64 pid, const QString &) {
                    onStatus(id, status, pid);
                });
        connect(m_supervisor, &Supervisor::processStarted, this, &ProcessMetrics::onStarted);
        connect(m_supervisor, &Supervisor::processExited, this, &ProcessMetrics::onExited);
    }
}

ProcessMetrics::~ProcessMetrics()
{
    // Berth 退出：补记本次运行尚未写盘的时长（进程由 Supervisor 析构时结束，不记为退出/崩溃）
    bool changed = false;
    for (auto it = m_live.begin(); it != m_live.end(); ++it)
        changed = creditRuntime(it.key(), it.value()) || changed;
    if (changed || m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
}

void ProcessMetrics::load()
{
    if (!QFileInfo::exists(m_path)) {
        m_loadNotice = QStringLiteral("未找到指标文件 %1，统计从 0 开始").arg(kFile);
        return;
    }
    QFile in(m_path);
    if (!in.open(QIODevice::ReadOnly)) {
        m_loadNotice = QStringLiteral("指标文件 %1 无法读取（%2），统计已重置为 0")
                           .arg(kFile, in.errorString());
        return;
    }
    const DataFile::LoadResult loaded = DataFile::load(in.readAll(), MetricsFile::kSchemaVersion, QString());
    if (loaded.mode == DataFile::Mode::Corrupt) {
        m_loadNotice = QStringLiteral("指标文件 %1 无法解析（%2），统计已重置为 0").arg(kFile, loaded.error);
        return;
    }
    bool ok = false;
    const QHash<QString, MetricsState> states = MetricsFile::decode(loaded.root, &ok);
    if (!ok) {
        m_loadNotice = QStringLiteral("指标文件 %1 缺少 instances 字段，统计已重置为 0").arg(kFile);
        return;
    }
    m_states = states;
    m_originalRoot = loaded.root;
    if (loaded.mode == DataFile::Mode::ReadOnly) {
        m_readOnly = true;
        m_loadNotice = QStringLiteral("%1 的 schema 版本 %2 高于当前支持的 %3，统计以只读方式加载，修改不会保存")
                           .arg(kFile)
                           .arg(loaded.version)
                           .arg(MetricsFile::kSchemaVersion);
    }
}

QVariantMap ProcessMetrics::current(const QString &id) const
{
    QVariantMap m;
    const auto it = m_live.constFind(id);
    const bool valid = it != m_live.constEnd() && it->currentValid;
    m.insert(QStringLiteral("valid"), valid);
    m.insert(QStringLiteral("mainOnly"),
             it != m_live.constEnd() && it->status == QLatin1String("external"));
    if (valid) {
        m.insert(QStringLiteral("cpu"), it->current.cpu);
        m.insert(QStringLiteral("memMb"), it->current.memMb);
        m.insert(QStringLiteral("at"), it->current.t);
    }
    return m;
}

QVariantList ProcessMetrics::trend(const QString &id) const
{
    QVariantList out;
    const auto it = m_live.constFind(id);
    if (it == m_live.constEnd())
        return out;
    for (const Point &p : it->trend) {
        QVariantMap m;
        m.insert(QStringLiteral("t"), p.t);
        m.insert(QStringLiteral("cpu"), p.cpu);
        m.insert(QStringLiteral("memMb"), p.memMb);
        out.append(m);
    }
    return out;
}

QVariantMap ProcessMetrics::totals(const QString &id) const
{
    const MetricsState s = m_states.value(id);
    qint64 runtime = s.runtimeSec;
    const auto it = m_live.constFind(id);
    if (it != m_live.constEnd() && it->own) {
        const qint64 pending = (m_clock.elapsed() - it->startMs) / 1000 - it->creditedSec;
        if (pending > 0)
            runtime += pending;
    }
    QVariantMap m;
    m.insert(QStringLiteral("runtimeSec"), runtime);
    m.insert(QStringLiteral("starts"), s.starts);
    m.insert(QStringLiteral("crashes"), s.crashes);
    m.insert(QStringLiteral("hasLastExit"), s.hasLastExit);
    if (s.hasLastExit) {
        m.insert(QStringLiteral("lastExitCode"), s.lastExitCode);
        m.insert(QStringLiteral("lastExitAt"),
                 s.lastExitAt.isValid()
                     ? s.lastExitAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                     : QString());
    }
    return m;
}

bool ProcessMetrics::reset(const QString &id)
{
    const auto it = m_live.constFind(id);
    if (it != m_live.constEnd() && (it->own || isActive(it->status)))
        return false;
    if (m_readOnly)
        return false;
    m_states[id].reset();
    scheduleSave();
    emit totalsChanged(id);
    return true;
}

void ProcessMetrics::forget(const QString &id)
{
    m_live.remove(id);
    if (m_states.remove(id))
        scheduleSave();
    updateTimers();
}

QHash<QString, Summary::Sample> ProcessMetrics::latestSamples() const
{
    QHash<QString, Summary::Sample> out;
    for (auto it = m_live.cbegin(); it != m_live.cend(); ++it) {
        if (!it->currentValid)
            continue;
        Summary::Sample s;
        s.cpuPercent = it->current.cpu;
        s.memMb = it->current.memMb;
        out.insert(it.key(), s);
    }
    return out;
}

void ProcessMetrics::onStatus(const QString &id, const QString &status, qint64 pid)
{
    Live &live = m_live[id];
    const QString prev = live.status;
    live.status = status;
    if (status == QLatin1String("external")) {
        if (prev != status) {
            live.trend.clear();
            live.hasBase = false;
        }
        live.pid = pid;
    } else if (status != QLatin1String("running")) {
        live.currentValid = false;
        live.pid = 0;
        // 外部泊位的 CPU 基准基于 PID，离开 external 后作废
        if (prev == QLatin1String("external"))
            live.hasBase = false;
    }
    updateTimers();
    if (!live.currentValid && prev != status)
        emit sampled(id);
}

void ProcessMetrics::onStarted(const QString &id)
{
    Live &live = m_live[id];
    live.own = true;
    live.startMs = m_clock.elapsed();
    live.creditedSec = 0;
    // Job 在进程启动时新建，累计 CPU 从 0 开始
    live.hasBase = true;
    live.baseCpu100ns = 0;
    live.baseWallMs = live.startMs;
    live.currentValid = false;
    live.trend.clear();
    m_states[id].onStart();
    scheduleSave();
    updateTimers();
    emit totalsChanged(id);
}

void ProcessMetrics::onExited(const QString &id, int exitCode, bool crash)
{
    Live &live = m_live[id];
    creditRuntime(id, live);
    live.own = false;
    live.hasBase = false;
    live.currentValid = false;
    m_states[id].onExit(exitCode, crash, QDateTime::currentDateTime());
    scheduleSave();
    updateTimers();
    emit totalsChanged(id);
    emit sampled(id);
}

void ProcessMetrics::sampleAll()
{
    for (auto it = m_live.begin(); it != m_live.end(); ++it) {
        const QString &status = it->status;
        if (status != QLatin1String("running") && status != QLatin1String("external"))
            continue;
        sampleOne(it.key(), it.value());
        emit sampled(it.key());
    }
    emit samplesUpdated();
}

void ProcessMetrics::sampleOne(const QString &id, Live &live)
{
    bool ok = false;
    Point p;
    p.t = QDateTime::currentMSecsSinceEpoch();
#ifdef Q_OS_WIN
    qint64 cpu = 0;
    quint64 mem = 0;
    if (live.status == QLatin1String("running")) {
        void *job = m_supervisor ? m_supervisor->jobHandle(id) : nullptr;
        if (job && queryJob(static_cast<HANDLE>(job), &cpu, &mem)) {
            const qint64 wall = m_clock.elapsed();
            if (!live.hasBase) {
                // 没有基准（理论上不会发生）：本次只建立基准
                live.hasBase = true;
                live.baseCpu100ns = cpu;
                live.baseWallMs = wall;
            } else {
                p.cpu = cpuPercent(cpu - live.baseCpu100ns, wall - live.baseWallMs, m_cores);
                p.memMb = bytesToMb(mem);
                live.baseCpu100ns = cpu;
                live.baseWallMs = wall;
                ok = wall > 0;
            }
        }
    } else {
        qint64 created = 0;
        if (queryPid(live.pid, &cpu, &created, &mem)) {
            const qint64 wall = systemNow100ns() / 10000;
            qint64 baseCpu = live.baseCpu100ns;
            qint64 baseWall = live.baseWallMs;
            if (!live.hasBase) {
                // 首次采样：以进程创建时刻为基准，得到自创建以来的平均占用
                baseCpu = 0;
                baseWall = created / 10000;
            }
            p.cpu = cpuPercent(cpu - baseCpu, wall - baseWall, m_cores);
            p.memMb = bytesToMb(mem);
            live.hasBase = true;
            live.baseCpu100ns = cpu;
            live.baseWallMs = wall;
            ok = true;
        }
    }
#else
    Q_UNUSED(id);
#endif
    if (!ok) {
        // 采样失败：跳过该点，当前值显示"—"，保留趋势
        live.currentValid = false;
        return;
    }
    live.current = p;
    live.currentValid = true;
    live.trend.append(p);
    while (live.trend.size() > kTrendMax)
        live.trend.removeFirst();
}

bool ProcessMetrics::creditRuntime(const QString &id, Live &live)
{
    if (!live.own)
        return false;
    const qint64 total = (m_clock.elapsed() - live.startMs) / 1000;
    const qint64 delta = total - live.creditedSec;
    if (delta <= 0)
        return false;
    m_states[id].addRuntime(delta);
    live.creditedSec = total;
    return true;
}

void ProcessMetrics::periodicFlush()
{
    bool changed = false;
    for (auto it = m_live.begin(); it != m_live.end(); ++it) {
        if (creditRuntime(it.key(), it.value())) {
            changed = true;
            emit totalsChanged(it.key());
        }
    }
    if (changed)
        saveNow();
}

void ProcessMetrics::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void ProcessMetrics::saveNow()
{
    if (m_readOnly)
        return;
    const QJsonObject known = MetricsFile::encode(m_states);
    const QJsonObject root = DataFile::merge(m_originalRoot, known, MetricsFile::kSchemaVersion);
    const QByteArray bytes = DataFile::toBytes(root);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile out(m_path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        qWarning() << "ProcessMetrics: 写入" << m_path << "失败:" << out.errorString();
        return;
    }
    m_originalRoot = root;
    m_originalRoot.remove(QLatin1String(DataFile::kSchemaKey));
}

void ProcessMetrics::updateTimers()
{
    bool sampling = false;
    bool own = false;
    for (const Live &live : std::as_const(m_live)) {
        if (live.status == QLatin1String("running") || live.status == QLatin1String("external"))
            sampling = true;
        if (live.own)
            own = true;
    }
    if (sampling && !m_sampleTimer.isActive())
        m_sampleTimer.start();
    else if (!sampling)
        m_sampleTimer.stop();
    if (own && !m_flushTimer.isActive())
        m_flushTimer.start();
    else if (!own)
        m_flushTimer.stop();
}
