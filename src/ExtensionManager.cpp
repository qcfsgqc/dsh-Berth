#include "ExtensionManager.h"

#include "core/AffectedSet.h"
#include "core/PatchYaml.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace {

const QString kPatchFile = QStringLiteral("cordis.patch.yml");
const QByteArray kUtf8Bom("\xEF\xBB\xBF", 3);

// 原始字节 → 文本（去掉 UTF-8 BOM）；不是合法 UTF-8 时返回 false
bool decode(const QByteArray &bytes, QString *text, bool *bom) {
    *bom = bytes.startsWith(kUtf8Bom);
    const QByteArray body = *bom ? bytes.mid(kUtf8Bom.size()) : bytes;
    *text = QString::fromUtf8(body);
    return text->toUtf8() == body;
}

QByteArray encode(const QString &text, bool bom) {
    const QByteArray body = text.toUtf8();
    return bom ? kUtf8Bom + body : body;
}

QVariantMap result(bool ok, const QString &error = {}, bool externalChange = false) {
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("error"), error},
            {QStringLiteral("externalChange"), externalChange}};
}

bool isStartingOrRunning(const QString &status) {
    return status == QLatin1String("starting") || status == QLatin1String("running");
}

const QString kDisabledDir = QStringLiteral(".berth-disabled");

int sourceRank(const QString &source) {
    if (source == QLatin1String("home"))
        return 0;
    if (source == QLatin1String("workspace-dsh"))
        return 1;
    return 2;
}

// 扫描一层：<name>/SKILL.md 或 <name>.md；跳过以 '.' 开头的项（.system、.berth-disabled 等）
void scanSkills(const QString &dir, const QString &root, const QString &source, bool enabled, QVariantList *rows) {
    const QFileInfoList items =
        QDir(dir).entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : items) {
        const QString entry = fi.fileName();
        if (entry.startsWith(QLatin1Char('.')))
            continue;
        QString name;
        QString kind;
        if (fi.isDir()) {
            if (!QFileInfo(fi.filePath() + QStringLiteral("/SKILL.md")).isFile())
                continue;
            name = entry;
            kind = QStringLiteral("dir");
        } else if (fi.isFile() && entry.endsWith(QLatin1String(".md"), Qt::CaseInsensitive) && entry.size() > 3) {
            name = entry.chopped(3);
            kind = QStringLiteral("file");
        } else {
            continue;
        }
        rows->append(QVariantMap{{QStringLiteral("id"), root + QLatin1Char('|') + entry},
                                 {QStringLiteral("name"), name},
                                 {QStringLiteral("entry"), entry},
                                 {QStringLiteral("kind"), kind},
                                 {QStringLiteral("source"), source},
                                 {QStringLiteral("root"), root},
                                 {QStringLiteral("path"), fi.filePath()},
                                 {QStringLiteral("enabled"), enabled}});
    }
}

bool pathExists(const QString &path) {
    const QFileInfo fi(path);
    return fi.exists() || fi.isSymLink();
}

} // namespace

ExtensionManager::ExtensionManager(QObject *parent) : QObject(parent) {}

void ExtensionManager::setOps(Ops ops) {
    m_ops = std::move(ops);
}

ExtensionManager::FileState ExtensionManager::readFile(const QString &path, const QString &source,
                                                       QByteArray *bytes) {
    FileState state;
    state.path = path;
    state.source = source;
    const QFileInfo info(path);
    if (!info.exists())
        return state; // 不存在：空列表，不创建
    state.exists = true;
    state.size = info.size();
    state.mtime = info.lastModified().toMSecsSinceEpoch();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        state.ok = false;
        state.error = QStringLiteral("无法读取：%1").arg(file.errorString());
        return state;
    }
    const QByteArray data = file.readAll();
    file.close();
    state.sha256 = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    if (bytes)
        *bytes = data;

    QString text;
    bool bom = false;
    if (!decode(data, &text, &bom)) {
        state.ok = false;
        state.error = QStringLiteral("不是有效的 UTF-8 文本");
        return state;
    }
    const PatchYaml::ParseResult parsed = PatchYaml::parse(text);
    if (!parsed.ok) {
        state.ok = false;
        state.error = parsed.error;
        return state;
    }
    for (const PatchYaml::PatchRow &row : parsed.rows) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), row.id);
        m.insert(QStringLiteral("name"), row.displayName());
        m.insert(QStringLiteral("serverName"), row.serverName);
        m.insert(QStringLiteral("transport"), row.transport);
        m.insert(QStringLiteral("command"), row.command);
        m.insert(QStringLiteral("url"), row.url);
        m.insert(QStringLiteral("target"), row.command.isEmpty() ? row.url : row.command);
        m.insert(QStringLiteral("enabled"), !row.disabled);
        m.insert(QStringLiteral("readOnly"), row.readOnly);
        m.insert(QStringLiteral("readOnlyReason"), row.readOnlyReason);
        m.insert(QStringLiteral("line"), row.line + 1);
        m.insert(QStringLiteral("source"), source);
        m.insert(QStringLiteral("path"), path);
        state.rows.append(m);
    }
    return state;
}

