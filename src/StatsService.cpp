#include "StatsService.h"

#include "ProcessRunner.h"
#include "core/ProjectKey.h"
#include "core/UsageScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimeZone>
#include <QTimer>
#include <QVariantList>

namespace {

const QString kHelperResource = QStringLiteral(":/assets/helpers/usage-scan.mjs");
const QString kHelperName = QStringLiteral("usage-scan.mjs");

QVariantMap tokensMap(const UsageAgg::Tokens &t) {
    return {
        {QStringLiteral("input"), t.input},
        {QStringLiteral("output"), t.output},
        {QStringLiteral("cache"), t.cache},
    };
}

// 输出尾部里最后一行非协议输出（Node 报错信息等），截短后用于失败原因
QString lastPlainLine(const QString &tail) {
    const QStringList lines = tail.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        const QString line = lines.at(i).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('@')))
            return line.left(200);
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------

struct StatsService::SessionLife {
    QMutex mutex;
    bool alive = true;
};

StatsService::StatsService(ProcessRunner *runner, QObject *parent)
    : QObject(parent), m_runner(runner), m_life(std::make_shared<SessionLife>()) {}

StatsService::~StatsService() {
    {
        QMutexLocker lock(&m_life->mutex);
        m_life->alive = false;
    }
    for (const SessionEntry &e : std::as_const(m_sessions)) {
        if (e.cancel)
            e.cancel->store(true);
    }
    cancelUsage();
}

void StatsService::setNodeExecutable(const QString &path) {
    m_nodeExecutable = path.trimmed();
}

void StatsService::setHelpersDir(const QString &dir) {
    m_helpersDir = dir;
}

UsageAgg::Range StatsService::rangeFromString(const QString &s) {
    if (s == u"7d")
        return UsageAgg::Range::Last7Days;
    if (s == u"30d")
        return UsageAgg::Range::Last30Days;
    if (s == u"all")
        return UsageAgg::Range::All;
    return UsageAgg::Range::Today;
}

QString StatsService::rangeToString(UsageAgg::Range range) {
    switch (range) {
    case UsageAgg::Range::Last7Days: return QStringLiteral("7d");
    case UsageAgg::Range::Last30Days: return QStringLiteral("30d");
    case UsageAgg::Range::All: return QStringLiteral("all");
    case UsageAgg::Range::Today: break;
    }
    return QStringLiteral("today");
}

QString StatsService::resolveNode() const {
    if (!m_nodeExecutable.isEmpty()) {
        if (QFileInfo(m_nodeExecutable).isAbsolute() || m_nodeExecutable.contains(QLatin1Char('/'))
            || m_nodeExecutable.contains(QLatin1Char('\\')))
            return QFileInfo::exists(m_nodeExecutable) ? m_nodeExecutable : QString();
        return QStandardPaths::findExecutable(m_nodeExecutable);
    }
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

QString StatsService::ensureHelper(QString *error) const {
    QFile res(kHelperResource);
    if (!res.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("内置用量脚本缺失");
        return {};
    }
    const QByteArray bytes = res.readAll();
    if (m_helpersDir.isEmpty()) {
        *error = QStringLiteral("未设置辅助脚本目录");
        return {};
    }
    const QString target = QDir(m_helpersDir).filePath(kHelperName);
    {
        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bytes)
            return target;
    }
    if (!QDir().mkpath(m_helpersDir)) {
        *error = QStringLiteral("无法创建目录 %1").arg(QDir::toNativeSeparators(m_helpersDir));
        return {};
    }
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        *error = QStringLiteral("无法写入 %1：%2").arg(QDir::toNativeSeparators(target), out.errorString());
        return {};
    }
    return target;
}

