#include "core/RunValue.h"

#include <QDir>

namespace RunValue {

QString make(const QString &exePath)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(exePath) + QStringLiteral("\" ")
        + QLatin1String(kAutostartArg);
}

Parsed parse(const QString &value)
{
    Parsed p;
    const QString v = value.trimmed();
    if (v.isEmpty())
        return p;

    QString rest;
    if (v.startsWith(QLatin1Char('"'))) {
        const qsizetype close = v.indexOf(QLatin1Char('"'), 1);
        if (close < 0)
            return p; // 引号不闭合
        p.exe = v.mid(1, close - 1).trimmed();
        rest = v.mid(close + 1);
        p.args = rest.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    } else {
        const QStringList tokens = v.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QStringList exeParts;
        qsizetype i = 0;
        for (; i < tokens.size(); ++i) {
            if (tokens.at(i).startsWith(QLatin1Char('-')))
                break;
            exeParts << tokens.at(i);
        }
        p.exe = exeParts.join(QLatin1Char(' '));
        p.args = tokens.mid(i);
    }
    // 参数里可能混有制表符等空白，统一去掉首尾空白
    for (QString &a : p.args)
        a = a.trimmed();
    p.ok = !p.exe.isEmpty();
    return p;
}

bool samePath(const QString &a, const QString &b)
{
    if (a.trimmed().isEmpty() || b.trimmed().isEmpty())
        return false;
    const QString na = QDir::cleanPath(QDir::fromNativeSeparators(a.trimmed()));
    const QString nb = QDir::cleanPath(QDir::fromNativeSeparators(b.trimmed()));
    return na.compare(nb, Qt::CaseInsensitive) == 0;
}

bool matches(const QString &value, const QString &exePath)
{
    const Parsed p = parse(value);
    if (!p.ok || !samePath(p.exe, exePath))
        return false;
    for (const QString &a : p.args) {
        if (a.compare(QLatin1String(kAutostartArg), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace RunValue
