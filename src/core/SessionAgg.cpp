#include "core/SessionAgg.h"

namespace SessionAgg {

Result aggregate(const QList<SessionDir> &dirs, const QDateTime &now)
{
    Result r;
    r.total = int(dirs.size());
    for (const SessionDir &d : dirs) {
        if (!d.mtime.isValid())
            continue;
        if (!r.lastActivity.isValid() || d.mtime > r.lastActivity)
            r.lastActivity = d.mtime;
        if (now.isValid() && d.mtime.msecsTo(now) <= kActiveWindowMs)
            ++r.active;
    }
    return r;
}

} // namespace SessionAgg