int StatsService::usage(UsageAgg::Range range, const QList<Target> &targets) {
    cancelUsage();

    const int id = ++m_usageSeq;
    m_usage = UsageRun{};
    m_usage.active = true;
    m_usage.result.requestId = id;
    m_usage.result.range = range;
    m_usage.result.now = QDateTime::currentDateTime();
    for (const Target &t : targets) {
        BerthUsage b;
        b.berthId = t.berthId;
        b.berthName = t.berthName;
        m_usage.result.berths.append(b);
    }

    if (targets.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, id] {
            if (m_usage.active && m_usage.result.requestId == id)
                completeUsage();
        }, Qt::QueuedConnection);
        return id;
    }

    QString error;
    const QString script = ensureHelper(&error);
    const QString node = script.isEmpty() ? QString() : resolveNode();
    if (script.isEmpty() || node.isEmpty()) {
        if (!script.isEmpty())
            error = m_nodeExecutable.isEmpty()
                        ? QStringLiteral("未找到 node（需要 Node ≥ 22.19，可在设置中指定 Node 路径）")
                        : QStringLiteral("找不到 Node：%1").arg(QDir::toNativeSeparators(m_nodeExecutable));
        QMetaObject::invokeMethod(this, [this, id, error] {
            if (m_usage.active && m_usage.result.requestId == id)
                failAllUsage(error);
        }, Qt::QueuedConnection);
        return id;
    }

    // 区间起点（本地日 0 点）交给脚本做粗过滤；精确归日仍由 UsageAgg::aggregate 完成
    qint64 since = 0;
    QDate first, last;
    const QTimeZone tz = QTimeZone::systemTimeZone();
    if (UsageAgg::bounds(range, m_usage.result.now, tz, &first, &last))
        since = first.startOfDay(tz).toMSecsSinceEpoch();

    ProcessOptions opts;
    opts.timeoutMs = kUsageTimeoutMs;
    opts.tailLines = 200000; // 协议行都要保留

    m_usage.remaining = targets.size();
    for (int i = 0; i < targets.size(); ++i) {
        const Target &t = targets.at(i);
        QStringList args{QStringLiteral("--no-warnings"), script,
                         QStringLiteral("--root"), QDir(t.home).filePath(QStringLiteral("sessions"))};
        const QString ws = t.workspace.trimmed();
        if (!ws.isEmpty()) {
            const QString key = ProjectKey::projectKey(QDir::toNativeSeparators(QDir::cleanPath(ws)));
            if (!key.isEmpty())
                args << QStringLiteral("--project") << key;
        }
        if (since > 0)
            args << QStringLiteral("--since") << QString::number(since);

        ProcessTask *task = m_runner->run(node, args, opts);
        m_usage.tasks.append(task);
        connect(task, &ProcessTask::finished, this,
                [this, task, id, i](bool ok, int code, const QString &tail, bool timedOut) {
                    finishTarget(id, i, ok, code, tail, timedOut, task->failedToStart(), task->errorString());
                });
    }
    return id;
}

void StatsService::cancelUsage() {
    if (!m_usage.active)
        return;
    m_usage.active = false;
    const auto tasks = m_usage.tasks;
    m_usage.tasks.clear();
    for (const QPointer<ProcessTask> &t : tasks) {
        if (t && t->isRunning())
            t->kill();
    }
}

void StatsService::finishTarget(int requestId, int index, bool ok, int code, const QString &tail,
                                bool timedOut, bool failedToStart, const QString &processError) {
    if (!m_usage.active || m_usage.result.requestId != requestId)
        return;
    if (index < 0 || index >= m_usage.result.berths.size())
        return;
    BerthUsage &b = m_usage.result.berths[index];

    if (failedToStart) {
        b.error = QStringLiteral("无法启动 node：%1").arg(processError);
    } else if (timedOut) {
        b.error = QStringLiteral("读取超时（超过 %1 秒）").arg(kUsageTimeoutMs / 1000);
    } else {
        const UsageScan::Output out = UsageScan::parse(tail, b.berthId);
        if (!out.error.isEmpty()) {
            b.error = QStringLiteral("用量脚本出错：%1").arg(out.error);
        } else if (!out.complete) {
            const QString detail = lastPlainLine(tail);
            b.error = QStringLiteral("node 异常退出（退出码 %1）").arg(code);
            if (!detail.isEmpty())
                b.error += QStringLiteral("：") + detail;
            if (ok && code == 0)
                b.error = QStringLiteral("用量脚本输出不完整");
        } else if (!out.zstdSupported && out.skippedFiles > 0 && out.files == 0) {
            b.error = QStringLiteral("当前 Node 不支持 zstd 解压（需要 Node ≥ 22.19）");
        } else {
            b.ok = true;
            b.files = out.files;
            b.skippedFiles = out.skippedFiles;
            b.skippedLines = out.skippedLines;
            b.hasUsage = !out.records.isEmpty();
            m_usage.result.records.append(out.records);
        }
    }

    if (--m_usage.remaining <= 0)
        completeUsage();
}

