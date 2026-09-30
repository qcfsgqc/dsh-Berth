#pragma once

#include <QString>

// 托盘泊位子菜单五项的可用性规则（纯逻辑）。只依赖 Qt6::Core。
namespace TrayRules {

struct MenuFlags {
    bool start = false;       // 启动
    bool stop = false;        // 停止
    bool restart = false;     // 重启
    bool openUi = false;      // 打开界面
    bool openBrowser = false; // 在浏览器中打开

    bool operator==(const MenuFlags &o) const
    {
        return start == o.start && stop == o.stop && restart == o.restart && openUi == o.openUi
            && openBrowser == o.openBrowser;
    }
    bool operator!=(const MenuFlags &o) const { return !(*this == o); }
};

// status 取值与 Instance::status 一致（starting / running / stopping / external / 其它视为非活动）。
// 启动：不处于 Active_State 且不是 external；停止：starting / running / external；
// 重启：running；两个打开项：running / external；stopping 时五项全不可用。
MenuFlags flagsFor(const QString &status);

} // namespace TrayRules
