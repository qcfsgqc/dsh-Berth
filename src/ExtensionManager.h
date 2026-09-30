#pragma once

#include "Instance.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// 扩展页后端：某个 (DSH_HOME, profile) 的 Skills 与 MCP_Server 列表和启用开关。
// MCP：读取 <home>/profiles/<profile>/cordis.patch.yml 与 <home>/cordis.patch.yml（后者优先级更高），
// 只识别 name 为 '@deepseek-ai/dsh-mcp-client' 的条目（PatchYaml）。开关只改目标条目的 disabled 行。
// - load 时为每个文件记下快照（是否存在、大小、mtime、SHA-256）；写入前重新比对，
//   有外部修改就放弃写入、重新读取并发 mcpChanged
// - 用 QSaveFile 写入，失败时文件保持不变、返回失败原因（界面重新读取即可把开关退回）
// - 任一文件无法解析时整页不可写；文件不存在时视为空列表，不创建文件
// - 成功后按需求 4 找出受影响泊位，非空时发 restartHintRequested
// Skills（待定 TODO #1，暂按方案 A）：扫描 <home>/skills（跳过以 '.' 开头的项，含 .system）、
// <workspace>/.dsh/skills、<workspace>/.agents/skills 的第一层：<name>/SKILL.md 或 <name>.md。
// 禁用 = 移到 <root>/.berth-disabled/，启用 = 移回；移动失败时不改动、返回失败原因。
class ExtensionManager : public QObject {
    Q_OBJECT

public:
    struct Ops {
        // 空串 → 默认 DSH_HOME；非空 → 规范化后的绝对路径
        std::function<QString(const QString &)> resolveHome;
        std::function<QList<Instance>()> instances;
    };

    explicit ExtensionManager(QObject *parent = nullptr);
    void setOps(Ops ops);

    // 打开（或刷新）扩展页：读取两个 cordis.patch.yml 并记快照。workspace 供 Skills（21.5）使用。
    // 返回 {home, profile, mcp, skills}，结构见 mcpState() / skillsState()
    Q_INVOKABLE QVariantMap load(const QString &home, const QString &profile, const QString &workspace);
    // 当前已加载的 MCP 状态：
    // {ok, writable, error, files: [{path, source, exists, ok, error}],
    //  rows: [{id, name, serverName, transport, command, url, target, enabled, readOnly, readOnlyReason,
    //          line, source, path}]}
    // ok=false 时 error 为"<文件路径>：<原因>"（多个文件以换行分隔），writable=false；
    // rows 按名称升序（不区分大小写），同名时 profile 文件在前。source 为 "profile" 或 "home"；
    // target 为 stdio 的 command 或 streamable-http 的 url；line 为 1 起行号
    Q_INVOKABLE QVariantMap mcpState() const;
    // 切换 MCP 条目启用状态。path 与 rowId 取自 rows 中的 path / id。
    // 返回 {ok, error, externalChange}：externalChange=true 表示文件已被外部修改、本次未写入且已重新读取
    Q_INVOKABLE QVariantMap setMcpEnabled(const QString &path, const QString &rowId, bool enabled);

    // 当前已加载的 Skills 状态：
    // {ok, error, roots: [{path, source, exists}],
    //  rows: [{id, name, entry, kind, source, root, path, enabled}]}
    // source 为 "home" / "workspace-dsh" / "workspace-agents"；entry 为根目录下的条目名（"foo" 或 "foo.md"）；
    // kind 为 "dir" / "file"；path 为条目当前绝对路径；id = root + "|" + entry。
    // rows 按名称升序（不区分大小写），同名按 home → workspace-dsh → workspace-agents、再按启用在前。
    // 根目录不存在时视为空；workspace 为空时只扫 home
    Q_INVOKABLE QVariantMap skillsState() const;
    // 切换 Skill 启用状态。root 与 entry 取自 rows 中的 root / entry。
    // 返回 {ok, error, externalChange}：externalChange=true 表示条目已不在预期位置、已重新扫描
    Q_INVOKABLE QVariantMap setSkillEnabled(const QString &root, const QString &entry, bool enabled);

signals:
    // MCP 列表已重新读取（写入成功或检测到外部修改后），界面据此刷新
    void mcpChanged(const QString &home, const QString &profile);
    // Skills 列表已重新扫描
    void skillsChanged(const QString &home, const QString &profile);
    // 给 RestartHintController：启用状态变更成功且有启动中/运行中的受影响泊位时发出
    void restartHintRequested(const QString &home, const QString &profile, const QStringList &instanceIds);

private:
    struct FileState {
        QString path;
        QString source; // "profile" / "home"
        bool exists = false;
        qint64 size = -1;
        qint64 mtime = -1;
        QByteArray sha256;
        bool ok = true;
        QString error;
        QVariantList rows;
    };

    // 读取单个文件：填充快照、解析结果；bytes 非空时写出原始内容
    static FileState readFile(const QString &path, const QString &source, QByteArray *bytes = nullptr);
    void reload();
    FileState *fileFor(const QString &path);
    QStringList affected(const QString &source) const;
    void reloadSkills();
    QStringList skillAffected(const QString &source) const;

    struct SkillRoot {
        QString path;
        QString source;
        bool exists = false;
    };

    Ops m_ops;
    QString m_home;       // 调用方传入的原值（发信号时原样带回）
    QString m_resolvedHome;
    QString m_profile;
    QString m_workspace;
    bool m_loaded = false;
    QList<FileState> m_files;
    QList<SkillRoot> m_skillRoots;
    QVariantList m_skillRows;
};
