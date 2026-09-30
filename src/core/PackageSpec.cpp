#include "core/PackageSpec.h"

namespace PackageSpec {

namespace {

bool hasWhitespace(const QString &s)
{
    for (const QChar c : s) {
        if (c.isSpace())
            return true;
    }
    return false;
}

bool isNameChar(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '-' || u == '.' || u == '_'
        || u == '~';
}

// scope 或包名主体的片段校验；what 用于错误描述
std::optional<QString> validateSegment(const QString &seg, const QString &what)
{
    if (seg.isEmpty())
        return QStringLiteral("%1不能为空").arg(what);
    if (seg.startsWith(QLatin1Char('.')) || seg.startsWith(QLatin1Char('_')))
        return QStringLiteral("%1不能以 . 或 _ 开头").arg(what);
    for (const QChar c : seg) {
        if (c.isSpace())
            return QStringLiteral("%1不能包含空格").arg(what);
        if (c.isUpper())
            return QStringLiteral("%1必须全小写").arg(what);
        if (!isNameChar(c))
            return QStringLiteral("%1包含非法字符 '%2'").arg(what, QString(c));
    }
    return std::nullopt;
}

// 把 npm 规格拆成 (包名, 后缀)。hasSuffix 表示出现了 @ 分隔符（即使后缀为空）。
void splitSpec(const QString &spec, QString *name, QString *suffix, bool *hasSuffix)
{
    qsizetype from = 0;
    if (spec.startsWith(QLatin1Char('@'))) {
        const qsizetype slash = spec.indexOf(QLatin1Char('/'));
        from = slash < 0 ? spec.size() : slash + 1;
    }
    const qsizetype at = spec.indexOf(QLatin1Char('@'), from);
    if (at < 0) {
        *name = spec;
        suffix->clear();
        *hasSuffix = false;
    } else {
        *name = spec.left(at);
        *suffix = spec.mid(at + 1);
        *hasSuffix = true;
    }
}

} // namespace

std::optional<QString> validateNpmName(const QString &name)
{
    if (name.isEmpty())
        return QStringLiteral("包名不能为空");
    if (name.size() > 214)
        return QStringLiteral("包名长度不能超过 214 个字符");
    if (name.startsWith(QLatin1Char('@'))) {
        const qsizetype slash = name.indexOf(QLatin1Char('/'));
        if (slash < 0)
            return QStringLiteral("scope 包名应为 @scope/name 形式");
        if (auto e = validateSegment(name.mid(1, slash - 1), QStringLiteral("scope ")))
            return e;
        return validateSegment(name.mid(slash + 1), QStringLiteral("包名"));
    }
    if (name.contains(QLatin1Char('/')))
        return QStringLiteral("非 scope 包名不能包含 /");
    return validateSegment(name, QStringLiteral("包名"));
}

std::optional<QString> validateNpmSpec(const QString &spec)
{
    QString name, suffix;
    bool hasSuffix = false;
    splitSpec(spec, &name, &suffix, &hasSuffix);
    if (auto e = validateNpmName(name))
        return e;
    if (hasSuffix) {
        if (suffix.isEmpty())
            return QStringLiteral("@ 后缺少版本号或 dist-tag");
        if (hasWhitespace(suffix) || suffix.contains(QLatin1Char('@')) || suffix.contains(QLatin1Char('/')))
            return QStringLiteral("版本号或 dist-tag 不能包含空白、@ 或 /");
    }
    return std::nullopt;
}

std::optional<QString> validateGitUrl(const QString &url)
{
    static const QLatin1String prefixes[] = {QLatin1String("git+https://"), QLatin1String("https://"),
                                             QLatin1String("git@")};
    for (const QLatin1String &p : prefixes) {
        if (!url.startsWith(p))
            continue;
        if (url.size() == p.size())
            return QStringLiteral("git URL 在 %1 之后缺少地址").arg(p);
        if (hasWhitespace(url))
            return QStringLiteral("git URL 不能包含空白");
        return std::nullopt;
    }
    return QStringLiteral("git URL 应以 git+https://、https:// 或 git@ 开头");
}

std::optional<QString> validateInstallInput(const QString &input)
{
    const Parsed p = parse(input);
    if (p.kind == Kind::Invalid)
        return p.error;
    return std::nullopt;
}

std::optional<QString> validateCatalogUrl(const QString &url)
{
    static const QLatin1String prefixes[] = {QLatin1String("http://"), QLatin1String("https://")};
    for (const QLatin1String &p : prefixes) {
        if (!url.startsWith(p))
            continue;
        if (url.size() == p.size())
            return QStringLiteral("目录源地址在 %1 之后缺少主机").arg(p);
        if (hasWhitespace(url))
            return QStringLiteral("目录源地址不能包含空白");
        return std::nullopt;
    }
    return QStringLiteral("目录源地址必须以 http:// 或 https:// 开头");
}

Parsed parse(const QString &input)
{
    Parsed r;
    if (!validateGitUrl(input)) {
        r.kind = Kind::Git;
        r.name = input;
        return r;
    }
    if (auto e = validateNpmSpec(input)) {
        r.kind = Kind::Invalid;
        r.error = QStringLiteral("格式错误：既不是合法的 npm 包名（%1），也不是 git URL").arg(*e);
        return r;
    }
    bool hasSuffix = false;
    splitSpec(input, &r.name, &r.suffix, &hasSuffix);
    r.kind = Kind::Npm;
    return r;
}

} // namespace PackageSpec
