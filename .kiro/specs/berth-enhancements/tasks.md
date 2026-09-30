# Implementation Plan: berth-enhancements

## Overview

按 design.md 的阶段顺序实施：阶段 0（数据兼容层与测试骨架）→ B 稳定性 → D 托盘与体验 → A 管理 → E 统计 → C 更新与环境。纯逻辑放在 `src/core/`（berth_core，只链接 Qt6::Core），主程序代码放在 `src/` 与 `qml/`，测试放在 `tests/`（berth_tests）。AI 只写代码、改 CMake、写测试，不编译不运行。每个阶段末尾的 Checkpoint 由用户编译运行，并按各子任务里的手动验证步骤确认。

## Tasks

### 阶段 0：数据兼容层与测试骨架

- [x] 1. 构建骨架与测试框架
  - [x] 1.1 拆分 berth_core 并建立 berth_tests
    - `CMakeLists.txt`：新增 `option(BERTH_BUILD_TESTS ON)`；`berth_core` STATIC 库用 `file(GLOB CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)` 收集源文件，只链接 Qt6::Core；`dsh-berth` 链接 berth_core，再显式链接 `iphlpapi`、`crypt32`
    - `berth_tests` 用 `file(GLOB CONFIGURE_DEPENDS tests/tst_*.cpp)` 收集源文件，链接 berth_core 与 Qt6::Test（不链接 Widgets/Qml），并用 `add_test` 注册
    - `tests/main.cpp`：测试类自注册（`BERTH_TEST(Class)` 宏写入静态注册表），main 依次执行，这样后续测试文件不需要改 main
    - `tests/gen/Gen.h`：基于 `QRandomGenerator` 的 `forAll<T>(100, gen, prop)`，种子默认 `20260101`，可用 `BERTH_PBT_SEED` 覆盖；失败时 `QFAIL` 输出种子、迭代序号与反例 JSON
    - 手动验证：前置为已安装 Qt 6.10.2 → 用 CMake 配置并构建 → berth_tests 运行 0 个用例、返回 0，dsh-berth 行为与改动前一致
    - _Requirements: 27.4, 27.5, 27.6_

- [x] 2. 数据文件兼容层
  - [x] 2.1 实现 `core/DataFile`
    - `load(QByteArray, supported)` 返回 `{Normal/ReadOnly/Corrupt, version, root}`；缺少 schemaVersion 按 0 处理；schema 0 的顶层数组包装成对象
    - `backupName(file, oldVer)`；写回计划：低版本时第一步是备份，同名备份已存在则跳过；合并未知顶层字段；类型错误的字段记为"文件名.字段名"
    - 手动验证：由 2.2/2.3 的测试覆盖 → 运行 berth_tests → DataFile 相关用例全部通过
    - _Requirements: 25.2, 25.4, 25.5, 25.6, 25.8_
  - [x]* 2.2 编写性质测试 `tests/tst_datafile.cpp`
    - **Property 18: Schema 决策**
    - **Validates: Requirements 25.2, 25.4, 25.5, 25.6, 25.8**
  - [x]* 2.3 编写单元测试 `tests/tst_datafile_edge.cpp`
    - schema 0 顶层数组迁移；`.bak-0` 已存在时不覆盖；非法 JSON 不产生写入计划
    - _Requirements: 25.6, 25.8_
  - [x] 2.4 实现 `core/InstanceCodec` 与 `core/SettingsCodec`，扩展 `Instance`
    - `src/Instance.h` 新增 env、extraArgs、dshVersion、autoRestart、icon、group、tags、notes 与 `QJsonObject extra`
    - `fromJson(obj, typeErrors)` / `toJson`；未知字段原样写回；缺少的字段取默认值；Settings 按 design 新增字段与默认值
    - 手动验证：由 2.5 覆盖 → 运行 berth_tests → 往返用例通过
    - _Requirements: 5.9, 9.8, 25.1, 25.3, 25.9_
  - [x]* 2.5 编写性质测试 `tests/tst_codec.cpp`
    - **Property 17: 泊位与设置数据往返（含未知字段）**
    - **Validates: Requirements 5.9, 5.10, 9.8, 25.1, 25.3, 25.9**
  - [x] 2.6 实现 `DataStore`，接入 AppController 与 Settings
    - `src/DataStore.{h,cpp}`：用 `QSaveFile` 写入；备份失败时放弃写回；ReadOnly 或 Corrupt 时拒绝保存；发出 `loadWarnings`
    - `AppController::load/save` 与 `Settings` 的读写改走 DataStore；启动完成后用 notice 提示类型错误、只读、损坏；只读时 QML 禁用保存按钮
    - 手动验证：前置为备份现有 `%APPDATA%\dsh-Berth\instances.json`（旧数组格式）→ 启动 Berth 后改一个泊位名并保存 → 出现 `instances.json.bak-0`，新文件带 `"schemaVersion": 1`，原泊位都在；再把 schemaVersion 改成 99 后启动 → 数据照常显示，保存被禁止并提示升级
    - _Requirements: 25.1, 25.2, 25.3, 25.4, 25.5, 25.6, 25.7, 25.8_
  - [x] 2.7 实现 `core/Redact`
    - `redact(text, secrets)`：替换为 `***`，跳过空值，优先替换较长的值；`redactProxyUrl` 放到 30.1
    - 手动验证：由 2.8 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 5.11, 15.6_
  - [x]* 2.8 编写性质测试 `tests/tst_redact.cpp`
    - **Property 19: 脱敏**
    - **Validates: Requirements 5.11, 15.6, 16.7**
  - [x] 2.9 实现 `ProcessRunner`
    - `src/ProcessRunner.{h,cpp}`：`CREATE_NO_WINDOW`；每次运行一个 Job Object（`KILL_ON_JOB_CLOSE`）；超时后结束整个 Job 并标记 timedOut；只保留最后 N 行输出（默认 20）；环境注入钩子（先传空表）
    - 析构时关闭全部 Job 句柄；把 PluginOps、ProfileOps 现有的 QProcess 调用改为经过 ProcessRunner
    - 手动验证：前置为已有一个 profile → 在插件对话框卸载一个插件 → 不出现控制台窗口，结果提示与改动前一致；卸载过程中退出 Berth → 5 秒内任务管理器里没有残留的 pnpm/node 进程
    - _Requirements: 26.1, 26.7, 26.8_

- [x] 3. Checkpoint：阶段 0
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

### 阶段 B：稳定性（需求 11–13）

