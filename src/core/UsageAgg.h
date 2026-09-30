#pragma once

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QTimeZone>

#include <optional>

#include "SettingsCodec.h" // ModelPrice

// Token 用量汇总与费用估算的纯逻辑（需求 22.1–22.4、22.9）。只依赖 Qt6::Core。
// 数据来源（usage-scan.mjs 的输出或逐条事件）先转成 Record，再交给 aggregate。
namespace UsageAgg {

// 模型名缺失时的归类键（"未知模型"）
QString unknownModel();

constexpr double kMaxPrice = 10000.0; // 单价上限（每百万 token，含）
constexpr int kPriceDecimals = 4;     // 单价与费用的小数位数

// ---- 输入 ------------------------------------------------------------------

struct Record {
    // 归属日期：day 有效时直接使用（脚本已按本地日聚合）；否则由 timeMs 按 tz 换算本地日期
    QDate day;
    qint64 timeMs = 0;              // 事件时间戳（Unix 毫秒）
    QString berth;                  // 泊位 id
    QString model;                  // 为空（或全空白）→ 归入 unknownModel()
    std::optional<qint64> input;    // 缺失 → 按 0 计；负数按 0 计
    std::optional<qint64> output;
    std::optional<qint64> cache;
};

enum class Range { Today, Last7Days, Last30Days, All };

// ---- 输出 ------------------------------------------------------------------

struct Tokens {
    qint64 input = 0;
    qint64 output = 0;
    qint64 cache = 0;

    Tokens &operator+=(const Tokens &o) {
        input += o.input;
        output += o.output;
        cache += o.cache;
        return *this;
    }
    bool operator==(const Tokens &) const = default;
    bool isZero() const { return input == 0 && output == 0 && cache == 0; }
};

struct Totals {
    Tokens total;
    QMap<QDate, Tokens> byDay;     // 本地自然日
    QMap<QString, Tokens> byBerth; // 泊位 id
    QMap<QString, Tokens> byModel; // 模型名（含 unknownModel()）
    int recordCount = 0;           // 落入区间的记录数

    bool operator==(const Totals &) const = default;
};

// 记录的本地日期：day 有效则用 day，否则 timeMs 在 tz 下的日期
QDate localDay(const Record &r, const QTimeZone &tz);

// 区间 [first, last]（含）。today = now 在 tz 下的日期：
// Today → [today, today]；Last7Days → [today-6, today]；Last30Days → [today-29, today]；
// All → 返回 false（不设界）。
bool bounds(Range range, const QDateTime &now, const QTimeZone &tz, QDate *first, QDate *last);

// 汇总落入区间的记录；每条记录只按其本地日期归入一次，
// 因此 byDay / byBerth / byModel 各自三项之和都等于 total（不变量）。
Totals aggregate(const QList<Record> &records, Range range, const QDateTime &now,
                 const QTimeZone &tz = QTimeZone::systemTimeZone());

// ---- 费用 ------------------------------------------------------------------

// 保留 4 位小数（四舍五入）
double round4(double v);

// token ÷ 1,000,000 × 单价，三项相加后保留 4 位小数
double cost(const Tokens &tokens, const ModelPrice &price);

struct CostSummary {
    QMap<QString, double> byModel; // 仅含已配置单价的模型
    QStringList unpriced;          // 未配置单价的模型（费用显示"—"，不计入合计）
    double total = 0;              // 已计价模型费用之和（保留 4 位小数）
    bool hasUnpriced() const { return !unpriced.isEmpty(); }
};

// 对 totals.byModel 逐模型计价；prices 中找不到的模型归入 unpriced
CostSummary costs(const Totals &totals, const QMap<QString, ModelPrice> &prices);

// 单个模型的费用；未配置单价时返回 std::nullopt
std::optional<double> modelCost(const Totals &totals, const QString &model,
                                const QMap<QString, ModelPrice> &prices);

// ---- 单价校验（需求 22.9）----------------------------------------------------

// 文本形式：去掉首尾空白后须为非负十进制数（可无小数部分，最多 4 位小数），值在 [0, 10000]。
// 合法返回 std::nullopt，否则返回错误说明。
std::optional<QString> validatePrice(QStringView text);

// 数值形式：有限、在 [0, 10000]、且最多 4 位小数（按 1e-9 容差判定）
bool isValidPrice(double v);

// 三项单价都合法
bool isValidPrice(const ModelPrice &p);

} // namespace UsageAgg
