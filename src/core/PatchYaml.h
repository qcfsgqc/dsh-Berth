#pragma once

// cordis.patch.yml 的行级读取与改写（纯逻辑）。只依赖 Qt6::Core。
//
// 不是完整的 YAML 解析器：只识别块序列里的条目（`- id: ...` 形式，包括嵌在
// `- insert:` 等操作下的条目），读取条目级的 `id` / `name` / `disabled` / `config`，
// 并只把 `name` 为 '@deepseek-ai/dsh-mcp-client' 的条目作为 MCP 行返回。
// 其余行（注释、其他插件、嵌套内容）一律不解释、不改动。
//
// 改写规则（setDisabled）：
// - 禁用：没有 `disabled` 键时，在 `name` 行之后插入一行 `disabled: true`（与条目键同缩进，
//   沿用该行换行符）；已有 `disabled: false` 时就地把值改为 `true`；已是 true 时原样返回。
// - 启用：`disabled: true` 时删除该行；没有该键或已是 false 时原样返回。
// - 因此对没有 `disabled` 键的行，先禁用再启用得到与原文逐字节相同的文本；
//   每次编辑只增、删或改目标条目的那一行。
//
// 只读条目（readOnly=true，setDisabled 拒绝改写）：
// - `disabled` 为 `!!js …` 等带标签的值、带引号的字符串、空值或无法识别的值；
// - 条目本身或其 `config` 为 flow 风格（`{ ... }` / `[ ... ]`）；
// - 条目中含 YAML 锚点、别名或合并键（`&a` / `*a` / `<<:`）；
// - 缺少 `id`、`id` 与其他 MCP 行重复、`disabled` 键重复、条目级值跨多行（引号未闭合）。

#include <QList>
#include <QString>

namespace PatchYaml {

// MCP 客户端插件名
QString mcpClientName();

struct PatchRow {
    QString id;         // 条目 id；缺失时为空（该行只读）
    QString serverName; // config.serverName
    QString transport;  // config.transport（stdio / streamable-http）
    QString command;    // config.command（stdio）
    QString url;        // config.url（streamable-http）
    bool disabled = false;
    bool readOnly = false;
    QString readOnlyReason; // readOnly 时的原因说明
    int line = -1;          // 条目起始行（`- ` 所在行，0 起）

    // 列表显示名：serverName，缺失时退回 id
    QString displayName() const { return serverName.isEmpty() ? id : serverName; }

    bool operator==(const PatchRow &) const = default;
};

struct ParseResult {
    bool ok = false;
    QList<PatchRow> rows; // ok 时有效：每个 mcp-client 条目恰好一行，按 displayName 升序（不区分大小写），同名按 id
    QString error;        // !ok 时的原因，含行号（1 起）
};

// 文件中没有 mcp-client 条目（含空文本）时 ok=true 且 rows 为空。
// 缩进中含制表符、或整个文档为 flow 风格时 ok=false。
ParseResult parse(const QString &text);

// 把 id 为 rowId 的 MCP 行设置为禁用（disabled=true）或启用（false），规则见文件头。
// 找不到该行、该行只读或文本无法解析时返回原文，并在 error 非空时写入原因；成功或无需改动时 error 置空。
QString setDisabled(const QString &text, const QString &rowId, bool disabled, QString *error = nullptr);

} // namespace PatchYaml