- [x] 4. 启动流水线与 Supervisor 改造
  - [x] 4.1 实现 `core/LaunchEnv`
    - `build(sys, envTable, dshHome, proxyEnv, inheritProxy)`：键不区分大小写；优先级为系统环境 < 代理环境 < 泊位环境表 < DSH_HOME
    - `filterArgs(extra, conflicts)`：移除 `--port`/`--profile` 及其 `=` 形式；单独成项时连同紧随的一项一起移除
    - 手动验证：由 4.2/4.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 5.2, 5.3, 5.4, 5.8, 16.8_
  - [x]* 4.2 编写性质测试 `tests/tst_launchenv_env.cpp`
    - **Property 15: 启动环境组装**
    - **Validates: Requirements 5.2, 5.4, 16.3, 16.8**
  - [x]* 4.3 编写性质测试 `tests/tst_launchenv_args.cpp`
    - **Property 16: 启动参数过滤**
    - **Validates: Requirements 5.3, 5.8**
  - [x] 4.4 改造 `Supervisor`
    - 用 `start(const LaunchSpec&)`（exe、args、env、cwd、port）取代 `start(Instance, exe)`；每个进程记录"请求停止"标志，未请求的退出一律发出 `crashed(id, exitCode)`；`finished` 不再按 exit 0 判定为已停止
    - 新增 `attachExternal(id, pid, port)`、`jobHandle(id)`、`forceKill(id)`；新增状态"运行中（外部）""崩溃已停止"，InstanceModel 与 `statusText` 同步显示
    - 手动验证：前置为有一个可启动的泊位 → 启动后在任务管理器里结束 dsh 的 node 进程 → 泊位显示"失败"类状态，日志有 crash 记录；手动停止 → 显示"已停止"，不记为 crash
    - _Requirements: 11.8, 12.4, 23.3_
  - [x] 4.5 在 AppController 中实现统一启动流水线
    - `launch(id, reason)`：依次为端口检查、版本可执行文件解析（先只用 System_Dsh）、Preflight（占位直通）、LaunchEnv 组装（env 表先为空）、`Supervisor::start`；任一步拒绝都返回带原因的 `LaunchRejected`
    - 手动、重启、批量、自动重启都走这条流水线；参数顺序为 `--profile <p> --port <n> --no-open` + 过滤后的 extraArgs
    - 手动验证：前置为已有泊位 → 启动、重启、停止各一次 → 行为与改动前一致，日志中的命令行参数顺序正确
    - _Requirements: 3.1, 5.2, 5.3, 6.4, 12.1_

- [x] 5. 崩溃自动重启
  - [x] 5.1 实现 `core/Backoff` 与 `core/RestartState`
    - `backoffMs(n, base, max)`；状态机方法 `onCrash/onRunningFor/onManualStop/onManualStart/onToggle/onExternal`，返回 `None/Schedule(ms)/GiveUp`
    - 手动验证：由 5.2/5.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.7, 11.9, 11.10_
  - [x]* 5.2 编写性质测试 `tests/tst_backoff.cpp`
    - **Property 1: 退避间隔公式与单调性**
    - **Validates: Requirements 11.2, 11.7**
  - [x]* 5.3 编写性质测试 `tests/tst_restartstate.cpp`
    - **Property 2: 自动重启状态机**
    - **Validates: Requirements 11.1, 11.3, 11.4, 11.5, 11.8, 11.9, 11.10**
  - [x] 5.4 实现 `AutoRestarter` 并接入界面
    - `src/AutoRestarter.{h,cpp}`：每个泊位持有一个 RestartState；用 QTimer 执行退避和 120 秒清零；每秒发出 `countdown(id, sec, n, N)`；自动重启在进入运行中前退出也算 crash；达到上限后置为"崩溃已停止"，先用主窗口 notice 加日志提醒（11.1 接入 Notifier）
    - 泊位编辑界面加"崩溃自动重启"开关；设置页加 baseSec/maxSec/maxAttempts（按范围校验）；泊位卡片显示"将在 X 秒后重启（第 n/N 次）"
    - 手动验证：前置为泊位开启自动重启，上限设为 2 → 连续两次在任务管理器结束 dsh 进程 → 卡片依次倒计时 2 秒、4 秒并自动重启；第三次结束后显示"崩溃已停止"并出现 notice；倒计时期间点停止 → 倒计时 1 秒内消失
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.6, 11.8, 11.9, 11.10_

- [x] 6. 端口冲突检查与复用
  - [x] 6.1 实现 `core/PortPick`
    - `next(from, isListening, configured)`；`duplicates(instances, id, port)` 返回使用同一端口的泊位名
    - 手动验证：由 6.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 12.7, 12.8, 12.9_
  - [x]* 6.2 编写性质测试 `tests/tst_portpick.cpp`
    - **Property 20: 端口选择与重复校验**
    - **Validates: Requirements 12.7, 12.8, 12.9**
  - [x] 6.3 实现 `PortChecker`
    - `src/PortChecker.{h,cpp}`：用 `GetExtendedTcpTable`（IPv4/IPv6）查监听状态与 PID，用 `QueryFullProcessImageNameW` 取路径，取不到的字段显示"未知"
    - `probeDsh(port)`：3 秒内 `GET /manifest.webmanifest` 并按特征判定；`watchExternal`：每秒检查 PID 存活与端口监听，失效时置为"已停止"，不记为 crash
    - 手动验证：前置为用另一个程序占用泊位端口 → 启动泊位 → 1 秒内弹出冲突框，显示 PID、进程名、路径
    - _Requirements: 12.1, 12.2, 12.3, 12.10_
  - [x] 6.4 冲突对话框与保存时的端口重复校验
    - 新增 `qml/PortConflictDialog.qml`：取消启动、改用空闲端口（保存后用新端口启动，找不到时报错）、复用现有服务（仅探测命中时显示；调用 `attachExternal`，停止时只解除关联并提示）
    - `updateInstance` 发现端口重复时拒绝保存，列出泊位名，保留表单内容
    - 手动验证：前置为在命令行手动运行一个 `dsh web --port <泊位端口>` → 启动泊位 → 冲突框出现"复用现有服务"；选中后状态为"运行中（外部）"，可以打开界面；点停止后外部进程仍在；关闭外部 dsh → 5 秒内泊位变为"已停止"
    - _Requirements: 12.2, 12.3, 12.4, 12.5, 12.6, 12.7, 12.8, 12.9, 12.10_

- [x] 7. 批量启停与错峰自启
  - [x] 7.1 实现 `core/BatchPlan`
    - `plan(targets, statuses, intervalSec)`：跳过 Active_State 的泊位（计入已完成），其余按第 k·t 秒编排；`cancel(at)` 列出未发起的泊位
    - 手动验证：由 7.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 13.3, 13.4, 13.8_
  - [x]* 7.2 编写性质测试 `tests/tst_batchplan.cpp`
    - **Property 21: 批量错峰计划**
    - **Validates: Requirements 13.3, 13.4, 13.8**
  - [x] 7.3 实现 `BatchLauncher` 并接入界面
    - `src/BatchLauncher.{h,cpp}`：启动全部、停止全部、启动所选、停止所选；进度、取消、汇总（成功数、失败数、失败原因、未处理的泊位名）；批量停止并发执行；目标为空时禁用入口
    - 主窗口初始化后按 `staggerSec` 依次启动 autostart 泊位；设置页加错峰间隔（0–60）；"启动分组/停止分组"入口先隐藏（22.4 开启）
    - 手动验证：前置为 3 个泊位都勾选 autostart，间隔 3 秒 → 启动 Berth → 三个泊位约每 3 秒启动一个，进度显示 x/3；批量进行中点取消 → 汇总列出未处理的泊位
    - _Requirements: 13.1, 13.2, 13.3, 13.4, 13.5, 13.6, 13.7, 13.8, 13.9_

