#include "Backoff.h"

#include <climits>

namespace Backoff {

int backoffMs(int n, int baseSec, int maxSec)
{
    if (n < 1)
        n = 1;
    if (baseSec < 1)
        baseSec = 1;
    if (maxSec < baseSec)
        maxSec = baseSec;

    // 逐次翻倍，一旦达到上限立即截断，避免 2^(n-1) 溢出
    long long sec = baseSec;
    for (int i = 1; i < n && sec < maxSec; ++i)
        sec *= 2;
    if (sec > maxSec)
        sec = maxSec;

    const long long ms = sec * 1000LL;
    return ms > INT_MAX ? INT_MAX : static_cast<int>(ms);
}

} // namespace Backoff