QVariantMap ExtensionManager::load(const QString &home, const QString &profile, const QString &workspace) {
    m_home = home;
    m_resolvedHome = m_ops.resolveHome ? m_ops.resolveHome(home) : home;
    m_profile = profile;
    m_workspace = workspace;
    m_loaded = true;
    reload();
    reloadSkills();
    return {{QStringLiteral("home"), home},
            {QStringLiteral("profile"), profile},
            {QStringLiteral("mcp"), mcpState()},
            {QStringLiteral("skills"), skillsState()}};
}

void ExtensionManager::reloadSkills() {
    m_skillRoots.clear();
    m_skillRows.clear();
    if (!m_loaded || m_resolvedHome.isEmpty())
        return;
    m_skillRoots.append({QDir::cleanPath(m_resolvedHome + QStringLiteral("/skills")), QStringLiteral("home")});
    if (!m_workspace.isEmpty()) {
        const QString ws = QDir::cleanPath(QDir(m_workspace).absolutePath());
        m_skillRoots.append({ws + QStringLiteral("/.dsh/skills"), QStringLiteral("workspace-dsh")});
        m_skillRoots.append({ws + QStringLiteral("/.agents/skills"), QStringLiteral("workspace-agents")});
    }
    for (SkillRoot &r : m_skillRoots) {
        r.exists = QFileInfo(r.path).isDir();
        if (!r.exists)
            continue; // 不存在：空列表，不创建
        scanSkills(r.path, r.path, r.source, true, &m_skillRows);
        scanSkills(r.path + QLatin1Char('/') + kDisabledDir, r.path, r.source, false, &m_skillRows);
    }
}

QVariantMap ExtensionManager::skillsState() const {
    QVariantList roots;
    for (const SkillRoot &r : m_skillRoots) {
        roots.append(QVariantMap{{QStringLiteral("path"), r.path},
                                 {QStringLiteral("source"), r.source},
                                 {QStringLiteral("exists"), r.exists}});
    }
    QVariantList rows = m_skillRows;
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        const int c = QString::compare(x.value(QStringLiteral("name")).toString(),
                                       y.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
        const int rx = sourceRank(x.value(QStringLiteral("source")).toString());
        const int ry = sourceRank(y.value(QStringLiteral("source")).toString());
        if (rx != ry)
            return rx < ry;
        return x.value(QStringLiteral("enabled")).toBool() && !y.value(QStringLiteral("enabled")).toBool();
    });
    const bool ok = m_loaded && !m_resolvedHome.isEmpty();
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("error"), ok ? QString() : QStringLiteral("未选择 DSH_HOME 与 profile")},
            {QStringLiteral("roots"), roots},
            {QStringLiteral("rows"), rows}};
}

QStringList ExtensionManager::skillAffected(const QString &source) const {
    if (!m_ops.instances)
        return {};
    const QString defaultHome = m_ops.resolveHome ? m_ops.resolveHome({}) : QString();
    QStringList ids;
    for (const Instance &item : m_ops.instances()) {
        if (!isStartingOrRunning(item.status))
            continue;
        if (source == QLatin1String("home")) {
            // <DSH_HOME>/skills 作用于该 home 下的全部泊位
            const QString itemHome = item.dshHome.isEmpty() ? defaultHome : item.dshHome;
            if (AffectedSet::sameHome(itemHome, m_resolvedHome))
                ids.append(item.id);
        } else if (!item.workspace.isEmpty() && AffectedSet::sameHome(item.workspace, m_workspace)) {
            // workspace 的 skills 作用于使用该 workspace 的泊位
            ids.append(item.id);
        }
    }
    return ids;
}