- [ ] 8. Checkpoint：阶段 B
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

### 阶段 D：托盘与体验（需求 17–20）

- [x] 9. 托盘泊位菜单与独立托盘图标
  - [x] 9.1 实现 `core/TrayRules`
    - `flagsFor(status)` 返回启动、停止、重启、打开界面、浏览器打开五项的可用性
    - 手动验证：由 9.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 17.2_
  - [x]* 9.2 编写性质测试 `tests/tst_trayrules.cpp`
    - **Property 22: 托盘菜单可用性**
    - **Validates: Requirements 17.2**
  - [x] 9.3 实现 `TrayController`
    - `src/TrayController.{h,cpp}`：从 main.cpp 移出托盘逻辑；每个泊位一个子菜单，标题为"泊位名 — 状态"；监听 `InstanceModel::dataChanged` 刷新（菜单打开时也刷新）；提供启动全部/停止全部（批量进行中禁用）；没有泊位时显示"暂无泊位"占位项
    - 菜单动作调用与主窗口相同的 AppController 方法（含端口冲突处理）
    - 手动验证：前置为有 2 个泊位 → 打开托盘菜单 → 每个泊位都有子菜单，可用项符合状态；从托盘启动其中一个 → 1 秒内标题变为"启动中"，随后变为"运行中"
    - _Requirements: 17.1, 17.2, 17.3, 17.4, 17.5, 17.6_
  - [x] 9.4 泊位独立托盘图标
    - 设置项 `trayInstanceIcons`（默认关闭）；每个"运行中"泊位一个 `QSystemTrayIcon`（先用默认图标，22.4 换成自定义图标），悬停提示为泊位名加端口；左键打开或前置该泊位界面；离开运行中、关闭设置、退出时都先 `hide()` 再 delete
    - 手动验证：前置为开启该设置 → 启动一个泊位 → 通知区域出现第二个图标，单击会打开或前置界面；停止泊位 → 图标 1 秒内消失；退出 Berth → 通知区域不残留图标
    - _Requirements: 17.7, 17.8, 17.9, 17.10, 17.11_

- [x] 10. 开机自启与关闭到托盘
  - [x] 10.1 实现 `core/RunValue` 与 `AutostartRegistry`，解析 `--autostart`
    - `core/RunValue`：生成和解析 `"<exe>" --autostart`，比较路径时不区分大小写
    - `src/AutostartRegistry.{h,cpp}`：通过 QSettings 读写 HKCU Run 下的 `DSH Berth`；设置页开关按注册表实际状态显示，失败时开关退回并提示原因
    - main.cpp：带 `--autostart` 且 `startMinimized` 为真时只显示托盘，否则显示主窗口
    - 手动验证：前置为 Berth 没有管理员权限 → 打开开关 → `regedit` 中出现对应的值；注销后重新登录 → Berth 按 startMinimized 设置启动；关闭开关 → 值被删除，其他启动项不受影响
    - _Requirements: 18.1, 18.2, 18.3, 18.4, 18.10_
  - [x]* 10.2 编写单元测试 `tests/tst_runvalue.cpp`
    - 带引号和不带引号的路径、缺少 `--autostart`、路径大小写不同
    - _Requirements: 18.10_
  - [x] 10.3 关闭到托盘与退出确认
    - 设置项 `closeToTray`（默认开启）；开启时关闭窗口只隐藏；关闭时，有 Active_State 泊位就弹确认框（列出泊位名，提供停止并退出/取消）；10 秒后仍未停止的调用 `Supervisor::forceKill` 再退出
    - 手动验证：前置为关闭该设置并运行一个泊位 → 关闭主窗口 → 出现确认框；选"停止并退出" → 泊位停止，进程退出；开启该设置后关闭窗口 → 只隐藏，托盘仍在
    - _Requirements: 18.5, 18.6, 18.7, 18.8, 18.9_

- [x] 11. 系统通知
  - [x] 11.1 实现 `Notifier`
    - `src/Notifier.{h,cpp}`：用 `QSystemTrayIcon::showMessage` 发通知，用 `messageClicked` 恢复主窗口并选中泊位（泊位已删除时只提示）；不支持通知或托盘不可见时写入泊位日志或 `berth.log`
    - 崩溃通知（替换 5.4 的 notice 降级方案）、就绪通知（同一次启动只发一次；主窗口在前台且选中该泊位时改为窗口内提示，5 秒后消失）；设置页加三类开关，默认开启；更新通知的接口先留好（32.2 调用）
    - 手动验证：前置为三类开关都开启 → 最小化 Berth 后启动泊位 → 收到就绪通知，点击后主窗口前置并选中该泊位；结束 dsh 进程 → 2 秒内收到带退出码的崩溃通知；关闭崩溃开关后重复 → 不再有通知
    - _Requirements: 3.6, 11.3, 19.1, 19.2, 19.4, 19.5, 19.6, 19.7, 19.8, 19.9_

- [x] 12. 日志页增强
  - [x] 12.1 实现 `core/LogText`
    - `classify(line)`、`find(text, kw)`（不区分大小写、不重叠）、循环跳转序号、`firstLineStart(chunk)`、分段拼接、`humanSize(bytes)`
    - 手动验证：由 12.2–12.5 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 20.1, 20.4, 20.5, 20.12, 20.13_
  - [x]* 12.2 编写性质测试 `tests/tst_logtext_level.cpp`
    - **Property 24: 日志行级别与过滤**
    - **Validates: Requirements 20.4, 20.5**
  - [x]* 12.3 编写性质测试 `tests/tst_logtext_find.cpp`
    - **Property 25: 日志搜索计数与循环跳转**
    - **Validates: Requirements 20.1, 20.2, 20.3**
  - [x]* 12.4 编写性质测试 `tests/tst_logtext_tail.cpp`
    - **Property 26: 日志尾部分段加载**
    - **Validates: Requirements 20.13**
  - [x]* 12.5 编写性质测试 `tests/tst_logtext_size.cpp`
    - **Property 27: 文件大小格式**
    - **Validates: Requirements 20.12**
  - [x] 12.6 实现 `LogService`
    - `src/LogService.{h,cpp}`：在工作线程执行 `loadTail`、`loadMore`（每次 64KB）；`clear`：Active_State 下只记录偏移（偏移保存到泊位离开 Active_State 为止），否则截断文件；`exportTo` 用 `QSaveFile` 逐字节复制；每 2 秒刷新文件大小；写出前对 Berth 标记行做脱敏
    - 手动验证：前置为日志大于 64KB → 打开日志页 → 只加载末尾一段，顶部有"加载更多"，点击后可见行不跳动
    - _Requirements: 20.8, 20.9, 20.10, 20.11, 20.12, 20.13, 26.1_
  - [x] 12.7 日志页 QML
    - 新增 `qml/LogViewer.qml`：搜索框（300ms 防抖，最长 256 字符）、高亮、x/y 计数与上一个/下一个、仅显示匹配行、级别过滤、暂停滚动、复制所选/复制全部/清空/导出、文件大小显示、空日志提示、错误提示
    - 手动验证：前置为泊位日志中有 error 与 warn 行 → 搜索"error" → 显示计数，跳转会循环；级别选"错误"并打开"仅显示匹配行" → 只剩交集；运行中点清空 → 界面清空，文件大小不变；导出后比较文件 → 与原日志逐字节相同
    - _Requirements: 20.1, 20.2, 20.3, 20.4, 20.5, 20.6, 20.7, 20.8, 20.9, 20.10, 20.11, 20.12_

