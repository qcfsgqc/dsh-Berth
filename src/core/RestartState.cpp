#include "RestartState.h"

#include "Backoff.h"

namespace {
int clampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
} // namespace

RestartState::RestartState(int limit, int base, int max, bool on)
    : enabled(on)
{
    configure(limit, base, max);
}

void RestartState::configure(int limit, int base, int max)
{
    N = clampInt(limit, 1, 20);
    baseSec = clampInt(base, 1, 60);
    maxSec = clampInt(max, baseSec, 600);
    n = clampInt(n, 0, N);
}

RestartState::Action RestartState::onCrash()
{
    if (!enabled || external)
        return Action::none();
    if (n >= N) {
        n = N;
        return Action::giveUp();
    }
    ++n;
    return Action::schedule(backoffMs(n, baseSec, maxSec));
}

void RestartState::onRunningFor(int sec)
{
    if (sec >= kResetAfterSec)
        n = 0;
}

void RestartState::onManualStop()
{
    n = 0;
}

void RestartState::onManualStart()
{
    n = 0;
}

void RestartState::onToggle(bool on)
{
    enabled = on;
    if (!on)
        n = 0;
}

void RestartState::onExternal(bool ext)
{
    external = ext;
}
