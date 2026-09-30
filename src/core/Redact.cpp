#include "core/Redact.h"

#include <QList>
#include <QRegularExpression>

#include <algorithm>

namespace Redact {

namespace {

// 选一个不出现在任何敏感值里的掩码字符，保证掩码本身不会泄露或拼出原值
QChar pickMaskChar(const QStringList &secrets)
{
    auto usedByAny = [&secrets](QChar c) {
        for (const QString &s : secrets) {
            if (s.contains(c))
                return true;
        }
        return false;
    };

    static const QChar candidates[] = {u'*', u'#', u'~', u'x', u'?'};
    for (QChar c : candidates) {
        if (!usedByAny(c))
            return c;
    }
    // 常见候选都被占用时，从方块符号区往后找一个未用字符
    for (char16_t code = 0x2588; code < 0xD800; ++code) {
        const QChar c(code);
        if (!usedByAny(c))
            return c;
    }
    return QChar(u'*');
}

} // namespace

QString redact(const QString &text, const QStringList &secrets)
{
    // 去空、去重，较长的值优先
    QStringList values;
    for (const QString &s : secrets) {
        if (!s.isEmpty() && !values.contains(s))
            values.append(s);
    }
    if (values.isEmpty() || text.isEmpty())
        return text;
    std::stable_sort(values.begin(), values.end(),
                     [](const QString &a, const QString &b) { return a.size() > b.size(); });

    // 在原文上标记所有命中位置（包括重叠出现），之后统一替换
    QList<bool> masked(text.size(), false);
    bool any = false;
    for (const QString &v : values) {
        qsizetype from = 0;
        while (true) {
            const qsizetype pos = text.indexOf(v, from);
            if (pos < 0)
                break;
            for (qsizetype i = pos; i < pos + v.size(); ++i)
                masked[i] = true;
            any = true;
            from = pos + 1;
        }
    }
    if (!any)
        return text;

    const QString mask(3, pickMaskChar(values));
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        if (masked[i]) {
            // 连续的命中区间合并为一个掩码
            while (i < text.size() && masked[i])
                ++i;
            out += mask;
        } else {
            out += text[i];
            ++i;
        }
    }
    return out;
}

QString redactProxyUrl(const QString &text)
{
    // 贪婪匹配 [^\s/?#]* 后回溯到 authority 内最后一个 '@'
    static const QRegularExpression re(
        QStringLiteral("([A-Za-z][A-Za-z0-9+.\\-]*://)([^\\s/?#]*)@"));

    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(text).mid(last, m.capturedStart() - last);
        out += m.captured(1);
        out += m.captured(2).contains(u':') ? QStringLiteral("***:***@") : QStringLiteral("***@");
        last = m.capturedEnd();
    }
    if (last == 0)
        return text;
    out += QStringView(text).mid(last);
    return out;
}

} // namespace Redact