void StatsService::failAllUsage(const QString &reason) {
    for (BerthUsage &b : m_usage.result.berths) {
        b.ok = false;
        b.error = reason;
    }
    m_usage.result.error = reason;
    completeUsage();
}

void StatsService::completeUsage() {
    UsageResult &r = m_usage.result;
    r.totals = UsageAgg::aggregate(r.records, r.range, r.now);
    r.failedCount = 0;
    r.anyUsage = false;
    QStringList reasons;
    for (const BerthUsage &b : r.berths) {
        if (!b.ok) {
            ++r.failedCount;
            if (!reasons.contains(b.error))
                reasons.append(b.error);
        } else if (b.hasUsage) {
            r.anyUsage = true;
        }
    }
    r.allFailed = !r.berths.isEmpty() && r.failedCount == r.berths.size();
    if (r.allFailed && r.error.isEmpty())
        r.error = reasons.join(QStringLiteral("；"));

    m_usage.active = false;
    m_usage.tasks.clear();
    const UsageResult result = r;
    emit usageFinished(result);
}

// ---------------------------------------------------------------------------
// toVariantMap：
// {
//   requestId, range("today"|"7d"|"30d"|"all"), updatedAt(ISO 本地时间),
//   anyUsage, allFailed, error, failedCount,
//   total: {input, output, cache},
//   byBerth: [{id, name, ok, error, hasUsage, input, output, cache}],   // 与请求同序；失败泊位 token 为 0
//   byModel: [{model, input, output, cache, priced, cost}],             // cost 仅 priced 时有效
//   byDay:   [{day("YYYY-MM-DD"), input, output, cache}],
//   totalCost, hasUnpriced, unpriced: [模型名]
// }
QVariantMap StatsService::UsageResult::toVariantMap(const QMap<QString, ModelPrice> &prices) const {
    QVariantMap map;
    map.insert(QStringLiteral("requestId"), requestId);
    map.insert(QStringLiteral("range"), StatsService::rangeToString(range));
    map.insert(QStringLiteral("updatedAt"), now.toString(Qt::ISODate));
    map.insert(QStringLiteral("anyUsage"), anyUsage);
    map.insert(QStringLiteral("allFailed"), allFailed);
    map.insert(QStringLiteral("error"), error);
    map.insert(QStringLiteral("failedCount"), failedCount);
    map.insert(QStringLiteral("total"), tokensMap(totals.total));

    QVariantList berthRows;
    for (const BerthUsage &b : berths) {
        QVariantMap row = tokensMap(totals.byBerth.value(b.berthId));
        row.insert(QStringLiteral("id"), b.berthId);
        row.insert(QStringLiteral("name"), b.berthName);
        row.insert(QStringLiteral("ok"), b.ok);
        row.insert(QStringLiteral("error"), b.error);
        row.insert(QStringLiteral("hasUsage"), b.hasUsage);
        berthRows.append(row);
    }
    map.insert(QStringLiteral("byBerth"), berthRows);

    const UsageAgg::CostSummary cs = UsageAgg::costs(totals, prices);
    QVariantList modelRows;
    for (auto it = totals.byModel.constBegin(); it != totals.byModel.constEnd(); ++it) {
        QVariantMap row = tokensMap(it.value());
        row.insert(QStringLiteral("model"), it.key());
        const bool priced = cs.byModel.contains(it.key());
        row.insert(QStringLiteral("priced"), priced);
        row.insert(QStringLiteral("cost"), priced ? cs.byModel.value(it.key()) : 0.0);
        modelRows.append(row);
    }
    map.insert(QStringLiteral("byModel"), modelRows);

    QVariantList dayRows;
    for (auto it = totals.byDay.constBegin(); it != totals.byDay.constEnd(); ++it) {
        QVariantMap row = tokensMap(it.value());
        row.insert(QStringLiteral("day"), it.key().toString(Qt::ISODate));
        dayRows.append(row);
    }
    map.insert(QStringLiteral("byDay"), dayRows);

    map.insert(QStringLiteral("totalCost"), cs.total);
    map.insert(QStringLiteral("hasUnpriced"), cs.hasUnpriced());
    map.insert(QStringLiteral("unpriced"), cs.unpriced);
    return map;
}

