#pragma once

#include <QString>
#include <QStringList>

// 诊断报告与日志脱敏。只依赖 Qt6::Core。
namespace Redact {

// 把 text 中每个敏感值的所有出现位置替换为掩码（默认 "***"）。
// - 空敏感值会被跳过
// - 先在原文上标记所有命中区间（较长的值优先，重叠区间合并），再统一替换，
//   避免短值是长值子串时残留，也避免替换结果与相邻文本拼出新的敏感值
// - 若某个敏感值本身含 '*'，改用不与任何敏感值冲突的掩码字符
QString redact(const QString &text, const QStringList &secrets);

// 脱敏文本中所有 URL 的 userinfo（scheme://user:pass@host → scheme://***:***@host；
// 只有用户名时为 scheme://***@host）。可传单个代理 URL，也可传含多个 URL 的整段文本。
// userinfo 取 "://" 之后、不含空白与 '/'、'?'、'#' 的片段中最后一个 '@' 之前的部分，
// 以覆盖未编码的 '@'。
QString redactProxyUrl(const QString &text);

} // namespace Redact
