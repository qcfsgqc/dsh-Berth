#include "core/TrayRules.h"

#include "core/BatchPlan.h"

namespace TrayRules {

MenuFlags flagsFor(const QString &status)
{
    const bool running = status == QLatin1String("running");
    const bool external = status == QLatin1String("external");
    const bool starting = status == QLatin1String("starting");

    MenuFlags f;
    f.start = !BatchPlan::isActiveState(status) && !external;
    f.stop = starting || running || external;
    f.restart = running;
    f.openUi = running || external;
    f.openBrowser = running || external;
    return f;
}

} // namespace TrayRules
