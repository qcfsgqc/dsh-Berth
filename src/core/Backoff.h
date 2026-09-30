#pragma once

// 自动重启退避间隔。只依赖 Qt6::Core（实际无外部依赖）。
namespace Backoff {

// 第 n 次连续重启前的等待毫秒数：min(2^(n-1) * baseSec, maxSec) * 1000。
// - n < 1 按 1 处理；baseSec < 1 按 1 处理；maxSec < baseSec 按 baseSec 处理
// - 指数部分在超过 maxSec 时提前截断，不会溢出
int backoffMs(int n, int baseSec, int maxSec);

} // namespace Backoff

using Backoff::backoffMs;