// ---------------------------------------------------------------------------
// 会话统计（需求 21）。只枚举目录与读 mtime，不打开、不创建、不加锁。

StatsService::SessionScan StatsService::scanSessions(const QString &home, const QString &workspace,
                                                     const std::atomic_bool *cancel) {
    using Kind = SessionScan::Kind;
    SessionScan out;
    const auto fail = [&out](Kind kind, const QString &detail) {
        out.kind = kind;
        out.detail = detail;
        out.agg = {};
        return out;
    };
    const auto cancelled = [cancel] { return cancel && cancel->load(); };
    const auto native = [](const QString &p) { return QDir::toNativeSeparators(p); };

    const QString rootPath = QDir(home).filePath(QStringLiteral("sessions"));
    const QFileInfo root(rootPath);
    if (!root.exists())
        return fail(Kind::NotFound, native(rootPath));
    if (!root.isDir())
        return fail(Kind::Unparsable, QStringLiteral("%1 不是目录").arg(native(rootPath)));
    if (!root.isReadable())
        return fail(Kind::Unreadable, native(rootPath));

    const QDir::Filters dirFilter = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden;
    QStringList projects;
    const QString ws = workspace.trimmed();
    if (!ws.isEmpty()) {
        // 泊位配置了 workspace：只取该 workspace 对应的项目目录；目录还没有表示尚无会话
        const QString key = ProjectKey::projectKey(native(QDir::cleanPath(ws)));
        if (!key.isEmpty()) {
            const QString projectPath = QDir(rootPath).filePath(key);
            const QFileInfo project(projectPath);
            if (project.exists()) {
                if (!project.isDir())
                    return fail(Kind::Unparsable, QStringLiteral("%1 不是目录").arg(native(projectPath)));
                if (!project.isReadable())
                    return fail(Kind::Unreadable, native(projectPath));
                projects.append(projectPath);
            }
        }
    } else {
        const QFileInfoList list = QDir(rootPath).entryInfoList(dirFilter, QDir::Name);
        for (const QFileInfo &fi : list)
            projects.append(fi.absoluteFilePath());
    }

    QList<SessionAgg::SessionDir> dirs;
    int withLog = 0;
    const QStringList logPatterns{QStringLiteral("session*.jsonl*")};
    for (const QString &projectPath : std::as_const(projects)) {
        if (cancelled())
            return fail(Kind::Timeout, {});
        const QDir projectDir(projectPath);
        if (!projectDir.isReadable())
            return fail(Kind::Unreadable, native(projectPath));
        const QFileInfoList sessions = projectDir.entryInfoList(dirFilter, QDir::Name);
        for (const QFileInfo &sessionInfo : sessions) {
            if (cancelled())
                return fail(Kind::Timeout, {});
            SessionAgg::SessionDir sd;
            sd.name = sessionInfo.fileName();
            const QFileInfoList logs = QDir(sessionInfo.absoluteFilePath())
                                           .entryInfoList(logPatterns, QDir::Files | QDir::Hidden);
            for (const QFileInfo &log : logs) {
                const QDateTime m = log.lastModified();
                if (m.isValid() && (!sd.mtime.isValid() || m > sd.mtime))
                    sd.mtime = m;
            }
            if (!logs.isEmpty())
                ++withLog;
            dirs.append(sd);
        }
    }
    // 有会话目录却没有任何 session*.jsonl* 日志：目录布局与预期不符
    if (!dirs.isEmpty() && withLog == 0)
        return fail(Kind::Unparsable, QStringLiteral("会话目录中没有 session*.jsonl 日志（%1）").arg(native(rootPath)));

    out.kind = Kind::Ok;
    out.agg = SessionAgg::aggregate(dirs, QDateTime::currentDateTime());
    return out;
}