- [ ] 13. Checkpoint：阶段 D
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

### 阶段 A：管理（需求 1–10）

- [x] 14. 插件启用/禁用与批量操作
  - [x] 14.1 实现 `core/PluginSet` 与 `core/AffectedSet`
    - `setEnabled(pkg, name, on)`（只改 bundles）；`list(pkg, showCore, quarantined)`，版本为空时显示"—"；`BatchSummary`（汇总成功与失败，判定是否需要触发 Restart_Hint）
    - `affected(instances, home, profile)`：规范化 home 后不区分大小写比较，只取启动中或运行中的泊位；`merge(a, b)` 去重
    - 手动验证：由 14.2–14.5 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 1.1, 1.2, 1.5, 1.6, 1.7, 1.8, 1.9, 4.1, 4.6_
  - [x]* 14.2 编写性质测试 `tests/tst_pluginset_toggle.cpp`
    - **Property 9: 启用开关只影响目标插件且可往返**
    - **Validates: Requirements 1.2, 1.9**
  - [x]* 14.3 编写性质测试 `tests/tst_pluginset_list.cpp`
    - **Property 10: 插件列表内容与核心包过滤**
    - **Validates: Requirements 1.1, 1.5**
  - [x]* 14.4 编写性质测试 `tests/tst_batchsummary.cpp`
    - **Property 11: 批量汇总**
    - **Validates: Requirements 1.6, 1.7, 1.8**
  - [x]* 14.5 编写性质测试 `tests/tst_affected.cpp`
    - **Property 13: 受影响泊位判定与合并**
    - **Validates: Requirements 4.1, 4.6, 8.4**
  - [x] 14.6 实现 `PluginManager` 的启用与批量操作，重做插件页
    - `src/PluginManager.{h,cpp}`：`setEnabled` 先快照再用 QSaveFile 写入，失败时回滚并退回开关；`batch(op, names)` 逐个处理，失败项回滚，显示 x/y 进度；同一 profile 的操作互斥；package.json 读取失败时禁用写操作
    - `qml/PluginsDialog.qml`：(DSH_HOME, profile) 单选筛选器（默认第一项，切换时清空多选）、显示核心包开关、多选、批量按钮（未选中时禁用）、汇总
    - 手动验证：前置为 profile 中至少装有 2 个插件 → 禁用其中一个 → package.json 的 bundles 少了这一项，dependencies 不变；批量禁用 2 个 → 汇总显示成功 2；把 package.json 设为只读后切换开关 → 显示错误，开关退回
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.10, 1.11, 1.12, 26.4_

- [x] 15. 插件变动后的重启提示
  - [x] 15.1 实现 `RestartHintController` 与提示条
    - `src/RestartHintController.{h,cpp}`：`raise(home, profile)` 合并进当前提示；"全部重启"逐个执行（前一个运行中或失败后才开始下一个，跳过已不活跃的泊位），结束后汇总；"稍后"时加"待重启"标记，泊位进入运行中后移除；PluginManager 成功后调用
    - `qml/RestartHintBar.qml` 与泊位卡片上的"待重启"标记
    - 手动验证：前置为泊位运行中 → 在插件页禁用一个插件 → 1 秒内出现提示并列出该泊位；点"稍后" → 卡片显示"待重启"；重启该泊位 → 标记消失
    - _Requirements: 1.8, 4.1, 4.2, 4.3, 4.4, 4.5, 4.6, 4.7_

- [x] 16. 插件市场与安装
  - [x] 16.1 实现 `core/CatalogCodec` 与 `core/PackageSpec`
    - `parse`（错误信息带字段名或位置）、`print`、`filter(list, kw)`
    - `PackageSpec`：npm 包名（scope、`@版本`/`@tag`）、git URL、目录源 URL 的校验
    - 手动验证：由 16.2–16.5 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.11_
  - [x]* 16.2 编写性质测试 `tests/tst_catalog_roundtrip.cpp`
    - **Property 5: 插件目录往返**
    - **Validates: Requirements 2.3, 2.5, 2.6**
  - [x]* 16.3 编写性质测试 `tests/tst_catalog_invalid.cpp`
    - **Property 6: 非法目录被拒绝并指出字段**
    - **Validates: Requirements 2.4**
  - [x]* 16.4 编写性质测试 `tests/tst_catalog_filter.cpp`
    - **Property 7: 市场搜索过滤**
    - **Validates: Requirements 2.7**
  - [x]* 16.5 编写性质测试 `tests/tst_packagespec.cpp`
    - **Property 8: 输入格式校验**
    - **Validates: Requirements 2.2, 2.11**
  - [x] 16.6 PluginManager 目录拉取与安装
    - `fetchCatalog()`：使用共享的 QNetworkAccessManager，15 秒超时后 abort；失败时退回 `catalog-cache.json`
    - `install(home, profile, spec, channel)`：先快照 package.json，经 ProcessRunner 执行 `dsh plugin --profile <p> add <spec>`（渠道映射为 dist-tag），成功后确保包名在 bundles 中，失败时回滚并显示最后 20 行；记录 `pluginChannels`
    - 设置页加 `catalogUrl`（只接受 http/https）
    - 手动验证：前置为网络可用 → 刷新市场 → 显示加载状态，完成后出现列表；断网后刷新 → 显示错误与缓存列表；安装一个不存在的包名 → 显示错误摘要，package.json 不变
    - _Requirements: 2.1, 2.2, 2.4, 2.9, 2.10, 2.12, 2.14, 2.16, 26.1_
  - [x] 16.7 插件市场 QML
    - 新增 `qml/MarketPage.qml`：搜索（300ms 防抖）、渠道选择器、没有对应渠道版本时标注并禁用安装、目标泊位或 profile 选择（未选时禁用安装）、手动输入包名或 git URL 并校验、安装进度（同一 profile 的其他操作禁用）
    - 手动验证：前置为市场已加载 → 输入 `Bad Name` 点安装 → 提示格式错误，输入框内容保留；选 beta 渠道 → 没有 beta 版本的条目显示为不可安装
    - _Requirements: 2.7, 2.8, 2.9, 2.10, 2.11, 2.13, 2.15_

