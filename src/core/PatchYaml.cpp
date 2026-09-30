#include "core/PatchYaml.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace PatchYaml {

namespace {

struct Line {
    QString body; // 不含换行符
    QString eol;  // "\n"、"\r\n" 或空（末行无换行）
};

QList<Line> splitLines(const QString &text)
{
    QList<Line> lines;
    qsizetype pos = 0;
    while (pos < text.size()) {
        const qsizetype nl = text.indexOf(QLatin1Char('\n'), pos);
        if (nl < 0) {
            lines.append(Line{text.mid(pos), QString()});
            break;
        }
        QString body = text.mid(pos, nl - pos);
        QString eol = QStringLiteral("\n");
        if (body.endsWith(QLatin1Char('\r'))) {
            body.chop(1);
            eol = QStringLiteral("\r\n");
        }
        lines.append(Line{body, eol});
        pos = nl + 1;
    }
    return lines;
}

QString joinLines(const QList<Line> &lines)
{
    QString out;
    for (const Line &l : lines) {
        out += l.body;
        out += l.eol;
    }
    return out;
}

int indentOf(const QString &s)
{
    int i = 0;
    while (i < s.size() && s.at(i) == QLatin1Char(' '))
        ++i;
    return i;
}

// 空行或纯注释行
bool isTrivia(const QString &s)
{
    const int i = indentOf(s);
    return i >= s.size() || s.at(i) == QLatin1Char('#');
}

enum class Kind { Empty, Plain, Quoted, Tagged, Anchor, Alias, Flow, Block, Unterminated };

struct Scalar {
    Kind kind = Kind::Empty;
    QString value; // Plain/Quoted 为去引号后的值；其他类型为原文
    int start = 0; // 值在行内的起止位置（不含尾随空格与注释）
    int end = 0;
};

Scalar scanScalar(const QString &s, int from)
{
    Scalar r;
    int i = from;
    while (i < s.size() && s.at(i) == QLatin1Char(' '))
        ++i;
    r.start = r.end = i;
    if (i >= s.size() || s.at(i) == QLatin1Char('#'))
        return r;

    const QChar c = s.at(i);
    if (c == QLatin1Char('\'') || c == QLatin1Char('"')) {
        const bool single = c == QLatin1Char('\'');
        QString v;
        int j = i + 1;
        for (;;) {
            if (j >= s.size()) {
                r.kind = Kind::Unterminated;
                r.end = int(s.size());
                r.value = s.mid(i);
                return r;
            }
            const QChar ch = s.at(j);
            if (single && ch == QLatin1Char('\'')) {
                if (j + 1 < s.size() && s.at(j + 1) == QLatin1Char('\'')) {
                    v += QLatin1Char('\'');
                    j += 2;
                    continue;
                }
                break;
            }
            if (!single && ch == QLatin1Char('\\') && j + 1 < s.size()) {
                v += s.at(j + 1);
                j += 2;
                continue;
            }
            if (!single && ch == QLatin1Char('"'))
                break;
            v += ch;
            ++j;
        }
        r.kind = Kind::Quoted;
        r.value = v;
        r.end = j + 1;
        return r;
    }

    int j = i;
    while (j < s.size()) {
        if (s.at(j) == QLatin1Char('#') && j > i && s.at(j - 1) == QLatin1Char(' '))
            break;
        ++j;
    }
    int e = j;
    while (e > i && s.at(e - 1) == QLatin1Char(' '))
        --e;
    r.end = e;
    r.value = s.mid(i, e - i);
    if (c == QLatin1Char('!'))
        r.kind = Kind::Tagged;
    else if (c == QLatin1Char('&'))
        r.kind = Kind::Anchor;
    else if (c == QLatin1Char('*'))
        r.kind = Kind::Alias;
    else if (c == QLatin1Char('{') || c == QLatin1Char('['))
        r.kind = Kind::Flow;
    else if (c == QLatin1Char('|') || c == QLatin1Char('>'))
        r.kind = Kind::Block;
    else
        r.kind = Kind::Plain;
    return r;
}

struct KeyLine {
    int line = -1;
    QString key;
    Scalar val;
};

// 从 s 的 pos 处解析 `key: value`；不是键行时返回 false
bool parseKeyLine(const QString &s, int pos, int lineNo, KeyLine *out)
{
    static const QRegularExpression re(
        QStringLiteral("([A-Za-z_][A-Za-z0-9_-]*|<<)[ ]*:(?=[ ]|$)")); // 由 AnchorAtOffsetMatchOption 锚定在 pos
    const QRegularExpressionMatch m =
        re.match(s, pos, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch())
        return false;
    out->line = lineNo;
    out->key = m.captured(1);
    out->val = scanScalar(s, int(m.capturedEnd(0)));
    return true;
}

// 行内是否以锚点、别名或合并键开头（形如 `key: &a`、`- *a`、`<<: *base`）
bool hasAnchorSyntax(const QString &s)
{
    static const QRegularExpression re(
        QStringLiteral("^[ ]*(-[ ]+)*(([A-Za-z_][A-Za-z0-9_-]*|'[^']*'|\"[^\"]*\")[ ]*:[ ]+)?[&*][^ ]|^[ ]*(-[ ]+)*<<[ ]*:"));
    return re.match(s).hasMatch();
}

struct Item {
    int dashLine = -1;
    int keyIndent = 0;
    int end = 0; // 最后一行内容的下一行（不含尾随注释）
    QList<KeyLine> keys;
    bool flow = false;
    QString flowText;
    bool anchor = false;
};

struct RowInternal {
    PatchRow row;
    int nameLine = -1;
    int keyIndent = 0;
    int disabledLine = -1;
    Scalar disabledVal;
};

void markReadOnly(PatchRow &row, const QString &reason)
{
    if (row.readOnly)
        return;
    row.readOnly = true;
    row.readOnlyReason = reason;
}

bool isTrue(const QString &v)
{
    return v == QLatin1String("true") || v == QLatin1String("True") || v == QLatin1String("TRUE");
}

bool isFalse(const QString &v)
{
    return v == QLatin1String("false") || v == QLatin1String("False") || v == QLatin1String("FALSE");
}

// 读取条目级 config 块下的摘要字段
void readConfig(const QList<Line> &lines, const KeyLine &cfg, const Item &item, PatchRow &row)
{
    int childIndent = -1;
    for (int j = cfg.line + 1; j < item.end; ++j) {
        const QString &s = lines.at(j).body;
        if (isTrivia(s))
            continue;
        const int ind = indentOf(s);
        if (ind <= item.keyIndent)
            break;
        if (childIndent < 0)
            childIndent = ind;
        if (ind != childIndent)
            continue;
        KeyLine kl;
        if (!parseKeyLine(s, ind, j, &kl))
            continue;
        const bool scalar = kl.val.kind == Kind::Plain || kl.val.kind == Kind::Quoted
            || kl.val.kind == Kind::Tagged;
        if (!scalar)
            continue;
        if (kl.key == QLatin1String("serverName"))
            row.serverName = kl.val.value;
        else if (kl.key == QLatin1String("transport"))
            row.transport = kl.val.value;
        else if (kl.key == QLatin1String("command"))
            row.command = kl.val.value;
        else if (kl.key == QLatin1String("url"))
            row.url = kl.val.value;
    }
}

QString flowId(const QString &flowText)
{
    static const QRegularExpression re(QStringLiteral("[{,][ ]*id[ ]*:[ ]*['\"]?([^,'\"}\\s]+)"));
    const QRegularExpressionMatch m = re.match(flowText);
    return m.hasMatch() ? m.captured(1) : QString();
}

bool parseImpl(const QList<Line> &lines, QList<RowInternal> *rows, QString *error)
{
    // 基础检查：缩进中的制表符、整个文档为 flow 风格
    bool firstContent = true;
    for (int i = 0; i < lines.size(); ++i) {
        const QString &s = lines.at(i).body;
        int k = 0;
        while (k < s.size() && (s.at(k) == QLatin1Char(' ') || s.at(k) == QLatin1Char('\t')))
            ++k;
        if (k >= s.size() || s.at(k) == QLatin1Char('#'))
            continue;
        if (s.left(k).contains(QLatin1Char('\t'))) {
            *error = QStringLiteral("第 %1 行：缩进中含制表符").arg(i + 1);
            return false;
        }
        if (firstContent) {
            firstContent = false;
            if (s.at(k) == QLatin1Char('{') || s.at(k) == QLatin1Char('[')) {
                *error = QStringLiteral("第 %1 行：文件为 flow 风格，无法按行解析").arg(i + 1);
                return false;
            }
        }
    }

    const QString mcpName = mcpClientName();

    for (int i = 0; i < lines.size(); ++i) {
        const QString &s = lines.at(i).body;
        if (isTrivia(s))
            continue;
        const int dash = indentOf(s);
        if (s.at(dash) != QLatin1Char('-'))
            continue;
        if (dash + 1 < s.size() && s.at(dash + 1) != QLatin1Char(' '))
            continue; // 如 `---` 或 `-x`

        Item item;
        item.dashLine = i;
        int r = dash + 1;
        while (r < s.size() && s.at(r) == QLatin1Char(' '))
            ++r;

        if (r < s.size() && s.at(r) == QLatin1Char('&')) {
            item.anchor = true;
            while (r < s.size() && s.at(r) != QLatin1Char(' '))
                ++r;
            while (r < s.size() && s.at(r) == QLatin1Char(' '))
                ++r;
        }

        const bool restEmpty = r >= s.size() || s.at(r) == QLatin1Char('#');
        if (restEmpty) {
            // `-` 单独成行，键从下一行开始
            int next = i + 1;
            while (next < lines.size() && isTrivia(lines.at(next).body))
                ++next;
            if (next >= lines.size() || indentOf(lines.at(next).body) <= dash)
                continue;
            item.keyIndent = indentOf(lines.at(next).body);
            if (lines.at(next).body.at(item.keyIndent) == QLatin1Char('-'))
                continue;
        } else if (s.at(r) == QLatin1Char('{')) {
            item.flow = true;
            item.keyIndent = r;
            item.flowText = s.mid(r);
        } else if (s.at(r) == QLatin1Char('-') || s.at(r) == QLatin1Char('*')
                   || s.at(r) == QLatin1Char('[')) {
            continue; // 嵌套序列、别名或 flow 序列：不是映射条目
        } else {
            KeyLine first;
            if (!parseKeyLine(s, r, i, &first))
                continue;
            item.keyIndent = r;
            item.keys.append(first);
        }

        int lastContent = i;
        for (int j = i + 1; j < lines.size(); ++j) {
            const QString &t = lines.at(j).body;
            if (isTrivia(t))
                continue;
            const int ind = indentOf(t);
            if (ind < item.keyIndent || (item.flow && ind <= dash))
                break;
            lastContent = j;
            if (item.flow) {
                item.flowText += QLatin1Char(' ') + t.mid(ind);
                continue;
            }
            if (ind > item.keyIndent || t.at(ind) == QLatin1Char('-'))
                continue;
            KeyLine kl;
            if (parseKeyLine(t, ind, j, &kl))
                item.keys.append(kl);
        }
        item.end = lastContent + 1;

        // 判断是否为 mcp-client 条目
        if (item.flow) {
            if (!item.flowText.contains(mcpName))
                continue;
            RowInternal ri;
            ri.row.id = flowId(item.flowText);
            ri.row.line = i;
            ri.keyIndent = item.keyIndent;
            markReadOnly(ri.row, QStringLiteral("该条目为 flow 风格，无法按行改写"));
            rows->append(ri);
            continue;
        }

        const KeyLine *nameKey = nullptr;
        for (const KeyLine &kl : item.keys) {
            if (kl.key == QLatin1String("name")) {
                nameKey = &kl;
                break;
            }
        }
        if (!nameKey)
            continue;
        const bool nameScalar = nameKey->val.kind == Kind::Plain || nameKey->val.kind == Kind::Quoted;
        if (!nameScalar || nameKey->val.value != mcpName)
            continue;

        RowInternal ri;
        ri.row.line = i;
        ri.nameLine = nameKey->line;
        ri.keyIndent = item.keyIndent;
        PatchRow &row = ri.row;

        if (item.anchor)
            markReadOnly(row, QStringLiteral("该条目含 YAML 锚点，改写可能影响引用它的内容"));
        for (int j = i; j < item.end; ++j) {
            if (j == i && item.anchor)
                continue;
            if (hasAnchorSyntax(lines.at(j).body)) {
                markReadOnly(row, QStringLiteral("该条目含 YAML 锚点、别名或合并键（第 %1 行），无法安全改写").arg(j + 1));
                break;
            }
        }

        int disabledCount = 0;
        for (const KeyLine &kl : item.keys) {
            if (kl.val.kind == Kind::Unterminated)
                markReadOnly(row, QStringLiteral("第 %1 行的值跨多行，无法按行改写").arg(kl.line + 1));
            if (kl.key == QLatin1String("id")) {
                if (kl.val.kind == Kind::Plain || kl.val.kind == Kind::Quoted)
                    row.id = kl.val.value;
            } else if (kl.key == QLatin1String("disabled")) {
                ++disabledCount;
                ri.disabledLine = kl.line;
                ri.disabledVal = kl.val;
            } else if (kl.key == QLatin1String("config")) {
                if (kl.val.kind == Kind::Empty)
                    readConfig(lines, kl, item, row);
                else if (kl.val.kind == Kind::Flow)
                    markReadOnly(row, QStringLiteral("config 为 flow 风格，无法读取服务信息"));
            }
        }

        if (disabledCount > 1) {
            markReadOnly(row, QStringLiteral("disabled 键重复"));
        } else if (disabledCount == 1) {
            const Scalar &v = ri.disabledVal;
            if (v.kind == Kind::Plain && isTrue(v.value)) {
                row.disabled = true;
            } else if (v.kind == Kind::Plain && isFalse(v.value)) {
                row.disabled = false;
            } else if (v.kind == Kind::Tagged && v.value.startsWith(QLatin1String("!!js"))) {
                markReadOnly(row, QStringLiteral("disabled 为 !!js 表达式，由运行时求值，不能用开关修改"));
            } else if (v.kind == Kind::Tagged) {
                markReadOnly(row, QStringLiteral("disabled 带有 YAML 标签，不能用开关修改"));
            } else if (v.kind == Kind::Empty) {
                markReadOnly(row, QStringLiteral("disabled 值为空，不能用开关修改"));
            } else {
                markReadOnly(row, QStringLiteral("disabled 的值无法识别，不能用开关修改"));
            }
        }

        if (row.id.isEmpty())
            markReadOnly(row, QStringLiteral("缺少 id，无法定位该条目"));
        rows->append(ri);
    }

    // id 重复的 MCP 行全部只读
    QHash<QString, int> idCount;
    for (const RowInternal &ri : *rows) {
        if (!ri.row.id.isEmpty())
            ++idCount[ri.row.id];
    }
    for (RowInternal &ri : *rows) {
        if (!ri.row.id.isEmpty() && idCount.value(ri.row.id) > 1)
            markReadOnly(ri.row, QStringLiteral("id \"%1\" 重复，无法确定要改写的条目").arg(ri.row.id));
    }
    return true;
}

} // namespace