QVariantMap ExtensionManager::setSkillEnabled(const QString &root, const QString &entry, bool enabled) {
    const QString verb = enabled ? QStringLiteral("启用") : QStringLiteral("禁用");
    if (!m_loaded)
        return result(false, QStringLiteral("扩展页尚未加载"));
    const SkillRoot *rootState = nullptr;
    for (const SkillRoot &r : m_skillRoots) {
        if (r.path == root)
            rootState = &r;
    }
    if (!rootState)
        return result(false, QStringLiteral("不是受管理的 Skills 目录：%1").arg(root));
    if (entry.isEmpty() || entry.startsWith(QLatin1Char('.')) || entry.contains(QLatin1Char('/'))
        || entry.contains(QLatin1Char('\\')))
        return result(false, QStringLiteral("无效的 Skill 条目：%1").arg(entry));
    const QString source = rootState->source;

    const QString disabledDir = root + QLatin1Char('/') + kDisabledDir;
    const QString activePath = root + QLatin1Char('/') + entry;
    const QString parkedPath = disabledDir + QLatin1Char('/') + entry;
    const QString src = enabled ? parkedPath : activePath;
    const QString dst = enabled ? activePath : parkedPath;

    if (!pathExists(src)) {
        reloadSkills();
        emit skillsChanged(m_home, m_profile);
        if (pathExists(dst))
            return result(true); // 已是目标状态
        return result(false, QStringLiteral("Skill 已被外部移动或删除，已重新读取，请重新操作"), true);
    }
    if (pathExists(dst))
        return result(false, QStringLiteral("%1失败：目标位置已存在同名条目 %2").arg(verb, dst));

    bool createdDir = false;
    if (!enabled && !QFileInfo(disabledDir).isDir()) {
        if (!QDir().mkpath(disabledDir))
            return result(false, QStringLiteral("%1失败：无法创建 %2").arg(verb, disabledDir));
        createdDir = true;
    }
    if (!QDir().rename(src, dst)) {
        if (createdDir)
            QDir(root).rmdir(kDisabledDir);
        return result(false, QStringLiteral("%1失败：无法把 %2 移到 %3（可能被占用或没有权限）").arg(verb, src, dst));
    }
    if (enabled)
        QDir(root).rmdir(kDisabledDir); // 仅在已空时生效

    reloadSkills();
    emit skillsChanged(m_home, m_profile);
    bool confirmed = false;
    const QString id = root + QLatin1Char('|') + entry;
    for (const QVariant &v : m_skillRows) {
        const QVariantMap row = v.toMap();
        if (row.value(QStringLiteral("id")).toString() == id
            && row.value(QStringLiteral("enabled")).toBool() == enabled) {
            confirmed = true;
            break;
        }
    }
    if (!confirmed)
        return result(false, QStringLiteral("%1后重新读取，状态与预期不一致").arg(verb));

    const QStringList ids = skillAffected(source);
    if (!ids.isEmpty())
        emit restartHintRequested(m_home, m_profile, ids);
    return result(true);
}

void ExtensionManager::reload() {
    m_files.clear();
    if (!m_loaded || m_resolvedHome.isEmpty())
        return;
    const QString profileFile = m_resolvedHome + QStringLiteral("/profiles/") + m_profile + QLatin1Char('/') + kPatchFile;
    const QString homeFile = m_resolvedHome + QLatin1Char('/') + kPatchFile;
    m_files.append(readFile(profileFile, QStringLiteral("profile")));
    m_files.append(readFile(homeFile, QStringLiteral("home")));
}

QVariantMap ExtensionManager::mcpState() const {
    bool ok = true;
    QStringList errors;
    QVariantList files;
    QVariantList rows;
    for (const FileState &f : m_files) {
        files.append(QVariantMap{{QStringLiteral("path"), f.path},
                                 {QStringLiteral("source"), f.source},
                                 {QStringLiteral("exists"), f.exists},
                                 {QStringLiteral("ok"), f.ok},
                                 {QStringLiteral("error"), f.error}});
        if (!f.ok) {
            ok = false;
            errors.append(QStringLiteral("%1：%2").arg(f.path, f.error));
        }
        rows.append(f.rows);
    }
    if (!m_loaded || m_resolvedHome.isEmpty()) {
        ok = false;
        errors.append(QStringLiteral("未选择 DSH_HOME 与 profile"));
    }
    // 名称升序（不区分大小写）；同名时 profile 文件在前，再按行号
    std::stable_sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        const int c = QString::compare(x.value(QStringLiteral("name")).toString(),
                                       y.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
        const bool xp = x.value(QStringLiteral("source")).toString() == QLatin1String("profile");
        const bool yp = y.value(QStringLiteral("source")).toString() == QLatin1String("profile");
        if (xp != yp)
            return xp;
        return x.value(QStringLiteral("line")).toInt() < y.value(QStringLiteral("line")).toInt();
    });
    return {{QStringLiteral("ok"), ok},
            {QStringLiteral("writable"), ok},
            {QStringLiteral("error"), errors.join(QLatin1Char('\n'))},
            {QStringLiteral("files"), files},
            {QStringLiteral("rows"), rows}};
}