- [x] 17. 启动前插件自检与隔离
  - [x] 17.1 实现 `core/SemVer`
    - 解析时忽略开头的 v/V；按 SemVer 2.0 比较；`satisfies` 支持 `^ ~ >= <= > < = x * ||` 与连字符范围
    - 手动验证：由 17.2/17.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 3.1, 6.2, 14.8, 14.9, 15.2_
  - [x]* 17.2 编写性质测试 `tests/tst_semver_order.cpp`
    - **Property 3: SemVer 全序与格式规则**
    - **Validates: Requirements 14.8, 14.9, 6.2**
  - [x]* 17.3 编写性质测试 `tests/tst_semver_range.cpp`
    - **Property 4: 版本范围匹配与参考实现一致**
    - **Validates: Requirements 3.1, 15.2**
    - 附加边界：Node 23.x 不满足 `^22.19.0 || >=24.0.0`
  - [x] 17.4 实现 `core/PreflightDecision`
    - `decide(before, after, repairOutcome)`：输出隔离集合、剩余集合与原因枚举（missing / dep-unresolved / install-failed / install-timeout / no-package-manager）
    - 手动验证：由 17.5 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 3.4, 3.5, 3.9_
  - [x]* 17.5 编写性质测试 `tests/tst_preflight.cpp`
    - **Property 12: 自检隔离决策**
    - **Validates: Requirements 3.4, 3.5, 3.9**
  - [x] 17.6 实现 `Preflight` 与 quarantine.json，接入启动流水线
    - `src/Preflight.{h,cpp}`：在工作线程中按 canonicalFilePath 逐级向上解析依赖；每个 profile 一个串行队列，修复命令为 `dsh plugin --profile p install`，超时 300 秒；隔离时从 bundles 移除并写入 `quarantine.json`；每一步写入泊位日志
    - package.json 读取失败时中止启动，置为"已停止"并报错；设置项 `preflightEnabled` 关闭时直接放行；替换 4.5 中的占位步骤；有插件被隔离时发一条汇总通知
    - 手动验证：前置为把某个启用插件在 node_modules 中的目录改名 → 启动泊位 → 日志中有检查失败、修复开始/结束记录；修复后仍失败的插件被隔离，泊位照常启动并收到汇总通知
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.8, 3.9, 3.11, 3.12_
  - [x] 17.7 隔离标记与解除隔离界面
    - 插件列表显示"已隔离"与原因；泊位详情列出被隔离插件；`unquarantine` 删除记录、恢复启用，并触发 Restart_Hint
    - 手动验证：前置为已有一个被隔离的插件 → 插件页显示标记与原因 → 点"解除隔离" → 插件恢复启用，quarantine.json 中该条目被删除
    - _Requirements: 3.6, 3.7, 3.10_

- [x] 18. 泊位环境变量与额外启动参数
  - [x] 18.1 实现 `core/EnvTable` 校验
    - 键规则：非空、不含 `=` 和空白、长度 ≤ 256；不区分大小写去重；返回违规行号；行数上限 100，参数上限 50；识别 DSH_HOME 键
    - 手动验证：由 18.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 5.1, 5.5, 5.6, 5.7_
  - [x]* 18.2 编写性质测试 `tests/tst_envtable.cpp`
    - **Property 14: 环境变量键校验**
    - **Validates: Requirements 5.6, 5.7**
  - [x] 18.3 泊位编辑界面的环境变量表与参数列表，接入启动
    - `qml/InstancePane.qml`：环境变量表（键、值、敏感，可增删改，敏感值用掩码）、DSH_HOME 提示、参数列表（可排序）、冲突参数提示（允许保存）、非法或重复行标红并阻止保存
    - `updateInstance` 扩展参数；启动流水线把 env 表与 extraArgs 传入 LaunchEnv；Berth 写出的日志对敏感值做脱敏
    - 手动验证：前置为泊位已停止 → 添加 `FOO=bar` 与参数 `--port 1` → 保存时提示冲突但允许保存；启动后日志中的命令行不含 `--port 1`，dsh 进程环境中有 FOO（可用 Process Explorer 查看）；添加两行 `foo`/`FOO` → 拒绝保存
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7, 5.8, 5.9, 5.11_

- [x] 19. 多版本 dsh 共存
  - [x] 19.1 实现 `DshVersionStore` 并接入启动流水线
    - `src/DshVersionStore.{h,cpp}`：经 ProcessRunner 执行 `npm install -g --prefix <ver>.partial @deepseek-ai/dsh@<ver>`，成功后改名，失败时删除目录并显示摘要；拒绝重复安装；引用计数 ≥ 1 时拒绝删除；`executableFor(ver)`；列表按 SemVer 降序
    - 启动流水线按泊位的 `dshVersion` 解析可执行文件，缺失时拒绝启动并说明版本号与原因
    - 手动验证：前置为 npm 可用 → 安装一个旧版本 → `%APPDATA%\dsh-Berth\dsh-versions\<ver>\dsh.cmd` 存在，System_Dsh 不变；手动删除该目录后启动绑定它的泊位 → 保持"已停止"并提示缺失
    - _Requirements: 6.1, 6.2, 6.4, 6.5, 6.6, 6.7, 6.8, 6.9_
  - [x] 19.2 版本管理界面与泊位版本选择器
    - 新增 `qml/VersionsPage.qml`：版本号、路径、引用计数、安装中状态（同版本的安装与删除禁用）、删除确认；InstancePane 中加版本选择器（默认系统 dsh，缺失的版本标记"缺失"）
    - 手动验证：前置为已安装一个版本并被泊位绑定 → 删除该版本 → 被拒绝并列出泊位名；解除绑定后删除 → 2 秒内从列表和选择器中消失
    - _Requirements: 6.2, 6.3, 6.6, 6.7, 6.10_

- [x] 20. 泊位/Profile 整包导出与导入
  - [x] 20.1 实现 `core/BundleCodec`
    - `encode`/`decode(maxFormat)`；`excluded(relPath)`（排除任意层级的 node_modules 段与 `.log` 文件）；拒绝缺少 manifest 或 plugins 的输入；敏感值可省略
    - 手动验证：由 20.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.8, 7.11_
  - [x]* 20.2 编写性质测试 `tests/tst_bundle.cpp`
    - **Property 35: Bundle 往返与排除**
    - **Validates: Requirements 7.1, 7.3, 7.8, 7.11**
  - [x] 20.3 实现 `BundleIO`
    - `src/BundleIO.{h,cpp}`：在工作线程读取文件，用 QSaveFile 写出；导入流程为校验、冲突检测（profile 重名、端口）、写入、依赖重建；重建失败时标记"依赖未就绪"并支持重试；列出需要补填的敏感键
    - 手动验证：前置为一个带敏感 env 的泊位 → 导出（不含敏感值）→ 打开文件，没有 node_modules，也没有敏感原值；导入到同一 DSH_HOME → 提示 profile 重名，选择重命名后创建成功，列出需要补填的键
    - _Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8, 7.9, 7.10, 26.1_
  - [x] 20.4 导出导入界面
    - 泊位与 profile 菜单中加"导出"（含"包含敏感值"勾选项，默认不勾选）和"导入"对话框（冲突选项：重命名、自动分配端口、取消）
    - 手动验证：前置为有一个导出的 Bundle → 导入时选"取消" → instances.json 与 profiles 目录不变
    - _Requirements: 7.4, 7.6, 7.7, 7.10_

