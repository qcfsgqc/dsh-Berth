#pragma once

#include <QString>

// 复刻 dsh 的 projectKey(cwd)（deepseek-harness/packages/session/
// session-persistence-jsonl/src/format.ts）。只依赖 Qt6::Core。
// 规则：'/'、'\\'、':' 连续出现时合并为一个 '-'；[A-Za-z0-9._-] 原样保留；
// 其余 UTF-16 码元（含 '~'）写成 "~XXXX"（大写十六进制，补足 4 位）；
// 去掉开头的 '-'，为空时取 "root"；截取前 251 个字符后两端加 "--"。
namespace ProjectKey {

// cwd 为空时返回空字符串（dsh 在此情况下抛异常）
QString projectKey(const QString &cwd);

} // namespace ProjectKey
