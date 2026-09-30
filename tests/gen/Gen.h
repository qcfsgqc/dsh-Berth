#pragma once

#include <QByteArray>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTest>

#include <concepts>
#include <iterator>
#include <type_traits>
#include <utility>

// 轻量性质测试工具（不依赖第三方 PBT 库）。
//
// 生成器：可调用对象 T gen(QRandomGenerator &rng)
// 性质：  可调用对象 bool prop(const T &value)，返回 false 表示反例
//
//   Gen::forAll<int>(100,
//       [](QRandomGenerator &r) { return Gen::intIn(r, 0, 1000); },
//       [](const int &n) { return n >= 0; });
//
// 种子默认 20260101，可用环境变量 BERTH_PBT_SEED 覆盖（十进制或 0x 十六进制）。
// 每次 forAll 都从该种子重新开始，所以同一种子下结果可复现。
// 失败时 QFAIL 输出种子、迭代序号（从 0 开始）与反例 JSON。
// 注意：QFAIL 只从 forAll 内部返回，调用方若在 forAll 之后还有断言，
// 可先检查 QTest::currentTestFailed()。
namespace Gen {

constexpr quint32 kDefaultSeed = 20260101u;

inline quint32 seed()
{
    const QString env = qEnvironmentVariable("BERTH_PBT_SEED").trimmed();
    if (!env.isEmpty()) {
        bool ok = false;
        const uint value = env.toUInt(&ok, 0);
        if (ok)
            return value;
        qWarning("BERTH_PBT_SEED=\"%s\" is not a valid uint32, using default %u",
                 qUtf8Printable(env), kDefaultSeed);
    }
    return kDefaultSeed;
}

// ---- 反例序列化 -------------------------------------------------------------
// 优先级：QString/QJson* 原样 → 成员 toJson() → 可经 ADL 找到的 toJson(v)
// → QByteArray → 数值/布尔 → 可迭代容器 → QDebug 文本 → 占位字符串。
// toJson 的返回值须能隐式转换为 QJsonValue（如 QJsonObject）。
// 自定义类型只需提供成员或同命名空间的 toJson 即可获得可读的反例。

template <typename T>
QJsonValue toJsonValue(const T &value);

namespace detail {

template <typename T>
concept JsonNative = std::is_same_v<T, QString> || std::is_same_v<T, QJsonValue>
    || std::is_same_v<T, QJsonObject> || std::is_same_v<T, QJsonArray>;

template <typename T>
concept HasMemberToJson = requires(const T &v) {
    { v.toJson() } -> std::convertible_to<QJsonValue>;
};

template <typename T>
concept HasAdlToJson = requires(const T &v) {
    { toJson(v) } -> std::convertible_to<QJsonValue>;
};

template <typename T>
concept Iterable = requires(const T &v) {
    std::begin(v);
    std::end(v);
};

template <typename T>
concept DebugStreamable = requires(QDebug d, const T &v) { d << v; };

} // namespace detail

template <typename T>
QJsonValue toJsonValue(const T &value)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (detail::JsonNative<U>) {
        return QJsonValue(value);
    } else if constexpr (detail::HasMemberToJson<U>) {
        return QJsonValue(value.toJson());
    } else if constexpr (detail::HasAdlToJson<U>) {
        return QJsonValue(toJson(value));
    } else if constexpr (std::is_same_v<U, QByteArray>) {
        return QJsonValue(QString::fromUtf8(value));
    } else if constexpr (std::is_same_v<U, bool>) {
        return QJsonValue(value);
    } else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>) {
        return QJsonValue(qint64(value));
    } else if constexpr (std::is_floating_point_v<U>) {
        return QJsonValue(double(value));
    } else if constexpr (detail::Iterable<U>) {
        QJsonArray array;
        for (const auto &element : value)
            array.append(toJsonValue(element));
        return array;
    } else if constexpr (detail::DebugStreamable<U>) {
        QString text;
        QDebug(&text).nospace() << value;
        return QJsonValue(text);
    } else {
        return QJsonValue(QStringLiteral("<unprintable>"));
    }
}

inline QByteArray toJsonText(const QJsonValue &value)
{
    // QJsonDocument 只接受对象/数组：包一层数组再去掉首尾的方括号
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2);
}

// ---- forAll -----------------------------------------------------------------

template <typename T, typename GenFn, typename Prop>
void forAll(int iterations, GenFn &&gen, Prop &&prop)
{
    const quint32 s = seed();
    QRandomGenerator rng(s);
    for (int i = 0; i < iterations; ++i) {
        const T value = gen(rng);
        if (!prop(value)) {
            const QByteArray message =
                QByteArrayLiteral("forAll failed: seed=") + QByteArray::number(s)
                + QByteArrayLiteral(" iteration=") + QByteArray::number(i)
                + QByteArrayLiteral(" counterexample=") + toJsonText(toJsonValue(value));
            QFAIL(message.constData());
        }
    }
}

// ---- 基础生成器 -------------------------------------------------------------
// 领域生成器（版本号、包名、env 表等）由后续任务在此基础上补充。

// [lo, hi] 闭区间整数
inline int intIn(QRandomGenerator &rng, int lo, int hi)
{
    Q_ASSERT(lo <= hi);
    return int(rng.bounded(qint64(lo), qint64(hi) + 1));
}

// [lo, hi] 闭区间；要求 hi < INT64_MAX
inline qint64 int64In(QRandomGenerator &rng, qint64 lo, qint64 hi)
{
    Q_ASSERT(lo <= hi);
    return rng.bounded(lo, hi + 1);
}

// 以概率 p 返回 true
inline bool chance(QRandomGenerator &rng, double p)
{
    return rng.generateDouble() < p;
}

// 从非空列表中随机取一个元素
template <typename List>
auto pick(QRandomGenerator &rng, const List &items) -> std::remove_cvref_t<decltype(items[0])>
{
    Q_ASSERT(!items.isEmpty());
    return items[qsizetype(rng.bounded(quint32(items.size())))];
}

// 长度在 [minLen, maxLen] 内、字符取自 alphabet 的字符串
inline QString stringOf(QRandomGenerator &rng, const QString &alphabet, int minLen, int maxLen)
{
    Q_ASSERT(!alphabet.isEmpty());
    const int len = intIn(rng, minLen, maxLen);
    QString out;
    out.reserve(len);
    for (int i = 0; i < len; ++i)
        out.append(alphabet.at(qsizetype(rng.bounded(quint32(alphabet.size())))));
    return out;
}

// 长度在 [minLen, maxLen] 内的列表，元素由 elem(rng) 生成
template <typename T, typename ElemFn>
QList<T> listOf(QRandomGenerator &rng, int minLen, int maxLen, ElemFn &&elem)
{
    const int len = intIn(rng, minLen, maxLen);
    QList<T> out;
    out.reserve(len);
    for (int i = 0; i < len; ++i)
        out.append(elem(rng));
    return out;
}

} // namespace Gen