- [x] 21. Skills 与 MCP 管理
  - [x] 21.1 实现 `core/PatchYaml`
    - 行级解析 mcp-client 行（serverName、transport、command/url、disabled）；`setDisabled`：没有该键时插入，启用时删除，保证往返逐字节一致；`!!js`、flow 风格、锚点标为只读并说明原因
    - 手动验证：由 21.2/21.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 8.1, 8.2, 8.5_
  - [x]* 21.2 编写性质测试 `tests/tst_patchyaml.cpp`
    - **Property 34: MCP patch 行编辑往返**
    - **Validates: Requirements 8.1, 8.2, 8.5**
  - [x]* 21.3 编写单元测试 `tests/tst_patchyaml_edge.cpp`
    - `disabled: !!js …` 只读；flow 风格只读；文件中没有 mcp 行时结果为空
    - _Requirements: 8.3, 8.8_
  - [x] 21.4 实现 `ExtensionManager` 的 MCP 部分
    - `src/ExtensionManager.{h,cpp}`：读取 profile 与 `$DSH_HOME` 的 cordis.patch.yml；写入前比较 mtime、大小与 SHA-256，有外部修改就放弃并重新读取；用 QSaveFile 写入，失败时开关退回；成功后触发 Restart_Hint；文件不存在时显示空状态，不创建文件
    - 手动验证：前置为 profile 中配置了一个 MCP → 禁用它 → 文件中只有该行多了 `disabled: true`；再启用 → 文件与原来逐字节相同
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.6, 8.7, 8.8_
  - [x] 21.5 实现 Skills 列表与开关（待定 TODO #1，暂按方案 A）
    - 开始前先与用户确认 TODO.md #1；如改选方案 B，只实现只读列表
    - 扫描 `<dshHome>/skills`（跳过 `.system`）、workspace 的 `.dsh/skills`、`.agents/skills`；禁用时移到 `<root>/.berth-disabled/`，启用时移回；移动失败时开关退回
    - 手动验证：前置为 `<DSH_HOME>/skills/foo/SKILL.md` 存在 → 禁用 foo → 目录被移到 `.berth-disabled/foo`；启用 → 移回原位
    - _Requirements: 8.1, 8.2, 8.5, 8.6, 27.2_
  - [x] 21.6 扩展页 QML
    - 新增 `qml/ExtensionsPage.qml`：Skill 与 MCP 两个列表（按名称升序）、开关、只读原因、解析错误时禁用全部写操作、空状态
    - 手动验证：前置为 cordis.patch.yml 语法错误 → 打开扩展页 → 显示文件路径与原因，开关全部禁用
    - _Requirements: 8.1, 8.3, 8.8_

- [x] 22. 泊位图标、分组、标签与备注
  - [x] 22.1 实现 `core/InstanceMeta`
    - 分组名、标签、备注的长度与去重校验；图片扩展名与大小校验；`groupBy`（空白分组名归入"未分组"）；`search(kw)` 按名称或标签匹配，隐藏空分组
    - 手动验证：由 22.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 9.1, 9.3, 9.4, 9.5, 9.6_
  - [x]* 22.2 编写性质测试 `tests/tst_instancemeta.cpp`
    - **Property 23: 分组、标签与搜索**
    - **Validates: Requirements 9.1, 9.4, 9.5, 9.6**
  - [x] 22.3 编辑界面与分组列表
    - InstancePane 加图标（内置、文件、无）、分组、标签、备注；本地图片复制到 `icons/` 并保存相对路径；图标加载失败时显示默认图标；Main.qml 泊位列表改为可折叠分组，加搜索框（300ms 防抖）
    - 手动验证：前置为两个泊位 → 设置同一分组并添加标签 → 列表显示分组标题与数量；搜索标签 → 只剩匹配的泊位；删除原图片 → 图标仍然显示
    - _Requirements: 9.1, 9.2, 9.3, 9.4, 9.5, 9.6, 9.7, 9.8_
  - [x] 22.4 开放分组批量操作，托盘使用自定义图标
    - 显示 BatchLauncher 的"启动分组/停止分组"入口（包括"未分组"）；9.4 的独立托盘图标改用泊位自定义图标，缺失时退回默认图标
    - 手动验证：前置为一个有 2 个泊位的分组 → 点"启动分组" → 按错峰间隔依次启动；开启独立托盘图标 → 显示自定义图标
    - _Requirements: 13.1, 13.2, 17.7_

- [x] 23. 在外部工具中打开 workspace
  - [x] 23.1 实现 `WorkspaceOpener` 并接入详情页
    - `src/WorkspaceOpener.{h,cpp}`：检测 code/cursor 可执行文件（PATH 与常见安装路径）；校验 workspace 路径；用 `QProcess::startDetached` 按参数数组启动（支持空格和非 ASCII 路径）；资源管理器用 `explorer.exe <path>` 打开
    - 详情页三个按钮带启用状态与悬停原因；打开详情和保存 workspace 时重新检测；启动失败时提示
    - 手动验证：前置为 workspace 路径含中文和空格 → 点"在 VS Code 中打开" → VS Code 打开该文件夹；清空 workspace → 三个按钮都禁用，悬停显示原因
    - _Requirements: 10.1, 10.2, 10.3, 10.4, 10.5, 10.6, 10.7_

- [ ] 24. Checkpoint：阶段 A
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

### 阶段 E：统计（需求 21–24）

- [x] 25. 会话统计
  - [x] 25.1 实现 `core/ProjectKey` 与 `core/SessionAgg`
    - 按 `format.ts` 逐字符复刻 `projectKey(cwd)`；`aggregate(dirs, now)` 计算总数、活跃数（30 分钟内有修改）与最近活动时间
    - 手动验证：由 25.2/25.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 21.1_
  - [x]* 25.2 编写性质测试 `tests/tst_session.cpp`
    - **Property 28: 会话统计不变量**
    - **Validates: Requirements 21.1, 21.2**
  - [x]* 25.3 编写单元测试 `tests/tst_projectkey.cpp`
    - 对照 dsh `format.ts` 的样例输入与输出
    - _Requirements: 21.1_
  - [x] 25.4 实现 `StatsService::sessionStats` 并接入详情页
    - `src/StatsService.{h,cpp}`：在工作线程只读枚举目录，5 秒超时后放弃；原因区分不存在、无法读取、无法解析、超时；运行中每 30 秒刷新，其他状态只在打开详情时读取；详情页显示计数、`YYYY-MM-DD HH:mm` 与口径说明
    - 手动验证：前置为泊位有过会话 → 打开详情 → 显示会话数与最近活动时间；把 DSH_HOME 指向空目录 → 显示"暂无数据"，悬停提示为"文件不存在"
    - _Requirements: 21.1, 21.2, 21.3, 21.4, 21.5, 21.6, 21.7, 21.8_