ExtensionManager::FileState *ExtensionManager::fileFor(const QString &path) {
    for (FileState &f : m_files) {
        if (f.path == path)
            return &f;
    }
    return nullptr;
}

QStringList ExtensionManager::affected(const QString &source) const {
    if (!m_ops.instances)
        return {};
    const QString defaultHome = m_ops.resolveHome ? m_ops.resolveHome({}) : QString();
    const QList<Instance> items = m_ops.instances();
    if (source == QLatin1String("profile"))
        return AffectedSet::affected(items, m_resolvedHome, m_profile, defaultHome);
    // $DSH_HOME/cordis.patch.yml 作用于该 home 下的全部 profile
    QStringList ids;
    for (const Instance &item : items) {
        const QString itemHome = item.dshHome.isEmpty() ? defaultHome : item.dshHome;
        if (AffectedSet::sameHome(itemHome, m_resolvedHome) && isStartingOrRunning(item.status))
            ids.append(item.id);
    }
    return ids;
}

QVariantMap ExtensionManager::setMcpEnabled(const QString &path, const QString &rowId, bool enabled) {
    const QString verb = enabled ? QStringLiteral("启用") : QStringLiteral("禁用");
    if (!m_loaded)
        return result(false, QStringLiteral("扩展页尚未加载"));
    if (!mcpState().value(QStringLiteral("writable")).toBool())
        return result(false, QStringLiteral("配置文件无法解析，已禁用写操作"));
    FileState *state = fileFor(path);
    if (!state || !state->exists)
        return result(false, QStringLiteral("找不到配置文件：%1").arg(path));

    // 1. 与打开时的快照比对（mtime、大小、SHA-256）
    const FileState snapshot = *state;
    QByteArray bytes;
    const FileState now = readFile(path, snapshot.source, &bytes);
    if (!now.exists || now.size != snapshot.size || now.mtime != snapshot.mtime || now.sha256 != snapshot.sha256) {
        reload();
        emit mcpChanged(m_home, m_profile);
        return result(false, QStringLiteral("配置已被外部修改，已重新读取，请重新操作"), true);
    }

    // 2. 行级改写
    QString text;
    bool bom = false;
    if (!decode(bytes, &text, &bom))
        return result(false, QStringLiteral("不是有效的 UTF-8 文本"));
    QString error;
    const QString updated = PatchYaml::setDisabled(text, rowId, !enabled, &error);
    if (!error.isEmpty())
        return result(false, QStringLiteral("%1失败：%2").arg(verb, error));
    if (updated == text)
        return result(true); // 已是目标状态，不写入、不提示重启

    // 3. QSaveFile 原子写入；失败时原文件不变
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return result(false, QStringLiteral("%1失败：无法写入 %2（%3）").arg(verb, path, out.errorString()));
    const QByteArray data = encode(updated, bom);
    if (out.write(data) != data.size()) {
        const QString reason = out.errorString();
        out.cancelWriting();
        return result(false, QStringLiteral("%1失败：写入 %2 出错（%3）").arg(verb, path, reason));
    }
    if (!out.commit())
        return result(false, QStringLiteral("%1失败：保存 %2 出错（%3）").arg(verb, path, out.errorString()));

    // 4. 重新读取并核对该条目状态
    reload();
    emit mcpChanged(m_home, m_profile);
    bool confirmed = false;
    if (FileState *f = fileFor(path)) {
        for (const QVariant &v : f->rows) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("id")).toString() == rowId) {
                confirmed = row.value(QStringLiteral("enabled")).toBool() == enabled;
                break;
            }
        }
    }
    if (!confirmed)
        return result(false, QStringLiteral("%1后重新读取，状态与预期不一致").arg(verb));

    const QStringList ids = affected(snapshot.source);
    if (!ids.isEmpty())
        emit restartHintRequested(m_home, m_profile, ids);
    return result(true);
}
