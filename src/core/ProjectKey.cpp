#include "core/ProjectKey.h"

namespace ProjectKey {

namespace {

bool isSafe(char16_t c)
{
    return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9')
        || c == u'.' || c == u'_' || c == u'-';
}

} // namespace

QString projectKey(const QString &cwd)
{
    if (cwd.isEmpty())
        return QString();

    QString readable;
    readable.reserve(cwd.size());
    bool separatorRun = false;
    for (const QChar ch : cwd) {
        const char16_t c = ch.unicode();
        if (c == u'/' || c == u'\\' || c == u':') {
            if (!separatorRun)
                readable += QLatin1Char('-');
            separatorRun = true;
        } else if (isSafe(c)) {
            readable += ch;
            separatorRun = false;
        } else {
            readable += QLatin1Char('~')
                + QString::number(uint(c), 16).toUpper().rightJustified(4, QLatin1Char('0'));
            separatorRun = false;
        }
    }

    // 对应 readable.replace(/^-+/, '') || 'root'
    qsizetype start = 0;
    while (start < readable.size() && readable.at(start) == QLatin1Char('-'))
        ++start;
    QString slug = readable.mid(start);
    if (slug.isEmpty())
        slug = QStringLiteral("root");

    return QStringLiteral("--") + slug.left(251) + QStringLiteral("--");
}

} // namespace ProjectKey