- [x] 26. Token 用量与费用统计（待定 TODO #2，暂按方案 A）
  - [x] 26.1 实现 `core/UsageAgg`
    - 开始前先与用户确认 TODO.md #2；如改选方案 B，本任务只保留 26.5 的降级提示
    - 按本地时区划分区间（今日、7 天、30 天、全部）；按日、泊位、模型汇总；未知模型归类；缺失 token 按 0 计；`cost` 保留 4 位小数，统计未计价模型；单价校验
    - 手动验证：由 26.2/26.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 22.1, 22.2, 22.3, 22.4, 22.9_
  - [x]* 26.2 编写性质测试 `tests/tst_usage_agg.cpp`
    - **Property 29: 用量汇总不变量**
    - **Validates: Requirements 22.1, 22.2, 22.4**
  - [x]* 26.3 编写性质测试 `tests/tst_usage_cost.cpp`
    - **Property 30: 费用计算与单价校验**
    - **Validates: Requirements 22.3, 22.9**
  - [x] 26.4 编写 `usage-scan.mjs`，实现 `StatsService::usage`（待定 TODO #2，暂按方案 A）
    - 开始前先与用户确认 TODO.md #2
    - `assets/helpers/usage-scan.mjs` 加入 qrc，首次使用时释放到 `helpers/`；只读打开文件，用 `zlib.zstdDecompressSync` 解压，跳过坏行，stdout 输出按日、项目、模型聚合的 JSON
    - 经 ProcessRunner 用 `nodeExecutable` 或 PATH 上的 node 运行；单个泊位失败时排除并注明；没有任何 usage 时按降级方案处理
    - 手动验证：前置为 Node ≥ 22.19，泊位有过对话 → 打开用量页 → 3 秒内显示今日 token；把 node 改名 → 显示失败原因，不弹模态框
    - _Requirements: 22.1, 22.5, 22.6, 22.7, 22.8, 26.1_
  - [x] 26.5 用量页与单价设置（待定 TODO #2，暂按方案 A）
    - 开始前先与用户确认 TODO.md #2
    - 新增 `qml/UsagePage.qml`：时间范围、按泊位和按模型的表格、合计、"估算"标注、未计价说明、刷新（保留上次结果）、"当前 dsh 版本未提供用量数据"提示；设置页加模型单价编辑与校验
    - 手动验证：前置为用量页有数据 → 配置一个模型的单价 → 显示 4 位小数的费用；输入 10001 → 拒绝保存
    - _Requirements: 22.2, 22.3, 22.5, 22.7, 22.8, 22.9_

- [x] 27. 进程运行指标
  - [x] 27.1 实现 `core/MetricsState`
    - `onStart/onExit/addRuntime/reset`、JSON 往返、`cpuPercent(d100ns, dWallMs, cores)`
    - 手动验证：由 27.2/27.3 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 23.1, 23.2, 23.3, 23.6, 23.7_
  - [x]* 27.2 编写性质测试 `tests/tst_metrics.cpp`
    - **Property 31: 进程指标不变量**
    - **Validates: Requirements 23.1, 23.2, 23.6, 23.7**
  - [x]* 27.3 编写性质测试 `tests/tst_cpupercent.cpp`
    - **Property 32: CPU 百分比换算**
    - **Validates: Requirements 23.3**
  - [x] 27.4 实现 `ProcessMetrics`
    - `src/ProcessMetrics.{h,cpp}`：每 2 秒经 `jobHandle` 查询 CPU 与内存（外部泊位只采单个 PID，标注"仅主进程"），采样失败时跳过；启动或退出事件后 1 秒内写入 `metrics.json`，运行中每 60 秒写入一次；文件损坏时清零并提示；保留 150 个采样点的趋势
    - 手动验证：前置为启动一个泊位 → 2 分钟后关闭再打开 Berth → 运行时长与启动次数得以保留
    - _Requirements: 23.1, 23.2, 23.3, 23.5, 23.9, 23.10_
  - [x] 27.5 详情页指标展示与重置
    - 显示当前 CPU/内存、5 分钟趋势图（QML Canvas）与累计数据；Active_State 下禁用"重置统计"并给出悬停原因；重置需确认
    - 手动验证：前置为泊位运行中 → 详情页每 2 秒刷新数值，趋势图增长；停止后点重置并确认 → 三项计数归零
    - _Requirements: 23.4, 23.6, 23.8_

- [x] 28. 顶部汇总面板
  - [x] 28.1 实现 `core/Summary`
    - `compute(statuses, samples)` 与 `formatMem(mb)`
    - 手动验证：由 28.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 24.1, 24.2, 24.3, 24.9_
  - [x]* 28.2 编写性质测试 `tests/tst_summary.cpp`
    - **Property 33: 汇总面板计算**
    - **Validates: Requirements 24.1, 24.2, 24.3, 24.9**
  - [x] 28.3 汇总面板 QML
    - 新增 `qml/SummaryPanel.qml` 放在 Main 顶部：五项指标，状态或采样变化时 2 秒内刷新；点击运行中或失败数字进行筛选或取消筛选；可用更新数量先隐藏（32.2 开启）
    - 手动验证：前置为 3 个泊位，其中 1 个运行中 → 顶部显示 3/1/0 与内存、CPU；点"运行中" → 列表只剩该泊位，再点一次恢复
    - _Requirements: 24.1, 24.2, 24.3, 24.4, 24.5, 24.6_

- [ ] 29. Checkpoint：阶段 E
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

### 阶段 C：更新与环境（需求 14–16）

- [x] 30. 代理设置与自动探测
  - [x] 30.1 实现 `core/ProxyConfig`，扩展 Redact
    - 手动代理字段校验（主机非空，端口 1–65535）；生成代理环境变量（HTTP_PROXY、HTTPS_PROXY 及小写形式）与 registry 环境变量；`Redact::redactProxyUrl`
    - 手动验证：由 30.2 覆盖 → 运行 berth_tests → 通过
    - _Requirements: 16.1, 16.3, 16.9_
  - [x]* 30.2 编写性质测试 `tests/tst_proxyconfig.cpp`
    - **Property 36: 代理字段校验**
    - **Validates: Requirements 16.1, 16.9**
  - [x] 30.3 实现 `ProxyManager` 并接入各处
    - `src/ProxyManager.{h,cpp}`：读取系统代理；逐个探测端口（每个 1 秒，总计 10 秒以内）；测试连接（npm 与 GitHub，各 10 秒，区分失败类别）；密码用 DPAPI 加密，解密失败时清空；镜像兜底只重试一次
    - 把代理应用到共享 QNetworkAccessManager，把 `childEnv()` 传给 ProcessRunner，`instanceInheritProxy` 为真时传给 LaunchEnv
    - 手动验证：前置为本机 7890 端口有代理 → 点自动探测 → 候选中出现 127.0.0.1:7890；settings.json 中只有 `passwordDpapi`，没有明文密码
    - _Requirements: 16.2, 16.3, 16.4, 16.5, 16.6, 16.7, 16.8, 16.10_
  - [x] 30.4 代理设置 QML
    - SettingsPane 加代理模式、手动字段、自动探测候选、测试连接结果、npm 镜像（官方、预置、自定义）、镜像兜底、泊位继承代理；非法字段拒绝保存
    - 手动验证：前置为手动模式 → 端口填 70000 保存 → 拒绝保存，已保存设置不变；测试连接 → 显示耗时或失败类别
    - _Requirements: 16.1, 16.4, 16.6, 16.9_