QString StatsService::sessionReasonText(const SessionScan &scan) {
    using Kind = SessionScan::Kind;
    QString head;
    switch (scan.kind) {
    case Kind::Ok: return {};
    case Kind::NotFound: head = QStringLiteral("文件不存在"); break;
    case Kind::Unreadable: head = QStringLiteral("无法读取"); break;
    case Kind::Unparsable: head = QStringLiteral("格式无法解析"); break;
    case Kind::Timeout:
        return QStringLiteral("读取超时（超过 %1 秒）").arg(kSessionTimeoutMs / 1000);
    }
    return scan.detail.isEmpty() ? head : head + QStringLiteral("：") + scan.detail;
}

void StatsService::sessionStats(const QString &id, const QString &home, const QString &workspace) {
    if (id.isEmpty())
        return;
    SessionEntry &entry = m_sessions[id];
    if (entry.loading)
        return;
    const int seq = ++m_sessionSeq;
    entry.seq = seq;
    entry.loading = true;
    auto cancel = std::make_shared<std::atomic_bool>(false);
    entry.cancel = cancel;
    emit sessionStatsChanged(id);

    // 析构时 alive 在同一把锁内置 false；持锁投递保证投递时对象仍在，
    // 对象销毁时 Qt 会丢弃尚未处理的投递事件
    const std::shared_ptr<SessionLife> life = m_life;
    QThreadPool::globalInstance()->start([this, life, cancel, id, seq, home, workspace] {
        const SessionScan scan = scanSessions(home, workspace, cancel.get());
        QMutexLocker lock(&life->mutex);
        if (!life->alive)
            return;
        QMetaObject::invokeMethod(this, [this, id, seq, scan] { finishSession(id, seq, scan); },
                                  Qt::QueuedConnection);
    });

    QTimer::singleShot(kSessionTimeoutMs, this, [this, id, seq] {
        const auto it = m_sessions.constFind(id);
        if (it == m_sessions.constEnd() || it->seq != seq || !it->loading)
            return;
        if (it->cancel)
            it->cancel->store(true);
        SessionScan timeout;
        timeout.kind = SessionScan::Kind::Timeout;
        finishSession(id, seq, timeout);
    });
}

void StatsService::finishSession(const QString &id, int seq, const SessionScan &scan) {
    const auto it = m_sessions.find(id);
    if (it == m_sessions.end() || it->seq != seq || !it->loading)
        return; // 已超时或已被新一轮取代
    it->loading = false;
    it->hasResult = true;
    it->scan = scan;
    it->updatedAt = QDateTime::currentDateTime();
    it->cancel.reset();
    emit sessionStatsChanged(id);
}

QVariantMap StatsService::sessionStatsFor(const QString &id) const {
    QVariantMap map;
    const auto it = m_sessions.constFind(id);
    const bool loading = it != m_sessions.constEnd() && it->loading;
    map.insert(QStringLiteral("loading"), loading);
    if (it == m_sessions.constEnd() || !it->hasResult) {
        map.insert(QStringLiteral("state"), QStringLiteral("idle"));
        return map;
    }
    map.insert(QStringLiteral("updatedAt"), it->updatedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (it->scan.kind != SessionScan::Kind::Ok) {
        map.insert(QStringLiteral("state"), QStringLiteral("error"));
        map.insert(QStringLiteral("error"), sessionReasonText(it->scan));
        return map;
    }
    const SessionAgg::Result &agg = it->scan.agg;
    map.insert(QStringLiteral("state"), QStringLiteral("ok"));
    map.insert(QStringLiteral("total"), agg.total);
    map.insert(QStringLiteral("active"), agg.active);
    map.insert(QStringLiteral("lastActivity"),
               agg.total > 0 && agg.lastActivity.isValid()
                   ? agg.lastActivity.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                   : QStringLiteral("—"));
    return map;
}
