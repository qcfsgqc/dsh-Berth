#include "core/SemVer.h"

#include <QList>

#include <algorithm>

namespace {

constexpr quint64 kMaxSafe = 9007199254740991ULL; // 2^53 - 1

bool isDigits(const QString &s)
{
    if (s.isEmpty())
        return false;
    for (QChar c : s) {
        if (c < u'0' || c > u'9')
            return false;
    }
    return true;
}

bool isIdentChar(QChar c)
{
    return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z')
        || c == u'-';
}

std::optional<quint64> parseNum(const QString &s)
{
    if (!isDigits(s) || (s.size() > 1 && s.at(0) == u'0') || s.size() > 16)
        return std::nullopt;
    bool ok = false;
    const quint64 v = s.toULongLong(&ok);
    if (!ok || v > kMaxSafe)
        return std::nullopt;
    return v;
}

// 点分标识符列表；prerelease 的纯数字标识符不允许前导零
bool parseIdents(const QString &s, bool isPrerelease, QStringList &out)
{
    if (s.isEmpty())
        return false;
    const QStringList parts = s.split(u'.', Qt::KeepEmptyParts);
    for (const QString &p : parts) {
        if (p.isEmpty())
            return false;
        for (QChar c : p) {
            if (!isIdentChar(c))
                return false;
        }
        if (isPrerelease && isDigits(p) && p.size() > 1 && p.at(0) == u'0')
            return false;
    }
    out = parts;
    return true;
}

int compareIdent(const QString &a, const QString &b)
{
    const bool an = isDigits(a);
    const bool bn = isDigits(b);
    if (an && bn) {
        // 无前导零：先比长度再按字典序，即数值比较（任意长度）
        if (a.size() != b.size())
            return a.size() < b.size() ? -1 : 1;
        const int c = a.compare(b);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    if (an != bn)
        return an ? -1 : 1;
    const int c = a.compare(b); // ASCII 字典序
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

int cmpNum(quint64 a, quint64 b)
{
    return a < b ? -1 : (a > b ? 1 : 0);
}

QString stripV(const QString &s)
{
    if (!s.isEmpty() && (s.at(0) == u'v' || s.at(0) == u'V'))
        return s.mid(1);
    return s;
}

// ---- 范围 ----

enum class Op { Any, Eq, Gt, Ge, Lt, Le };

struct Comp {
    Op op = Op::Any;
    SemVer v;
};

using CompSet = QList<Comp>;

// 部分版本：1、1.2、1.x、1.2.*、x；x 之后的部分一律视为 x
struct Partial {
    bool xM = true, xm = true, xp = true;
    quint64 M = 0, m = 0, p = 0;
    QStringList pre;
};

bool isX(const QString &s)
{
    return s == QLatin1String("x") || s == QLatin1String("X") || s == QLatin1String("*");
}

std::optional<Partial> parsePartial(const QString &text)
{
    QString s = stripV(text);
    if (s.isEmpty())
        return std::nullopt;

    Partial r;
    bool hasTail = false;
    const int plus = s.indexOf(u'+');
    if (plus >= 0) {
        QStringList build;
        if (!parseIdents(s.mid(plus + 1), false, build))
            return std::nullopt;
        s.truncate(plus);
        hasTail = true;
    }
    const int dash = s.indexOf(u'-');
    if (dash >= 0) {
        if (!parseIdents(s.mid(dash + 1), true, r.pre))
            return std::nullopt;
        s.truncate(dash);
        hasTail = true;
    }

    const QStringList parts = s.split(u'.', Qt::KeepEmptyParts);
    if (parts.size() > 3 || (hasTail && parts.size() != 3))
        return std::nullopt;

    quint64 nums[3] = {0, 0, 0};
    bool xs[3] = {true, true, true};
    for (int i = 0; i < parts.size(); ++i) {
        if (isX(parts.at(i))) {
            xs[i] = true;
            continue;
        }
        const auto n = parseNum(parts.at(i));
        if (!n)
            return std::nullopt;
        nums[i] = *n;
        xs[i] = false;
    }
    r.xM = xs[0];
    r.xm = r.xM || xs[1];
    r.xp = r.xm || xs[2];
    r.M = r.xM ? 0 : nums[0];
    r.m = r.xm ? 0 : nums[1];
    r.p = r.xp ? 0 : nums[2];
    if (r.xp)
        r.pre.clear(); // 带通配时忽略预发布（与 node-semver 一致）
    return r;
}

SemVer make(quint64 M, quint64 m, quint64 p, const QStringList &pre = {})
{
    SemVer v;
    v.major = M;
    v.minor = m;
    v.patch = p;
    v.prerelease = pre;
    return v;
}

const QStringList kZero{QStringLiteral("0")}; // 上界的 "-0"

void addNothing(CompSet &out)
{
    out.append(Comp{Op::Lt, make(0, 0, 0, kZero)}); // <0.0.0-0：不匹配任何版本
}

void addRange(CompSet &out, const SemVer &lo, const SemVer &hiExclusive)
{
    out.append(Comp{Op::Ge, lo});
    out.append(Comp{Op::Lt, hiExclusive});
}

bool expandTilde(const QString &rest, CompSet &out)
{
    const auto pv = parsePartial(rest);
    if (!pv)
        return false;
    const Partial &x = *pv;
    if (x.xM)
        return true; // 任意
    if (x.xm)
        addRange(out, make(x.M, 0, 0), make(x.M + 1, 0, 0, kZero));
    else
        addRange(out, make(x.M, x.m, x.p, x.pre), make(x.M, x.m + 1, 0, kZero));
    return true;
}

bool expandCaret(const QString &rest, CompSet &out)
{
    const auto pv = parsePartial(rest);
    if (!pv)
        return false;
    const Partial &x = *pv;
    if (x.xM)
        return true;
    const SemVer lo = make(x.M, x.m, x.p, x.pre);
    SemVer hi;
    if (x.xm)
        hi = make(x.M + 1, 0, 0, kZero);
    else if (x.xp)
        hi = x.M == 0 ? make(0, x.m + 1, 0, kZero) : make(x.M + 1, 0, 0, kZero);
    else if (x.M > 0)
        hi = make(x.M + 1, 0, 0, kZero);
    else if (x.m > 0)
        hi = make(0, x.m + 1, 0, kZero);
    else
        hi = make(0, 0, x.p + 1, kZero);
    addRange(out, lo, hi);
    return true;
}

// op 文本：""、"="、">"、">="、"<"、"<="
bool expandXRange(QString op, const QString &rest, CompSet &out)
{
    const auto pv = parsePartial(rest);
    if (!pv)
        return false;
    Partial x = *pv;
    const bool anyX = x.xp;
    if (op == QLatin1String("=") && anyX)
        op.clear();

    if (x.xM) {
        if (op == QLatin1String(">") || op == QLatin1String("<"))
            addNothing(out);
        return true; // 其余为任意
    }
    if (!op.isEmpty() && anyX) {
        QStringList pre;
        if (op == QLatin1String(">")) {
            op = QStringLiteral(">=");
            if (x.xm) {
                x.M += 1;
                x.m = 0;
            } else {
                x.m += 1;
            }
            x.p = 0;
        } else if (op == QLatin1String("<=")) {
            op = QStringLiteral("<");
            if (x.xm) {
                x.M += 1;
                x.m = 0;
            } else {
                x.m += 1;
            }
            x.p = 0;
        }
        if (op == QLatin1String("<"))
            pre = kZero;
        const SemVer v = make(x.M, x.m, x.p, pre);
        Op o = Op::Eq;
        if (op == QLatin1String(">="))
            o = Op::Ge;
        else if (op == QLatin1String("<"))
            o = Op::Lt;
        out.append(Comp{o, v});
        return true;
    }
    if (x.xm) {
        addRange(out, make(x.M, 0, 0), make(x.M + 1, 0, 0, kZero));
        return true;
    }
    if (x.xp) {
        addRange(out, make(x.M, x.m, 0), make(x.M, x.m + 1, 0, kZero));
        return true;
    }

    // 完整版本
    Op o = Op::Eq;
    if (op == QLatin1String(">"))
        o = Op::Gt;
    else if (op == QLatin1String(">="))
        o = Op::Ge;
    else if (op == QLatin1String("<"))
        o = Op::Lt;
    else if (op == QLatin1String("<="))
        o = Op::Le;
    out.append(Comp{o, make(x.M, x.m, x.p, x.pre)});
    return true;
}

bool expandToken(const QString &tok, CompSet &out)
{
    auto stripEq = [](QString s) {
        while (s.startsWith(u'='))
            s.remove(0, 1);
        return s;
    };
    if (tok.startsWith(QLatin1String("~>")))
        return expandTilde(stripEq(tok.mid(2)), out);
    if (tok.startsWith(u'~'))
        return expandTilde(stripEq(tok.mid(1)), out);
    if (tok.startsWith(u'^'))
        return expandCaret(stripEq(tok.mid(1)), out);

    static const char *const ops[] = {">=", "<=", ">", "<", "="};
    for (const char *op : ops) {
        const QLatin1String l(op);
        if (tok.startsWith(l))
            return expandXRange(QString(l), stripEq(tok.mid(l.size())), out);
    }
    return expandXRange(QString(), tok, out);
}

bool isOperatorOnly(const QString &t)
{
    static const char *const ops[] = {"~", "~>", "^", ">", ">=", "<", "<=", "="};
    for (const char *op : ops) {
        if (t == QLatin1String(op))
            return true;
    }
    return false;
}

bool expandHyphen(const QString &fromText, const QString &toText, CompSet &out)
{
    const auto f = parsePartial(fromText);
    const auto t = parsePartial(toText);
    if (!f || !t)
        return false;
    if (!f->xM)
        out.append(Comp{Op::Ge, make(f->M, f->m, f->p, f->pre)});
    if (!t->xM) {
        if (t->xm)
            out.append(Comp{Op::Lt, make(t->M + 1, 0, 0, kZero)});
        else if (t->xp)
            out.append(Comp{Op::Lt, make(t->M, t->m + 1, 0, kZero)});
        else
            out.append(Comp{Op::Le, make(t->M, t->m, t->p, t->pre)});
    }
    return true;
}

bool parseSet(const QString &text, CompSet &out)
{
    const QStringList raw = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (raw.isEmpty())
        return true; // 空串 = 任意

    if (raw.size() == 3 && raw.at(1) == QLatin1String("-"))
        return expandHyphen(raw.at(0), raw.at(2), out);

    // 合并 "操作符 版本" 之间的空白
    QStringList tokens;
    for (int i = 0; i < raw.size(); ++i) {
        const QString &t = raw.at(i);
        if (isOperatorOnly(t)) {
            if (i + 1 >= raw.size())
                return false;
            tokens.append(t + raw.at(i + 1));
            ++i;
        } else {
            tokens.append(t);
        }
    }
    for (const QString &t : tokens) {
        if (!expandToken(t, out))
            return false;
    }
    return true;
}

bool testComp(const Comp &c, const SemVer &v)
{
    if (c.op == Op::Any)
        return true;
    const int r = SemVer::compare(v, c.v);
    switch (c.op) {
    case Op::Eq: return r == 0;
    case Op::Gt: return r > 0;
    case Op::Ge: return r >= 0;
    case Op::Lt: return r < 0;
    case Op::Le: return r <= 0;
    case Op::Any: break;
    }
    return true;
}

bool testSet(const CompSet &set, const SemVer &v)
{
    for (const Comp &c : set) {
        if (!testComp(c, v))
            return false;
    }
    if (v.prerelease.isEmpty())
        return true;
    // 预发布版本：需存在同主次补丁号且带预发布标签的比较器
    for (const Comp &c : set) {
        if (c.op == Op::Any || c.v.prerelease.isEmpty())
            continue;
        if (c.v.major == v.major && c.v.minor == v.minor && c.v.patch == v.patch)
            return true;
    }
    return false;
}

} // namespace

std::optional<SemVer> SemVer::parse(const QString &text)
{
    QString s = stripV(text);
    SemVer v;

    const int plus = s.indexOf(u'+');
    if (plus >= 0) {
        if (!parseIdents(s.mid(plus + 1), false, v.build))
            return std::nullopt;
        s.truncate(plus);
    }
    const int dash = s.indexOf(u'-');
    if (dash >= 0) {
        if (!parseIdents(s.mid(dash + 1), true, v.prerelease))
            return std::nullopt;
        s.truncate(dash);
    }

    const QStringList parts = s.split(u'.', Qt::KeepEmptyParts);
    if (parts.size() != 3)
        return std::nullopt;
    const auto M = parseNum(parts.at(0));
    const auto m = parseNum(parts.at(1));
    const auto p = parseNum(parts.at(2));
    if (!M || !m || !p)
        return std::nullopt;
    v.major = *M;
    v.minor = *m;
    v.patch = *p;
    return v;
}

int SemVer::compare(const SemVer &a, const SemVer &b)
{
    if (int c = cmpNum(a.major, b.major))
        return c;
    if (int c = cmpNum(a.minor, b.minor))
        return c;
    if (int c = cmpNum(a.patch, b.patch))
        return c;

    const bool ap = !a.prerelease.isEmpty();
    const bool bp = !b.prerelease.isEmpty();
    if (ap != bp)
        return ap ? -1 : 1; // 预发布低于正式版
    const qsizetype n = std::min(a.prerelease.size(), b.prerelease.size());
    for (qsizetype i = 0; i < n; ++i) {
        if (int c = compareIdent(a.prerelease.at(i), b.prerelease.at(i)))
            return c;
    }
    return cmpNum(quint64(a.prerelease.size()), quint64(b.prerelease.size()));
}

bool SemVer::satisfies(const SemVer &version, const QString &range)
{
    const QStringList alternatives = range.split(QStringLiteral("||"), Qt::KeepEmptyParts);
    QList<CompSet> sets;
    for (const QString &alt : alternatives) {
        CompSet set;
        if (!parseSet(alt.simplified(), set))
            return false; // 任一部分非法：整个范围非法
        sets.append(set);
    }
    for (const CompSet &set : sets) {
        if (testSet(set, version))
            return true;
    }
    return false;
}

QString SemVer::toString() const
{
    QString s = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    if (!prerelease.isEmpty()) {
        s += QLatin1Char('-');
        s += prerelease.join(QLatin1Char('.'));
    }
    if (!build.isEmpty()) {
        s += QLatin1Char('+');
        s += build.join(QLatin1Char('.'));
    }
    return s;
}
