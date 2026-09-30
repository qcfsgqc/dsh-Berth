#pragma once

// berth_core 静态库的占位入口：保证 src/core/ 在尚无其它源文件时 GLOB 不为空，
// 否则 STATIC 库没有源文件会导致 CMake 报错。后续 core 模块加入后可以保留它。
// 约束：src/core/ 下只依赖 Qt6::Core，不 include Gui/Qml/Widgets/windows.h。
namespace BerthCore {

// 返回库标识字符串，仅用于占位与链接自检
const char *libraryName();

} // namespace BerthCore
