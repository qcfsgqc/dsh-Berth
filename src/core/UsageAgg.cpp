#include "UsageAgg.h"

#include <algorithm>
#include <cmath>

namespace UsageAgg {

namespace {

qint64 nonNeg(const std::optional<qint64> &v) {
    return (v && *v > 0) ? *v : 0;
}

QString modelKey(const QString &model) {
    const QString m = model.trimmed();
    return m.isEmpty() ? unknownModel() : m;
}

// 最多 kPriceDecimals 位小数（容差 1e-9，吸收二进制浮点误差）
bool hasAtMost4Decimals(double v) {
    const double scaled = v * 10000.0;
    return std::fabs(scaled - std::round(scaled)) <= 1e-9 * std::max(1.0, std::fabs(scaled));
}

} // namespace

QString unknownModel() {
    return QStringLiteral("未知模型");
}

QDate localDay(const Record &r, const QTimeZone &tz) {
    if (r.day.isValid())
        return r.day;
    return QDateTime::fromMSecsSinceEpoch(r.timeMs, tz).date();
}

bool bounds(Range range, const QDateTime &now, const QTimeZone &tz, QDate *first, QDate *last) {
    const QDate today = now.toTimeZone(tz).date();
    int span = 0;
    switch (range) {
    case Range::Today: span = 1; break;
    case Range::Last7Days: span = 7; break;
    case Range::Last30Days: span = 30; break;
    case Range::All: return false;
    }
    if (first)
        *first = today.addDays(-(span - 1));
    if (last)
        *last = today;
    return true;
}

Totals aggregate(const QList<Record> &records, Range range, const QDateTime &now, const QTimeZone &tz) {
    QDate first, last;
    const bool bounded = bounds(range, now, tz, &first, &last);

    Totals t;
    for (const Record &r : records) {
        const QDate day = localDay(r, tz);
        if (bounded && (!day.isValid() || day < first || day > last))
            continue;
        const Tokens tk{nonNeg(r.input), nonNeg(r.output), nonNeg(r.cache)};
        t.total += tk;
        t.byDay[day] += tk;
        t.byBerth[r.berth] += tk;
        t.byModel[modelKey(r.model)] += tk;
        ++t.recordCount;
    }
    return t;
}

double round4(double v) {
    return std::round(v * 10000.0) / 10000.0;
}

double cost(const Tokens &tokens, const ModelPrice &price) {
    const double v = double(tokens.input) / 1'000'000.0 * price.input
                   + double(tokens.output) / 1'000'000.0 * price.output
                   + double(tokens.cache) / 1'000'000.0 * price.cache;
    return round4(v);
}

CostSummary costs(const Totals &totals, const QMap<QString, ModelPrice> &prices) {
    CostSummary s;
    double sum = 0;
    for (auto it = totals.byModel.constBegin(); it != totals.byModel.constEnd(); ++it) {
        const auto p = prices.constFind(it.key());
        if (p == prices.constEnd()) {
            s.unpriced.append(it.key());
            continue;
        }
        const double c = cost(it.value(), p.value());
        s.byModel.insert(it.key(), c);
        sum += c;
    }
    // 合计 = 各模型已取整费用之和，再取整，保证与表格逐行相加一致
    s.total = round4(sum);
    return s;
}

std::optional<double> modelCost(const Totals &totals, const QString &model,
                                const QMap<QString, ModelPrice> &prices) {
    const auto p = prices.constFind(model);
    if (p == prices.constEnd())
        return std::nullopt;
    return cost(totals.byModel.value(model), p.value());
}

std::optional<QString> validatePrice(QStringView text) {
    const QString err = QStringLiteral("单价须为 0 至 10000（含）之间的数值，最多 4 位小数");
    const QStringView s = text.trimmed();
    if (s.isEmpty())
        return err;

    qsizetype i = 0;
    qsizetype intDigits = 0;
    while (i < s.size() && s.at(i).isDigit() && s.at(i).unicode() < 128) {
        ++i;
        ++intDigits;
    }
    qsizetype fracDigits = 0;
    if (i < s.size() && s.at(i) == QLatin1Char('.')) {
        ++i;
        while (i < s.size() && s.at(i).isDigit() && s.at(i).unicode() < 128) {
            ++i;
            ++fracDigits;
        }
        if (fracDigits == 0)
            return err; // "1." 这类不完整写法
    }
    if (i != s.size() || intDigits == 0 || fracDigits > kPriceDecimals)
        return err;

    bool ok = false;
    const double v = s.toString().toDouble(&ok);
    if (!ok || !std::isfinite(v) || v < 0 || v > kMaxPrice)
        return err;
    return std::nullopt;
}

bool isValidPrice(double v) {
    return std::isfinite(v) && v >= 0 && v <= kMaxPrice && hasAtMost4Decimals(v);
}

bool isValidPrice(const ModelPrice &p) {
    return isValidPrice(p.input) && isValidPrice(p.output) && isValidPrice(p.cache);
}

} // namespace UsageAgg