QString mcpClientName()
{
    return QStringLiteral("@deepseek-ai/dsh-mcp-client");
}

ParseResult parse(const QString &text)
{
    ParseResult result;
    QList<RowInternal> internals;
    if (!parseImpl(splitLines(text), &internals, &result.error))
        return result;
    result.ok = true;
    for (const RowInternal &ri : internals)
        result.rows.append(ri.row);
    std::stable_sort(result.rows.begin(), result.rows.end(), [](const PatchRow &a, const PatchRow &b) {
        const int c = a.displayName().compare(b.displayName(), Qt::CaseInsensitive);
        if (c != 0)
            return c < 0;
        return a.id < b.id;
    });
    return result;
}

QString setDisabled(const QString &text, const QString &rowId, bool disabled, QString *error)
{
    auto fail = [&](const QString &why) {
        if (error)
            *error = why;
        return text;
    };
    if (error)
        error->clear();

    QList<Line> lines = splitLines(text);
    QList<RowInternal> internals;
    QString parseError;
    if (!parseImpl(lines, &internals, &parseError))
        return fail(parseError);

    const RowInternal *target = nullptr;
    for (const RowInternal &ri : internals) {
        if (!rowId.isEmpty() && ri.row.id == rowId) {
            target = &ri;
            break;
        }
    }
    if (!target)
        return fail(QStringLiteral("未找到 id 为 \"%1\" 的 MCP 条目").arg(rowId));
    if (target->row.readOnly)
        return fail(target->row.readOnlyReason);
    if (target->row.disabled == disabled)
        return text;

    if (disabled) {
        if (target->disabledLine >= 0) {
            // 已有 disabled: false：就地改值，保留行内其他内容（如注释）
            Line &l = lines[target->disabledLine];
            const Scalar &v = target->disabledVal;
            l.body = l.body.left(v.start) + QStringLiteral("true") + l.body.mid(v.end);
        } else {
            QString fileEol = QStringLiteral("\n");
            for (const Line &l : lines) {
                if (!l.eol.isEmpty()) {
                    fileEol = l.eol;
                    break;
                }
            }
            Line &nameLine = lines[target->nameLine];
            Line inserted{QString(target->keyIndent, QLatin1Char(' ')) + QStringLiteral("disabled: true"),
                          nameLine.eol};
            if (nameLine.eol.isEmpty()) {
                nameLine.eol = fileEol;
                inserted.eol.clear();
            }
            lines.insert(target->nameLine + 1, inserted);
        }
    } else {
        // 当前为 disabled: true：删除该行
        const int d = target->disabledLine;
        if (lines.at(d).eol.isEmpty() && d > 0)
            lines[d - 1].eol.clear();
        lines.removeAt(d);
    }
    return joinLines(lines);
}

} // namespace PatchYaml