- [x] 31. 环境检测与诊断报告
  - [x] 31.1 实现 `EnvChecker`
    - `src/EnvChecker.{h,cpp}`：6 项检测（每项 10 秒超时；Node 按 `^22.19.0 || >=24.0.0` 用 SemVer 判定；WebView2 查注册表）；检测 winget；一键安装前确认完整命令，失败时显示退出码与最后 20 行，结束后只重测该项
    - `exportReport(path)`：包含 `RtlGetVersion`、检测结果、脱敏后的代理、泊位摘要，全文经 Redact 处理，用 QSaveFile 写出；未检测过时先检测
    - 手动验证：前置为 pnpm 不在 PATH 中 → 打开环境页 → pnpm 显示"缺失"并附下载链接；导出报告 → 搜索代理密码与敏感 env 值，都搜不到
    - _Requirements: 15.1, 15.2, 15.3, 15.4, 15.5, 15.6, 15.7, 15.8, 15.9_
  - [x] 31.2 环境页 QML，首次显示时自动检测
    - 新增 `qml/EnvPage.qml`：状态、版本、路径、链接、一键安装确认框、导出按钮；主窗口第一次显示时后台检测一次
    - 手动验证：前置为 Berth 刚启动 → 打开环境页 → 已有检测结果或显示"检测中"，界面可以操作
    - _Requirements: 15.1, 15.3, 15.4_

- [x] 32. dsh 与插件更新面板
  - [x] 32.1 实现 `UpdateCenter`
    - `src/UpdateCenter.{h,cpp}`：从 npm registry 查询 dsh 的 `latest` 与插件对应渠道的版本（30 秒超时，按 ProxyManager 的镜像设置）；用 SemVer 比较，非法版本显示"版本无法比较"；按 `updateCheckHours` 定时检查，也可关闭；全部检查进行中不重复发起
    - 升级：System_Dsh 复用 DshUpdate；多版本通过 DshVersionStore 安装新目录；插件走 PluginManager 并触发 Restart_Hint；一键升级按先 dsh 后插件的顺序执行，结束后汇总
    - 手动验证：前置为装有旧版插件 → 打开更新面板 → 该插件显示"有更新"；点升级 → 成功后出现 Restart_Hint
    - _Requirements: 14.1, 14.2, 14.3, 14.4, 14.5, 14.6, 14.7, 14.8, 14.9, 14.10_
  - [x] 32.2 更新面板 QML，接入汇总面板与通知
    - 新增 `qml/UpdatePage.qml`：两栏布局、最近检查时间、单项与一键升级（进行中时禁用）；SummaryPanel 显示可用更新数量，点击打开面板；每个升级任务结束后调用 Notifier 的更新通知；设置页加检查间隔
    - 手动验证：前置为有 1 项可用更新 → 顶部显示数量 1，点击打开面板；升级完成 → 收到带新版本号的更新通知
    - _Requirements: 14.1, 14.2, 14.6, 14.10, 19.3, 24.7, 24.8_

- [ ] 33. Checkpoint：最终
  - 请用户编译运行 berth_tests 与 dsh-berth 并按手动验证步骤确认，有问题时询问用户。

## Notes

- 标 `*` 的子任务是可选测试，可以跳过以加快 MVP；它们仍列在依赖图中。
- 性质测试每条至少迭代 100 次，函数前标注 `// Feature: berth-enhancements, Property N: <标题>`。
- AI 不执行编译、构建、测试或启动程序；验证统一在 Checkpoint 由用户完成。
- 21.5、26.1、26.4、26.5 开始前先与用户确认 TODO.md 中对应的待定项。

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1"] },
    { "id": 1, "tasks": ["2.1", "2.7"] },
    { "id": 2, "tasks": ["2.2", "2.3", "2.4", "2.8", "2.9"] },
    { "id": 3, "tasks": ["2.5", "2.6"] },
    { "id": 4, "tasks": ["4.1", "5.1", "6.1", "7.1"] },
    { "id": 5, "tasks": ["4.2", "4.3", "5.2", "5.3", "6.2", "7.2", "4.4"] },
    { "id": 6, "tasks": ["4.5"] },
    { "id": 7, "tasks": ["5.4"] },
    { "id": 8, "tasks": ["6.3"] },
    { "id": 9, "tasks": ["6.4"] },
    { "id": 10, "tasks": ["7.3"] },
    { "id": 11, "tasks": ["9.1", "12.1"] },
    { "id": 12, "tasks": ["9.2", "12.2", "12.3", "12.4", "12.5", "9.3"] },
    { "id": 13, "tasks": ["9.4"] },
    { "id": 14, "tasks": ["10.1"] },
    { "id": 15, "tasks": ["10.2", "10.3"] },
    { "id": 16, "tasks": ["11.1"] },
    { "id": 17, "tasks": ["12.6"] },
    { "id": 18, "tasks": ["12.7"] },
    { "id": 19, "tasks": ["14.1", "16.1", "17.1", "17.4", "18.1", "20.1", "21.1", "22.1"] },
    { "id": 20, "tasks": ["14.2", "14.3", "14.4", "14.5", "16.2", "16.3", "16.4", "16.5", "17.2", "17.3", "17.5", "18.2", "20.2", "21.2", "21.3", "22.2", "14.6"] },
    { "id": 21, "tasks": ["15.1"] },
    { "id": 22, "tasks": ["16.6"] },
    { "id": 23, "tasks": ["16.7"] },
    { "id": 24, "tasks": ["17.6"] },
    { "id": 25, "tasks": ["17.7"] },
    { "id": 26, "tasks": ["18.3"] },
    { "id": 27, "tasks": ["19.1"] },
    { "id": 28, "tasks": ["19.2"] },
    { "id": 29, "tasks": ["20.3"] },
    { "id": 30, "tasks": ["20.4"] },
    { "id": 31, "tasks": ["21.4"] },
    { "id": 32, "tasks": ["21.5"] },
    { "id": 33, "tasks": ["21.6"] },
    { "id": 34, "tasks": ["22.3"] },
    { "id": 35, "tasks": ["22.4"] },
    { "id": 36, "tasks": ["23.1"] },
    { "id": 37, "tasks": ["25.1", "26.1", "27.1", "28.1"] },
    { "id": 38, "tasks": ["25.2", "25.3", "26.2", "26.3", "27.2", "27.3", "28.2", "25.4"] },
    { "id": 39, "tasks": ["26.4"] },
    { "id": 40, "tasks": ["26.5"] },
    { "id": 41, "tasks": ["27.4"] },
    { "id": 42, "tasks": ["27.5"] },
    { "id": 43, "tasks": ["28.3"] },
    { "id": 44, "tasks": ["30.1"] },
    { "id": 45, "tasks": ["30.2", "30.3"] },
    { "id": 46, "tasks": ["30.4"] },
    { "id": 47, "tasks": ["31.1"] },
    { "id": 48, "tasks": ["31.2"] },
    { "id": 49, "tasks": ["32.1"] },
    { "id": 50, "tasks": ["32.2"] }
  ]
}
```