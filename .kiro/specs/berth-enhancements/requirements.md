# Requirements Document

## Introduction

DSH Berth 是基于 Qt 6.10.2 + QML 的非官方 Windows 控台，用于 DeepSeek Harness（dsh）的设置、启停和多开，不修改 dsh 本体。现有能力包括泊位增删改与复制、启停与重启（Job Object 连带结束子进程）、端口探活、内嵌 WebView2 与浏览器打开、日志尾部查看、profile 管理、插件列表与卸载、npm 更新 dsh、基础托盘。

本特性参考 GitHub 上的同类启动器（dsh-plugins/dsh-launcher、loudMore/dsh-launcher、deepseek-dsh/dsh-desktop、liguobao/dsh-desktop），在六个功能域补齐增强能力：

- A. 管理（需求 1–10）：插件、泊位配置、多版本 dsh、导入导出、Skills/MCP、外部编辑器
- B. 稳定性（需求 11–13）：崩溃重启、端口冲突、批量与错峰启动
- C. 更新与环境（需求 14–16）：dsh 与插件更新、环境检测、代理
- D. 托盘与体验（需求 17–20）：托盘菜单、自启与关闭到托盘、系统通知、日志页
- E. 数据统计（需求 21–24）：会话、Token 用量、进程指标、汇总面板
- F. 通用约束（需求 25–27）：数据兼容、异步执行、平台与边界

各功能域可以独立分阶段实施。需求中标注"依赖调研"的条目，具体数据位置与格式在设计阶段根据 dsh 源码（`deepseek-harness/` 子模块）确定。

## Glossary

- **Berth**：DSH Berth 应用整体。
- **Instance（泊位）**：Berth 管理的一套 dsh 服务配置，包含端口、profile、DSH_HOME、workspace 等字段，持久化在 `instances.json`。
- **Instance_Status（泊位状态）**：取值"已停止"、"启动中"、"运行中"、"停止中"、"失败"、"运行中（外部）"、"崩溃已停止"之一。
- **Supervisor**：Berth 中负责启动、停止、探活 dsh 进程的组件，使用 Windows Job Object 管理进程树。
- **Profile**：`<DSH_HOME>/profiles/<name>` 目录，包含 `package.json`、插件依赖与配置。
- **Plugin（插件）**：profile `package.json` 中由用户安装的依赖包；名称匹配 `@deepseek-ai/*` 的包称为 **Core_Package（核心包）**。
- **Plugin_Manager**：Berth 中负责插件列举、启用/禁用、安装、卸载的组件。
- **Plugin_Catalog（插件目录源）**：提供可安装插件列表的远程 JSON 源，默认 `https://dsh-plug.in/api/plugins.json`。
- **Catalog_Parser**：把 Plugin_Catalog 的 JSON 解析为插件条目列表的组件。
- **Catalog_Printer**：把插件条目列表序列化为 Plugin_Catalog 格式 JSON 的组件（用于本地缓存）。
- **Release_Channel（版本渠道）**：插件版本的发布渠道，取值 `stable`、`beta`、`alpha`。
- **Preflight_Checker（启动前自检）**：泊位启动前检查插件依赖的组件。
- **Quarantine（隔离）**：Preflight_Checker 将无法修复的插件从 profile 启用列表中移除，并记录隔离原因。
- **Restart_Hint（重启提示）**：插件变动后提示用户重启受影响泊位的界面元素。
- **Launch_Env（启动环境）**：泊位启动时注入 dsh 进程的环境变量与额外命令行参数。
- **Dsh_Version_Store（多版本仓库）**：Berth 数据目录下存放多个隔离安装的 dsh 版本的目录。
- **System_Dsh**：Settings 中 `dshExecutable` 指向的 dsh（默认 PATH 上的 `dsh`）。
- **Bundle（整包）**：泊位或 profile 的导出文件，包含配置文件与插件清单，不包含 `node_modules`。
- **Bundle_Exporter / Bundle_Importer**：生成与读取 Bundle 的组件。
- **Skill**：dsh 的技能定义条目（数据位置依赖调研）。
- **MCP_Server**：dsh 配置中的 Model Context Protocol 服务条目（数据位置依赖调研）。
- **Extension_Manager**：Berth 中负责 Skill 与 MCP_Server 查看和启用/禁用的组件。
- **Crash（崩溃）**：泊位进程在未经用户或 Berth 请求停止的情况下退出。
- **Auto_Restarter**：崩溃后按退避间隔自动重启泊位的组件。
- **Port_Checker**：检查端口占用及占用进程的组件。
- **Batch_Launcher**：执行批量启动/停止与开机错峰启动的组件。
- **Update_Center（更新面板）**：同时展示并执行 dsh 与插件更新的界面与后台组件。Berth 自身不做自更新（当前仅个人使用，由用户自行构建）。
- **Env_Checker（环境检测）**：检测 Node、npm、pnpm、Git、dsh、WebView2 的组件。
- **Diagnostic_Report（诊断报告）**：Env_Checker 导出的文本报告。
- **Proxy_Manager**：管理代理设置、代理探测与 npm 镜像的组件。
- **Tray（托盘）**：Berth 的系统托盘图标与菜单。
- **Notifier**：发送 Windows 系统通知的组件。
- **Log_Viewer（日志页）**：显示泊位日志的界面。
- **Session_Stats**：从 dsh session index 读取会话统计的组件（数据位置依赖调研）。
- **Usage_Stats**：读取 Token 用量与费用的组件（数据可用性依赖调研）。
- **Process_Metrics**：记录运行时长、启动次数、崩溃次数、退出码并采集 CPU/内存的组件。
- **Summary_Panel（汇总面板）**：主窗口顶部显示全局状态汇总的界面。
- **Active_State（活跃状态）**：泊位状态为"启动中"、"运行中"或"停止中"之一。

## Requirements

---

## A. 管理

### Requirement 1: 插件启用/禁用与批量操作

**User Story:** 作为 Berth 用户，我想在 Berth 里启用或禁用插件并批量处理，这样无需进入 Web UI 就能调整 profile 的插件组合。

#### Acceptance Criteria

1. WHEN 插件列表加载完成，THE Plugin_Manager SHALL 为当前筛选 profile 中的每个插件各显示一行，内容包括包名、`package.json` 中声明的版本和启用状态（启用/禁用）；声明版本为空的插件在版本处显示占位符"—"。
2. WHEN 用户切换某个插件的启用开关，THE Plugin_Manager SHALL 只修改该 profile 插件启用配置中这一个插件的启用状态，`package.json` 的 `dependencies` 字段、`node_modules` 目录内容和其他插件的启用状态都保持不变，并在写入成功后 1 秒内让列表中该插件的启用状态与新配置一致。
3. THE Plugin_Manager SHALL 提供单选的 profile 筛选器，选项是 Berth 已知的全部 (DSH_HOME, profile) 组合，每个组合只出现一次，选项文字同时写出 DSH_HOME 和 profile 名；打开插件页时默认选中列表中的第一个组合。
4. WHEN 用户在筛选器中选中一个 (DSH_HOME, profile) 组合，THE Plugin_Manager SHALL 在 1 秒内把列表替换为该组合对应 profile 的插件，不显示其他组合的插件，并清空之前的多选状态。
5. THE Plugin_Manager SHALL 提供"显示核心包"开关，默认关闭；开关关闭时列表中不显示任何 Core_Package，开关打开时 Core_Package 和其他插件一起显示。
6. WHEN 用户选中 1 个或多个插件后执行批量启用、批量禁用或批量卸载，THE Plugin_Manager SHALL 按选中顺序逐个处理，全部处理完后显示一份汇总，内容包括成功数、失败数，以及每个失败插件的包名和失败原因；未选中任何插件时，三个批量操作按钮都不可用。
7. IF 批量操作中某个插件处理失败，THEN THE Plugin_Manager SHALL 保持该插件的启用状态与安装状态和操作前一致，继续处理剩下的插件，并在汇总里把该插件标为失败。
8. IF 一次单个或批量变更中至少有 1 个插件成功，且目标 (DSH_HOME, profile) 被至少一个处于 Active_State 的泊位使用，THEN THE Plugin_Manager SHALL 在整次变更结束后按需求 4 只触发 1 次 Restart_Hint；没有任何插件成功时不触发 Restart_Hint。
9. FOR ALL 插件启用配置，对同一个插件先禁用再启用 SHALL 让 profile 配置回到与操作前等价的状态：每个插件的启用状态都与操作前相同，配置中与启用状态无关的其他内容也保持不变（往返性质）。
10. IF 切换启用开关时写入 profile 启用配置失败，THEN THE Plugin_Manager SHALL 显示一条写明失败插件名和失败原因的错误信息，把开关退回操作前的状态，并保持 profile 配置文件与操作前一致。
11. WHILE 某个 profile 上的批量操作正在进行，THE Plugin_Manager SHALL 以"已处理数/总数"的形式显示进度，并让该 profile 上的启用开关、批量操作按钮和卸载操作都不可用，直到汇总出现。
12. IF 选中 profile 的 `package.json` 或插件启用配置无法读取或解析，THEN THE Plugin_Manager SHALL 显示一条写明文件和原因的错误信息，并让该 profile 的启用开关、批量操作按钮和卸载操作都不可用。

### Requirement 2: 插件市场与安装

**User Story:** 作为 Berth 用户，我想浏览插件目录源并把插件装到指定泊位或 profile，这样能发现和安装新插件。

#### Acceptance Criteria

1. WHEN 用户打开插件市场或点击刷新，THE Plugin_Manager SHALL 从已配置的 Plugin_Catalog 地址异步拉取目录数据，拉取期间显示加载状态，且 Berth 主界面保持可交互。
2. THE Berth SHALL 允许用户在设置中修改 Plugin_Catalog 地址，默认值为 `https://dsh-plug.in/api/plugins.json`；只接受以 `http://` 或 `https://` 开头的地址，不符合时拒绝保存并保留原值；新地址在下一次打开插件市场或刷新时生效。
3. WHEN 目录数据下载完成，THE Catalog_Parser SHALL 将 JSON 解析为插件条目列表，每个条目包含包名（必需，非空字符串）、描述（可选，缺失时视为空字符串）以及 stable / beta / alpha 中至少一个 Release_Channel 的版本号。
4. IF 目录数据不是合法 JSON，或任一条目缺少包名、或没有任何 Release_Channel 版本号，THEN THE Catalog_Parser SHALL 返回包含出错位置或字段名的错误描述，Plugin_Manager 显示该错误并继续显示上一次成功加载的目录缓存。
5. THE Catalog_Printer SHALL 将插件条目列表序列化为与 Plugin_Catalog 格式一致的 JSON，用于本地缓存。
6. FOR ALL 合法插件条目列表，序列化后再解析 SHALL 得到等价的插件条目列表，等价指条目数量与顺序相同，且每个条目的包名、描述、各 Release_Channel 版本号均相同（往返性质）。
7. WHEN 用户在市场搜索框输入关键字，THE Plugin_Manager SHALL 在最后一次输入后 300ms 内只显示包名或描述中包含该关键字（不区分大小写）的条目；关键字为空时显示全部条目。
8. THE Plugin_Manager SHALL 提供 Release_Channel 选择器（stable / beta / alpha），默认 stable，并按所选渠道显示每个条目对应的版本号。
9. WHEN 用户选择一个市场条目并指定目标泊位或 profile 后点击安装，THE Plugin_Manager SHALL 在目标 profile 目录中安装所选渠道的版本，并将该插件加入启用配置；指定泊位时目标为该泊位的 (DSH_HOME, profile) 组合；未指定目标时安装按钮处于禁用状态。
10. WHEN 用户输入 npm 包名（可带 `@<版本号>` 或 `@<dist-tag>` 后缀）或 git URL 并点击安装，THE Plugin_Manager SHALL 按第 9 条的相同流程安装到目标 profile。
11. IF 用户输入的包名不符合 npm 包名规则（长度 1–214 字符、全小写、不以 `.` 或 `_` 开头、不含空格，可带 `@scope/` 前缀），并且也不是 `git+https://`、`https://`、`git@` 开头的 git URL，THEN THE Plugin_Manager SHALL 拒绝安装、不调用包管理器、保留输入框内容，并提示格式错误。
12. IF 安装过程失败，THEN THE Plugin_Manager SHALL 显示包管理器错误输出的最后最多 20 行作为错误摘要，并保持该 profile 的启用配置与 `package.json` 依赖列表与安装前一致。
13. WHILE 某个 profile 的安装进行中，THE Plugin_Manager SHALL 显示该安装的进度状态，并禁用同一 profile 上的安装、卸载、启用/禁用操作，其他 profile 的操作不受影响。
14. IF 目录数据拉取在 15 秒内未完成或因网络错误失败，THEN THE Plugin_Manager SHALL 显示指示失败原因的错误信息和重试入口，并显示上一次成功加载的目录缓存；无缓存时显示空列表。
15. IF 所选 Release_Channel 下某条目没有版本号，THEN THE Plugin_Manager SHALL 在该条目上显示该渠道无可用版本，并禁用该条目的安装按钮。
16. WHEN 安装成功完成，THE Plugin_Manager SHALL 显示成功提示并刷新目标 profile 的插件列表；若目标 profile 被处于 Active_State 的泊位使用，THE Plugin_Manager SHALL 触发需求 4 的 Restart_Hint。

### Requirement 3: 启动前插件自检与隔离

**User Story:** 作为 Berth 用户，我想在泊位启动前自动检查插件，这样坏插件不会让整个服务起不来。

#### Acceptance Criteria

1. WHEN 一个泊位即将启动（包括手动启动、重启、Batch_Launcher 批量或错峰启动、Auto_Restarter 自动重启），THE Preflight_Checker SHALL 在启动 dsh 进程前，对该泊位 profile 中每个启用的插件检查两项条件：（a）该插件包存在于该 profile 的 `node_modules` 中；（b）该插件包 `dependencies` 中声明的每个依赖都能在 `node_modules` 中找到版本满足声明范围的包；两项均满足才判定通过。
2. WHERE 设置中"启动前自检"开关处于关闭状态（默认开启），THE Supervisor SHALL 不调用 Preflight_Checker，直接以 profile 当前的启用插件启动泊位，且不产生任何修复或 Quarantine 操作。
3. IF Preflight_Checker 发现一个或多个插件未通过第 1 条的检查，且包管理器可用，THEN THE Preflight_Checker SHALL 在该 profile 目录执行一次依赖安装（每次启动最多 1 次，超时上限 300 秒），结束后对全部启用插件重新检查；安装以非零退出码结束或超时被终止时，视为修复失败并进入第 4 条处理。
4. IF 修复后（或修复失败后）某插件仍未通过检查，THEN THE Preflight_Checker SHALL 对该插件执行 Quarantine，将其从 profile 启用列表中移除并持久化记录插件名、隔离时间与失败原因（包缺失、具体不可解析的依赖名、安装失败或安装超时之一），该记录在 Berth 重启后仍保留；通过检查的插件保持启用状态不变。
5. WHEN Preflight_Checker 完成检查、修复与隔离，THE Supervisor SHALL 以剩余启用插件启动泊位；全部插件均被 Quarantine 时仍以零个插件启动。
6. WHEN 本次启动中有 1 个及以上插件被 Quarantine，THE Berth SHALL 在该泊位详情中列出每个被隔离插件的插件名与失败原因（持续显示直到解除隔离），并通过 Notifier 发送 1 条汇总系统通知。
7. THE Plugin_Manager SHALL 在插件列表中为每个处于 Quarantine 的插件显示"已隔离"标记及其失败原因，并提供"解除隔离"操作。
8. THE Preflight_Checker SHALL 将每个插件的检查结果（通过/未通过及原因）、修复的开始与结束（含退出码或超时）、每次 Quarantine 各写入一条带时间戳的记录到该泊位日志。
9. IF 修复所需的包管理器不可用（可执行文件无法定位或无法启动），THEN THE Preflight_Checker SHALL 跳过修复，直接对未通过检查的插件执行 Quarantine，并在失败原因中注明包管理器缺失。
10. WHEN 用户对某个已隔离插件执行"解除隔离"，THE Plugin_Manager SHALL 删除该插件的隔离记录并将其恢复为启用状态，目标 profile 被处于 Active_State 的泊位使用时触发需求 4 的 Restart_Hint；该插件在下一次泊位启动时重新接受检查。
11. WHILE 某个 profile 正在被 Preflight_Checker 执行依赖安装，THE Preflight_Checker SHALL 让使用同一 profile 的其他泊位的自检等待该安装结束后再开始，同一 profile 同一时刻最多运行 1 个依赖安装进程。
12. IF Preflight_Checker 无法读取该 profile 的 `package.json`（文件不存在或无法解析），THEN THE Supervisor SHALL 中止本次启动、不启动 dsh 进程、将泊位状态置为"已停止"，并在泊位详情与泊位日志中显示 profile 配置无法读取的错误，且不对任何插件执行 Quarantine。

### Requirement 4: 插件变动后的重启提示

**User Story:** 作为 Berth 用户，我想在插件变动后被提示重启相关泊位，这样变更能生效。

#### Acceptance Criteria

1. WHEN 某个 profile 的插件安装、卸载、启用、禁用或解除隔离操作成功完成（批量操作以整批结束为准），THE Berth SHALL 找出同时满足以下条件的泊位作为受影响泊位列表：DSH_HOME 路径与该 profile 所属 DSH_HOME 相同（规范化后不区分大小写比较）、profile 名相同、状态为"启动中"或"运行中"。
2. WHEN 受影响泊位列表非空，THE Berth SHALL 在插件操作完成后 1 秒内显示 Restart_Hint，按泊位列表顺序列出全部受影响泊位名，并提供"全部重启"和"稍后"两个操作；IF 列表为空，THEN THE Berth SHALL 不显示 Restart_Hint。
3. WHEN 用户点击"全部重启"，THE Supervisor SHALL 按 Restart_Hint 中的顺序逐个重启泊位，前一个进入"运行中"或重启失败后才开始下一个，并跳过此时已不处于"启动中"或"运行中"的泊位；Restart_Hint 随之关闭。
4. WHEN 用户点击"稍后"或关闭 Restart_Hint，THE Berth SHALL 在列表中每个泊位的卡片上显示"待重启"标记，并在该泊位下一次进入"运行中"时移除；泊位在此之前被停止时标记保留。
5. IF "全部重启"过程中某个泊位重启失败，THEN THE Supervisor SHALL 继续重启剩余泊位，结束后显示成功数、失败数及每个失败泊位名与原因；失败泊位显示"待重启"标记。
6. WHILE Restart_Hint 正在显示，WHEN 又有插件变动产生新的受影响泊位，THE Berth SHALL 将新泊位合并到当前 Restart_Hint 的列表中（同一泊位只列一次），不另外弹出第二个 Restart_Hint。
7. IF 插件变动操作失败或被取消，THEN THE Berth SHALL 不显示 Restart_Hint，并保持各泊位现有的"待重启"标记不变。

### Requirement 5: 泊位环境变量与额外启动参数

**User Story:** 作为 Berth 用户，我想给每个泊位单独配置环境变量和启动参数，这样能为不同泊位定制运行方式。

#### Acceptance Criteria

1. THE Berth SHALL 在泊位编辑界面提供环境变量表与额外启动参数列表：环境变量表每行包含键、值与"敏感"标记，支持增删改，最多 100 行；额外启动参数列表每项是一个独立参数，支持增删改与调整顺序，最多 50 项。
2. WHEN 泊位启动，THE Supervisor SHALL 以 Berth 进程的系统环境为基础写入该泊位环境变量表中的每一行；键名不区分大小写比较，与系统环境同名时以泊位配置为准，空字符串值照常写入；配置修改在下一次启动时生效。
3. WHEN 泊位启动，THE Supervisor SHALL 把额外启动参数按列表顺序追加到 Berth 生成的参数之后，每项作为一个完整参数传给 dsh，不按空格拆分，也不做 shell 展开。
4. THE Supervisor SHALL 始终以 Berth 计算出的 DSH_HOME 覆盖用户在环境变量表中为 `DSH_HOME`（键名不区分大小写）配置的值。
5. WHEN 用户在环境变量表中输入的键不区分大小写等于 `DSH_HOME`，THE Berth SHALL 在该行旁提示该键由 Berth 注入、用户值不会生效；该提示不阻止保存。
6. IF 环境变量键为空、含 `=`、含空白字符或长度超过 256 个字符，THEN THE Berth SHALL 拒绝保存泊位配置，标出非法行并提示键名非法，保留编辑界面中已输入的全部内容，`instances.json` 保持不变。
7. IF 环境变量表中存在两行及以上的键不区分大小写相同，THEN THE Berth SHALL 拒绝保存泊位配置，标出重复行并提示键名重复，`instances.json` 保持不变。
8. IF 额外启动参数中有一项等于 `--port` 或 `--profile`，或以 `--port=`、`--profile=` 开头，THEN THE Berth SHALL 在保存时提示参数冲突并列出冲突项，同时允许保存；启动时 THE Supervisor SHALL 不传递这些冲突项，`--port` 或 `--profile` 单独成项时紧随其后的一项也不传递。
9. THE Berth SHALL 将环境变量表（键、值、敏感标记，保持行顺序）与额外启动参数列表（保持顺序）持久化到 `instances.json` 中该泊位的新增字段；读取缺少这些字段的旧版文件时视为两者均为空，并正常加载其余字段。
10. FOR ALL 通过校验的泊位配置，写入 `instances.json` 后再读取 SHALL 得到等价的环境变量表与额外启动参数列表，即行数、顺序、键、值、敏感标记与参数内容全部一致（往返性质）。
11. WHERE 某行环境变量标记为"敏感"，THE Berth SHALL 在泊位编辑界面与泊位详情中以掩码显示该值（不含原值中的任何字符），并在 Berth 写出的日志与诊断报告中省略该值，只保留键名。

### Requirement 6: 多版本 dsh 共存

**User Story:** 作为 Berth 用户，我想安装多个 dsh 版本并让每个泊位绑定其中一个，这样能对比或回退版本。

#### Acceptance Criteria

1. WHEN 用户在版本管理界面选择一个 dsh 版本（符合 SemVer 2.0 的完整版本号）并点击安装，THE Berth SHALL 把该版本安装到 Dsh_Version_Store 下目录名等于该版本号的独立目录；安装过程中 System_Dsh 和其他版本目录的内容保持不变。
2. THE Berth SHALL 在版本管理界面按 SemVer 2.0 优先级从高到低列出所有已安装完成的版本，每项显示版本号、安装目录绝对路径与引用计数（`instances.json` 中绑定该版本的泊位数量，每次泊位绑定保存或泊位删除后重新计算）。
3. THE Berth SHALL 在泊位编辑界面提供 dsh 版本选择器，选项为"系统 dsh"加上全部已安装版本，新建泊位默认"系统 dsh"；泊位已保存的绑定版本不在 Dsh_Version_Store 中时，选择器仍显示该版本号并标记为"缺失"。
4. WHEN 泊位启动，THE Supervisor SHALL 用该泊位绑定版本目录中的 dsh 可执行文件启动 dsh 进程；绑定为"系统 dsh"时使用 System_Dsh。
5. IF 泊位启动时绑定的版本目录不存在或目录中找不到 dsh 可执行文件，THEN THE Supervisor SHALL 不创建 dsh 进程，让泊位保持"已停止"状态，并显示包含版本号与缺失原因的错误提示。
6. IF 用户删除一个引用计数 ≥1 的版本，THEN THE Berth SHALL 拒绝删除，列出所有引用该版本的泊位名称，且该版本目录保持不变。
7. WHEN 用户删除一个引用计数为 0 的版本并确认，THE Berth SHALL 删除该版本目录，并在 2 秒内把该版本从列表和选择器中移除；其他版本目录与 System_Dsh 保持不变。
8. IF 所选版本已存在于 Dsh_Version_Store 中，THEN THE Berth SHALL 拒绝安装并提示该版本已安装。
9. IF 版本安装失败（下载失败、包管理器返回非零退出码、安装后找不到 dsh 可执行文件），THEN THE Berth SHALL 删除本次生成的不完整目录，显示包管理器错误摘要，且已安装版本列表与安装前一致。
10. WHILE 某个版本正在安装，THE Berth SHALL 显示该版本的"安装中"状态，并禁用再次安装同一版本号与删除该版本两项操作。

### Requirement 7: 泊位/Profile 整包导出与导入

**User Story:** 作为 Berth 用户，我想把泊位或 profile 打包导出并在别处导入，这样能迁移或分享配置。

#### Acceptance Criteria

1. WHEN 用户对一个泊位或 profile 执行导出并选定保存位置，THE Bundle_Exporter SHALL 生成单个 Bundle 文件，包含清单（格式版本、导出时间、Berth 版本、Bundle 类型取值"泊位"或"profile"）、profile 目录下的配置文件，以及插件清单（每个插件的包名、版本、启用状态，含 Core_Package）。
2. WHEN 导出对象是泊位，THE Bundle_Exporter SHALL 额外写入该泊位的配置（端口、profile 名、workspace、dsh 版本绑定、Launch_Env）；导出对象是 profile 时不包含泊位配置。
3. THE Bundle_Exporter SHALL 排除 profile 目录下任意层级的 `node_modules` 目录、扩展名为 `.log` 的文件和泊位日志文件。
4. WHEN 被导出的泊位含有标记为"敏感"的环境变量，THE Bundle_Exporter SHALL 显示"包含敏感值"勾选项（默认不勾选）；不勾选时只保留键名与敏感标记，勾选时写入原值。
5. IF 导出过程中读取源文件或写入目标文件失败，THEN THE Bundle_Exporter SHALL 中止导出、提示失败原因，并不在目标位置留下不完整的 Bundle 文件。
6. WHEN 用户选择一个 Bundle 文件与目标 DSH_HOME 并确认导入，THE Bundle_Importer SHALL 先校验清单格式版本与文件结构，通过后在目标 DSH_HOME 下创建 profile、写入配置文件、按插件清单安装依赖并恢复启用状态；Bundle 类型为"泊位"时还在 `instances.json` 中新增一个泊位。
7. IF 目标 DSH_HOME 下已存在同名 profile，或 Bundle 中的泊位端口已被现有泊位使用，THEN THE Bundle_Importer SHALL 在写入任何数据前提示冲突，并提供"重命名 profile"（新名称不得重名）、"自动分配端口"（未被任何泊位使用且 Port_Checker 检测为未占用）与"取消"；选择"取消"时现有数据保持不变。
8. IF Bundle 清单格式版本高于当前 Berth 支持的最高版本，或文件无法解析、缺少清单或插件清单，THEN THE Bundle_Importer SHALL 拒绝导入并提示具体原因，且不创建或修改任何 profile、泊位或 `instances.json` 条目。
9. IF 导入的泊位 Bundle 中存在只保留键名的敏感环境变量，THEN THE Bundle_Importer SHALL 以空值和"敏感"标记导入这些变量，并在导入完成后列出需要用户补填的键名。
10. IF 依赖重建失败，THEN THE Bundle_Importer SHALL 保留已写入的配置文件与泊位条目，将该泊位（profile 类型则为该 profile）标记为"依赖未就绪"，显示包管理器错误摘要并提供"重试"；重试成功后清除该标记。
11. FOR ALL 合法 Bundle，在使用相同"包含敏感值"选项的前提下，导入后再导出 SHALL 产生与原 Bundle 等价的清单（导出时间与 Berth 版本除外）、配置文件、泊位配置与插件清单（往返性质）；导入时因冲突改过的 profile 名或端口不参与比较。

### Requirement 8: Skills 与 MCP 管理

**User Story:** 作为 Berth 用户，我想查看并启用/禁用 profile 的 Skills 和 MCP 服务，这样能在 Berth 中统一管理扩展能力。

> 依赖调研：Skill 与 MCP_Server 的存储位置、启用标记方式由设计阶段根据 dsh 源码确定。

#### Acceptance Criteria

1. WHEN 用户打开某个 (DSH_HOME, profile) 的扩展页，THE Extension_Manager SHALL 在 2 秒内分两个列表显示：Skill 列表（名称、来源、启用状态）与 MCP_Server 列表（名称、启动命令或地址、启用状态），均按名称升序排列，且条目与配置中的定义一一对应、无遗漏无重复。
2. WHEN 用户切换某个 Skill 或 MCP_Server 的启用开关，THE Extension_Manager SHALL 按 dsh 的配置格式只修改该条目的启用标记并写回，其他条目与字段保持不变；写回后重新读取，列表中该条目状态与开关一致。
3. IF 配置文件无法解析，THEN THE Extension_Manager SHALL 显示指明文件路径与失败原因的错误信息，禁用该页全部写操作，并保持该文件不变；用户修正文件后重新打开扩展页恢复正常。
4. WHEN 启用状态变更成功，THE Berth SHALL 按需求 4 的规则找出受影响泊位，列表非空时显示 Restart_Hint，为空时不显示。
5. FOR ALL 合法配置文件，Extension_Manager 读取后不做修改直接写回 SHALL 产生与原文件语义等价的内容；对同一条目禁用再启用 SHALL 使配置恢复为语义等价的内容（往返性质）。
6. IF 写回配置文件失败（只读、被占用或磁盘错误），THEN THE Extension_Manager SHALL 显示失败原因、将开关恢复为操作前状态、保持文件不变，并不触发 Restart_Hint。
7. IF 配置文件在扩展页打开后被外部程序修改，THEN THE Extension_Manager SHALL 在下一次写回前检测到该变化，放弃本次写回，提示配置已被外部修改，并重新读取刷新列表。
8. IF 该 profile 中不存在 Skill 或 MCP_Server 定义（包括配置文件不存在），THEN THE Extension_Manager SHALL 在对应列表显示"无条目"空状态，且不创建或修改任何配置文件。

### Requirement 9: 泊位图标、分组、标签与备注

**User Story:** 作为 Berth 用户，我想给泊位设置图标、分组、标签和备注，这样泊位多时也容易区分。

#### Acceptance Criteria

1. THE Berth SHALL 在泊位编辑界面提供：图标选择（内置图标集中一项、本地图片文件或"无图标"）、分组名（单行，最长 32 字符）、标签列表（最多 10 个，每个最长 20 字符）与多行备注（最长 2000 字符）。
2. WHEN 用户选择一个通过校验的本地图片作为图标并保存泊位，THE Berth SHALL 将图片复制到 Berth 数据目录，并在泊位配置中保存相对于数据目录的路径；之后删除或移动原图片不影响显示。
3. IF 所选图片扩展名不是 PNG、JPG、JPEG、ICO 或 SVG（不区分大小写），或大于 1,048,576 字节，或无法读取，THEN THE Berth SHALL 拒绝设置、提示具体原因，并保留原有图标设置。
4. THE Berth SHALL 在泊位列表中按分组名显示可折叠分组，标题显示分组名与泊位数量；分组名为空或仅含空白的泊位归入"未分组"。
5. WHEN 用户在泊位列表的搜索框中输入关键字，THE Berth SHALL 在 300ms 内只显示名称或任一标签包含该关键字（不区分大小写，忽略首尾空白）的泊位，并隐藏过滤后为空的分组；关键字清空时恢复显示全部。
6. IF 用户输入的分组名、标签或备注超过第 1 条的长度上限，或添加为空或与已有标签重复（不区分大小写）的标签，THEN THE Berth SHALL 拒绝该输入并提示原因，保留其他字段不变。
7. IF 泊位配置中保存的图标文件在加载时不存在或无法解析，THEN THE Berth SHALL 以默认图标显示该泊位，且不修改已保存配置。
8. THE Berth SHALL 将图标、分组、标签、备注持久化到 `instances.json` 的新增字段；FOR ALL 泊位配置，写入后再读取 SHALL 得到等价的图标、分组、标签（含顺序）与备注（往返性质）；不含这些字段的旧版文件读取后视为无图标、未分组、无标签、无备注。

### Requirement 10: 在外部工具中打开 workspace

**User Story:** 作为 Berth 用户，我想一键在 VS Code、Cursor 或资源管理器中打开泊位的 workspace，这样能快速进入项目。

#### Acceptance Criteria

1. THE Berth SHALL 在泊位详情提供"在 VS Code 中打开"、"在 Cursor 中打开"、"在资源管理器中打开"三个操作，对任意状态的泊位均显示。
2. WHEN 用户打开某个泊位的详情，THE Berth SHALL 重新检测本机 VS Code 与 Cursor 可执行文件是否存在，并重新校验 workspace 路径，用结果决定三个操作的启用状态。
3. WHEN 用户点击处于启用状态的一个操作，THE Berth SHALL 以 workspace 目录完整路径为目标启动对应程序（VS Code/Cursor 以该目录作为打开的文件夹，资源管理器显示该目录），路径含空格或非 ASCII 字符时同样有效，且不等待外部程序退出。
4. IF 未检测到 VS Code 或 Cursor 的可执行文件，THEN THE Berth SHALL 禁用对应操作，并在悬停提示中说明未检测到该程序；"在资源管理器中打开"不受此检测影响。
5. IF 泊位未配置 workspace（为空或只含空白）、路径不存在或不是目录，THEN THE Berth SHALL 禁用三个操作，并在悬停提示中说明具体原因。
6. IF 点击后对应程序启动失败，THEN THE Berth SHALL 显示说明哪个程序启动失败的错误提示，并保持泊位配置与运行状态不变。
7. WHEN 用户保存修改后的 workspace 字段，THE Berth SHALL 按第 5 条重新校验，并立即更新三个操作的启用状态与悬停提示。

---

## B. 稳定性

### Requirement 11: 崩溃自动重启

**User Story:** 作为 Berth 用户，我想让崩溃的泊位自动重启，这样短暂故障不需要人工干预。

#### Acceptance Criteria

1. WHERE 泊位的"崩溃自动重启"开关开启（按泊位独立保存，新建泊位默认关闭），WHEN 该泊位发生 Crash 且其连续重启次数 n 小于上限 N，THE Auto_Restarter SHALL 将 n 加 1，并在第 n 次对应的退避间隔结束后重启该泊位，实际等待与计算值偏差不超过 1 秒。
2. THE Auto_Restarter SHALL 使用指数退避间隔：第 n 次连续重启前等待 min(2^(n-1) × 基础间隔, 最大间隔)；基础间隔默认 2 秒（可配置 1–60 秒），最大间隔默认 60 秒（可配置为不小于基础间隔且不超过 600 秒）。
3. WHEN 泊位发生 Crash 且 n 已等于上限 N（默认 5，可配置 1–20），THE Auto_Restarter SHALL 不再安排重启，将泊位状态标记为"崩溃已停止"，并通过 Notifier 发送包含泊位名与已重启次数的通知。
4. WHEN 泊位自最近一次进入"运行中"起连续保持"运行中"达到 120 秒，THE Auto_Restarter SHALL 将 n 清零。
5. WHEN 用户在退避等待期间手动停止该泊位或关闭其"崩溃自动重启"开关，THE Auto_Restarter SHALL 在 1 秒内取消尚未执行的重启计划、将 n 清零并移除倒计时显示。
6. WHILE 泊位处于退避等待中，THE Berth SHALL 在泊位卡片上显示"将在 X 秒后重启（第 n/N 次）"，X 为剩余秒数向上取整、每 1 秒刷新，重启开始时移除。
7. THE Auto_Restarter SHALL 保证对任意连续失败序列，第 1 至第 N 次计算出的等待间隔单调不减且不超过最大间隔（不变量）。
8. IF 自动重启时泊位进程未能成功启动（启动命令失败，或在进入"运行中"前退出），THEN THE Auto_Restarter SHALL 将该次视为一次 Crash，按第 1 条与第 3 条继续处理。
9. WHEN 用户手动启动处于"崩溃已停止"或"已停止"状态的泊位，THE Auto_Restarter SHALL 先将 n 清零再执行启动。
10. WHILE 泊位处于"运行中（外部）"，THE Auto_Restarter SHALL 不对该泊位安排任何自动重启。

### Requirement 12: 启动前端口冲突检查与复用

**User Story:** 作为 Berth 用户，我想在启动前知道端口是否被占用及被谁占用，这样能避免启动失败或选择复用现有服务。

#### Acceptance Criteria

1. WHEN 泊位启动（包括手动启动与重启），THE Port_Checker SHALL 在启动 dsh 进程前于 1 秒内完成该泊位端口在 127.0.0.1 上是否处于监听状态的检查；未被监听时 THE Supervisor SHALL 继续正常启动且不显示冲突对话框。
2. IF 端口已被占用，THEN THE Port_Checker SHALL 获取占用进程的 PID、进程名与可执行文件路径，并在冲突对话框中连同端口号显示；无法获取的字段显示为"未知"，且不启动 dsh 进程。
3. IF 占用端口的服务在 3 秒内对 dsh 服务特征探测返回符合特征的响应，THEN THE Berth SHALL 在冲突对话框中提供"复用现有服务"选项；否则不显示该选项。
4. WHEN 用户选择"复用现有服务"，THE Berth SHALL 不启动新 dsh 进程，将泊位状态标记为"运行中（外部）"，记录占用进程 PID，启用"打开界面"，且不将该进程纳入 Job Object。
5. WHILE 泊位处于"运行中（外部）"，WHEN 用户点击停止，THE Berth SHALL 解除关联、将泊位状态置为"已停止"、提示外部进程未被结束，外部进程保持运行。
6. THE Berth SHALL 在冲突对话框中始终提供"取消启动"与"改用空闲端口"；WHEN 用户选择"取消启动"，THE Berth SHALL 关闭对话框、不启动 dsh 进程、恢复泊位启动前状态且配置不变。
7. IF 保存泊位配置时其端口与另一泊位已配置的端口相同，THEN THE Berth SHALL 阻止保存、提示端口重复并列出使用该端口的泊位名，保留表单内容。
8. WHEN 用户选择"改用空闲端口"，THE Port_Checker SHALL 从当前端口 +1 起逐一检查至 65535，选取第一个未被监听且未被其他泊位配置的端口，THE Berth SHALL 将其保存到泊位配置并以新端口启动。
9. IF 在当前端口 +1 至 65535 范围内找不到满足条件的端口，THEN THE Berth SHALL 显示无可用空闲端口的错误、不启动 dsh 进程，端口保持原值。
10. IF 泊位处于"运行中（外部）"时所记录 PID 的进程退出或端口不再被监听，THEN THE Berth SHALL 在 5 秒内将泊位置为"已停止"并解除关联，且不计为 Crash。

### Requirement 13: 批量启停与错峰自启

**User Story:** 作为 Berth 用户，我想批量启动/停止泊位，并让开机自启的泊位错开启动，这样不会同时抢占资源。

#### Acceptance Criteria

1. THE Batch_Launcher SHALL 提供"启动全部"、"停止全部"、"启动所选"、"停止所选"、"启动分组"、"停止分组"六个操作，目标集合分别为全部泊位、当前选中的泊位、需求 9 定义的某一分组（含"未分组"）下的全部泊位。
2. IF 某个批量操作的目标集合为空，THEN THE Berth SHALL 禁用该操作入口，Batch_Launcher 不执行任何启停。
3. WHEN 执行批量启动，THE Batch_Launcher SHALL 按泊位列表当前显示顺序逐个发起启动，相邻两次发起的时间差等于错峰间隔（默认 3 秒，可配置 0–60 整数秒，误差 ±500ms），不等待上一个进入"运行中"。
4. WHEN 执行批量启动，THE Batch_Launcher SHALL 跳过已处于 Active_State 的目标泊位，被跳过的不占用错峰间隔并计入已完成数。
5. WHEN Berth 完成主窗口初始化，THE Batch_Launcher SHALL 按相同顺序和错峰间隔依次启动所有 `autostart` 为真的泊位；没有此类泊位时不发起批量操作也不显示进度。
6. WHEN 执行批量停止，THE Batch_Launcher SHALL 同时对全部处于 Active_State 的目标泊位发起停止，其余目标直接计入已完成数。
7. WHILE 批量操作进行中，THE Berth SHALL 显示"已完成数/总数"进度与取消操作，并禁用其他批量操作入口；泊位达到"运行中"或被判定启动失败，或停止后进程已退出，即视为已完成。
8. WHEN 用户在批量操作进行中点击取消，THE Batch_Launcher SHALL 不再对尚未发起的泊位发起启停并保持其状态不变，已发起的继续完成；结束汇总中列出因取消而未处理的泊位名。
9. IF 批量启动中某个泊位被判定启动失败（包括在达到"运行中"前退出，以及因绑定版本缺失、端口冲突等被拒绝启动），THEN THE Batch_Launcher SHALL 按错峰间隔继续启动剩余泊位，结束时显示成功数、失败数及每个失败泊位名与原因。

---

## C. 更新与环境

### Requirement 14: dsh 与插件更新面板

**User Story:** 作为 Berth 用户，我想在一个面板里看到并执行 dsh 与插件的更新，这样维护版本更省事。

#### Acceptance Criteria

1. THE Update_Center SHALL 分两栏显示 dsh（System_Dsh 与 Dsh_Version_Store 中每个版本各一项）与插件（按 (DSH_HOME, profile) 分组，每个已安装插件一项）；每项显示当前版本、最新版本与最近一次成功检查时间，未检查的项显示"未检查"，有更新的项显示"有更新"标记与升级按钮。
2. WHEN Berth 主窗口显示完成，THE Update_Center SHALL 在后台执行一次全部检查且主窗口保持可操作；之后按检查间隔（默认 24 小时，可配置 1–168 整数小时或"关闭"）定期检查。
3. THE Update_Center SHALL 通过 npm registry 获取 `@deepseek-ai/dsh` 的 `latest` 版本作为 dsh 最新版本，并按插件当前 Release_Channel（未记录时视为 stable）获取插件最新版本。
4. WHEN 用户对 System_Dsh 执行升级，THE Update_Center SHALL 复用现有 DshUpdate 的 npm 升级流程；WHEN 用户对 Dsh_Version_Store 中某版本执行升级，THE Update_Center SHALL 按需求 6 将最新版本安装为新的独立版本目录，原版本目录与泊位绑定保持不变。
5. WHEN 用户执行插件升级，THE Update_Center SHALL 将插件升级到其当前 Release_Channel 的最新版本，成功后触发需求 4 的 Restart_Hint。
6. WHEN 用户点击"一键升级"，THE Update_Center SHALL 仅对标记"有更新"的项按先 dsh 后插件的顺序逐项升级，结束后显示成功数、失败数与失败项名称；WHILE 没有任何项标记"有更新"，THE Update_Center SHALL 禁用"一键升级"。
7. IF 某一项检查失败（网络错误、单次请求超过 30 秒、响应无法解析）或升级失败，THEN THE Update_Center SHALL 在该项旁显示失败原因、保持当前版本不变，并继续处理其余项。
8. FOR ALL 版本号对，THE Update_Center 的版本比较 SHALL 遵循 SemVer 2.0 优先级规则（预发布版本低于同号正式版本），比较前忽略开头的单个 "v" 或 "V"。
9. IF 某一项的当前或最新版本不是合法 SemVer 2.0 版本号，THEN THE Update_Center SHALL 显示"版本无法比较"，且不标记"有更新"。
10. WHILE 某一项正在检查或升级，THE Update_Center SHALL 显示进行中状态并禁用该项升级按钮与"一键升级"；WHILE 全部检查正在进行，THE Update_Center SHALL 不启动新的全部检查。

### Requirement 15: 环境检测与诊断报告

**User Story:** 作为 Berth 用户，我想一眼看到运行 dsh 所需的工具是否齐全，这样能快速排查环境问题。

#### Acceptance Criteria

1. WHEN 用户打开环境页，或 Berth 启动后主窗口第一次显示，THE Env_Checker SHALL 在后台异步检测 Node、npm、pnpm、Git、dsh（System_Dsh）、WebView2 Runtime 共 6 项的可用性、版本号与可执行文件路径；检测期间界面可操作，未返回的项显示"检测中"。
2. THE Env_Checker SHALL 为每项只显示一种状态："可用"（找到且版本不低于最低要求或无要求）、"缺失"（未找到）、"版本过低"（找到但低于最低要求）；最低版本在设计阶段根据 dsh 的 `engines` 字段确定；"可用"与"版本过低"的项同时显示版本号与路径。
3. IF 某项为"缺失"或"版本过低"，THEN THE Env_Checker SHALL 显示该工具的官方下载链接与安装指引，"版本过低"还显示当前与最低版本；点击链接时由系统默认浏览器打开。
4. WHERE 本机可以执行 winget 并返回版本号，WHEN 用户点击某个"缺失"项的"一键安装"，THE Env_Checker SHALL 先显示列出完整命令的确认对话框，确认后才执行，取消则不执行；命令结束后只重新检测该项。
5. WHEN 用户点击"导出诊断报告"并选好保存位置，THE Env_Checker SHALL 生成文本文件，包含生成时间、Berth 版本、Windows 版本、6 项检测结果、代理设置（凭据已脱敏）、泊位列表摘要（名称、端口、状态、最近退出码，从未退出注明"无"）；尚未检测过时先完成一次检测。
6. THE Env_Checker SHALL 在诊断报告中把 token、密码、代理 URL 中的用户名与密码，以及按需求 5 标记为"敏感"的环境变量值替换为 `***`，报告全文不出现任何上述原始值。
7. IF 某项检测超过 10 秒未返回或调用的进程异常退出，THEN THE Env_Checker SHALL 将该项标记为"检测失败"并显示原因，其余项继续检测。
8. IF "一键安装"的 winget 命令退出码不为 0，THEN THE Env_Checker SHALL 显示退出码和输出的最后 20 行，并在重新检测后按实际结果显示该项状态。
9. IF 诊断报告无法写入所选位置，THEN THE Env_Checker SHALL 显示失败原因，不留下不完整的报告文件，检测结果保持不变。

### Requirement 16: 代理设置与自动探测

**User Story:** 作为 Berth 用户，我想统一设置代理并让 Berth 自动探测，这样 npm、git 与更新请求在受限网络下也能工作。

#### Acceptance Criteria

1. THE Proxy_Manager SHALL 提供三种互斥的代理模式："不使用代理"（默认）、"系统代理"、"手动"；"手动"包含协议（HTTP、HTTPS、SOCKS5）、主机、端口（1–65535 整数）及可选用户名与密码。
2. WHEN 用户点击"自动探测"，THE Proxy_Manager SHALL 读取 Windows 系统代理设置，并对 127.0.0.1 上的端口 7890、7897、1080、10808、10809、8080 逐一发起 TCP 连接探测（每个端口超时不超过 1 秒，整次在 10 秒内结束），列出已启用的系统代理与可连通端口作为候选（显示主机、端口与来源）；用户选中并确认前已保存设置保持不变。
3. WHILE 代理模式不是"不使用代理"，THE Proxy_Manager SHALL 将代理应用到 Berth 发起的网络请求（npm registry 查询、插件目录拉取），并通过 HTTP_PROXY、HTTPS_PROXY 及其小写形式传给 Berth 启动的 npm、pnpm、git 子进程；保存后仅对新请求与新子进程生效。
4. THE Proxy_Manager SHALL 提供 npm 镜像设置，可选官方源（默认）、预置镜像之一或以 `http://`/`https://` 开头的自定义地址，所选源作为 registry 用于 Berth 发起的 npm 请求与启动的 npm、pnpm 子进程。
5. WHERE "镜像兜底"开启，IF 通过官方源的 npm 请求失败（连接失败、超过 30 秒无响应、错误响应或子进程非零退出），THEN THE Proxy_Manager SHALL 使用所配置镜像重试且只重试一次；重试仍失败时报告失败并注明两者都已尝试。
6. WHEN 用户点击"测试连接"，THE Proxy_Manager SHALL 用界面当前填写的设置（含未保存修改）分别访问 npm registry 与 GitHub API（每个超时 10 秒），显示结果、毫秒耗时及失败原因类别（超时、连接被拒绝、代理认证失败、其他）；测试不修改已保存设置。
7. THE Proxy_Manager SHALL 将代理密码用 Windows DPAPI 加密后存储，明文不出现在设置文件、日志与 Diagnostic_Report 中；界面以固定长度掩码显示；解密失败时清空已存凭据并提示重新输入，其他设置保持不变。
8. WHERE "泊位继承代理"开启且代理模式不是"不使用代理"，THE Supervisor SHALL 在泊位启动时注入代理环境变量，泊位自身环境变量表中的同名键优先；该开关关闭时不注入任何代理环境变量。
9. IF 用户在"手动"模式下保存时主机为空或端口不是 1–65535 的整数，THEN THE Proxy_Manager SHALL 拒绝保存、提示非法字段，并保留上一次已保存的设置。
10. IF 自动探测结束时系统代理未启用且所有探测端口都不可连通，THEN THE Proxy_Manager SHALL 提示未发现可用代理，已保存设置保持不变。

---

## D. 托盘与体验

### Requirement 17: 托盘泊位菜单与独立托盘图标

**User Story:** 作为 Berth 用户，我想在托盘菜单直接操作泊位，这样不用打开主窗口也能启停。

#### Acceptance Criteria

1. WHEN 用户打开托盘菜单，THE Tray SHALL 为每个泊位列出一个子菜单，顺序与主窗口泊位列表一致，标题格式为"泊位名 — 当前状态"，状态文字与泊位卡片一致。
2. THE Tray SHALL 在每个泊位子菜单中按顺序提供"启动"、"停止"、"重启"、"打开界面"、"在浏览器中打开"，可用规则为："启动"仅在不处于 Active_State 且不处于"运行中（外部）"时可用；"停止"仅在"启动中"、"运行中"或"运行中（外部）"时可用；"重启"仅在"运行中"时可用；"打开界面"与"在浏览器中打开"仅在"运行中"或"运行中（外部）"时可用；"停止中"时五项全部禁用。
3. WHEN 用户点击泊位子菜单中的某个可用项，THE Berth SHALL 执行与主窗口同名操作相同的行为（含 Port_Checker 冲突处理与失败提示），不要求主窗口可见。
4. WHEN 泊位状态变化，THE Tray SHALL 在 1 秒内按第 1、2 条更新该子菜单的标题与可用状态，包括菜单处于打开状态时。
5. THE Tray SHALL 在泊位子菜单下方提供"启动全部"与"停止全部"，分别调用 Batch_Launcher 的对应操作；WHILE 批量操作进行中，THE Tray SHALL 禁用这两项。
6. IF 没有任何泊位，THEN THE Tray SHALL 显示一条禁用的"暂无泊位"占位项，并禁用"启动全部"与"停止全部"。
7. WHERE "为运行中泊位显示独立托盘图标"开启（默认关闭），WHILE 泊位处于"运行中"（不含"运行中（外部）"），THE Tray SHALL 在 1 秒内为该泊位显示且仅显示一个独立托盘图标，使用泊位自定义图标（缺失时用 Berth 默认图标），悬停提示包含泊位名与端口。
8. WHEN 用户左键单击泊位独立托盘图标，THE Berth SHALL 打开该泊位界面；IF 该界面已打开，THEN THE Berth SHALL 将其还原并置于前台，不另开新窗口。
9. WHEN 泊位离开"运行中"状态，THE Tray SHALL 在 1 秒内移除该泊位的独立托盘图标。
10. WHEN 用户关闭"为运行中泊位显示独立托盘图标"，THE Tray SHALL 在 1 秒内移除所有独立托盘图标；WHEN 用户开启该设置，THE Tray SHALL 在 1 秒内为所有"运行中"泊位显示独立托盘图标。
11. WHEN Berth 退出，THE Tray SHALL 移除所有泊位独立托盘图标，通知区域不残留任何 Berth 图标。

### Requirement 18: 开机自启与关闭到托盘

**User Story:** 作为 Berth 用户，我想让 Berth 开机自启并能选择关闭窗口时是否最小化到托盘，这样使用习惯可以按需调整。

#### Acceptance Criteria

1. THE Berth SHALL 在设置中提供"开机自启 Berth"开关，默认关闭。
2. WHEN 用户开启"开机自启 Berth"，THE Berth SHALL 在当前用户（不要求管理员权限）的 Windows 启动项中写入当前 Berth 可执行文件完整路径加 `--autostart` 参数；WHEN 用户关闭该开关，THE Berth SHALL 移除该项且不影响其他启动项。
3. IF 写入或移除 Windows 启动项失败，THEN THE Berth SHALL 将开关恢复为操作前状态，并显示说明失败原因的错误提示。
4. WHEN Berth 以 `--autostart` 启动且 `startMinimized` 为 true，THE Berth SHALL 只显示 Tray 图标；WHEN 以 `--autostart` 启动且 `startMinimized` 为 false，THE Berth SHALL 显示主窗口与 Tray 图标；WHEN 不带 `--autostart` 启动，THE Berth SHALL 显示主窗口。
5. THE Berth SHALL 在设置中提供"关闭窗口时最小化到托盘"开关，默认开启，取值在重启后保持。
6. WHILE "关闭窗口时最小化到托盘"开启，WHEN 用户关闭主窗口，THE Berth SHALL 隐藏主窗口并保持 Tray 图标可见，进程与泊位状态不变。
7. WHILE "关闭窗口时最小化到托盘"关闭，WHEN 用户关闭主窗口且不存在 Active_State 泊位，THE Berth SHALL 退出应用。
8. WHILE "关闭窗口时最小化到托盘"关闭，WHEN 用户关闭主窗口且存在 Active_State 泊位，THE Berth SHALL 先显示列出这些泊位名的确认对话框，提供"停止并退出"与"取消"；"停止并退出"时停止这些泊位后退出，"取消"或关闭对话框时保持主窗口与泊位状态不变。
9. IF 用户选择"停止并退出"后 10 秒内仍有泊位未停止，THEN THE Berth SHALL 通过 Supervisor 强制结束这些泊位的进程树后退出。
10. WHEN Berth 设置界面打开，THE Berth SHALL 按 Windows 启动项实际状态显示"开机自启 Berth"：仅当启动项存在、指向当前 Berth 可执行文件且带 `--autostart` 时显示为开启。

### Requirement 19: 系统通知

**User Story:** 作为 Berth 用户，我想在泊位崩溃、就绪或更新完成时收到系统通知，这样在后台运行时也能及时知道。

#### Acceptance Criteria

1. WHERE "崩溃通知"开关开启，WHEN 泊位发生 Crash，THE Notifier SHALL 在检测到进程退出后 2 秒内发送包含泊位名与退出码的系统通知；用户或 Berth 请求的停止与重启不发送该通知。
2. WHERE "就绪通知"开关开启，WHEN 泊位状态从"启动中"变为"运行中"，THE Notifier SHALL 在 2 秒内发送包含泊位名与端口的就绪通知，同一次启动只发送一次。
3. WHERE "更新通知"开关开启，WHEN Update_Center 的一个升级任务结束，THE Notifier SHALL 发送包含升级项（dsh 或插件名）与结果（成功附新版本号，失败附原因摘要）的通知。
4. THE Berth SHALL 在设置中为崩溃、就绪、更新三类通知分别提供独立开关，默认全部开启，修改立即生效并在重启后保留。
5. IF 某类通知开关关闭，THEN THE Notifier SHALL 不发送该类通知，且不影响其他两类通知与泊位状态、日志的更新。
6. WHEN 用户点击崩溃或就绪通知，THE Berth SHALL 恢复并前置主窗口（含从最小化或仅托盘状态恢复），并选中对应泊位。
7. IF 用户点击通知时对应泊位已被删除，THEN THE Berth SHALL 只恢复并前置主窗口、不改变选中项，并提示该泊位已不存在。
8. WHILE 主窗口处于前台、未最小化且当前选中该泊位，WHEN 该泊位从"启动中"变为"运行中"，THE Notifier SHALL 不发送系统就绪通知，改为在主窗口内显示包含泊位名与端口的提示，5 秒后自动消失。
9. IF Windows 系统通知发送失败或被系统禁用，THEN THE Berth SHALL 将通知内容写入对应泊位日志（更新通知写入 Berth 日志），且不中断泊位运行与升级流程。

### Requirement 20: 日志页增强

**User Story:** 作为 Berth 用户，我想在日志页搜索、过滤和导出日志，这样排查问题更高效。

#### Acceptance Criteria

1. THE Log_Viewer SHALL 提供搜索框，接受最长 256 字符的关键字，按纯文本、不区分大小写匹配当前已加载的日志内容。
2. WHEN 用户输入或修改关键字，THE Log_Viewer SHALL 在最后一次输入后 300ms 内高亮所有匹配项，显示"当前序号/匹配总数"，并提供循环跳转的"上一个"/"下一个"按钮，跳转时将目标滚动到可见区域。
3. IF 关键字无匹配项，THEN THE Log_Viewer SHALL 显示匹配总数为 0 并禁用跳转按钮；IF 关键字为空，THEN THE Log_Viewer SHALL 清除所有搜索高亮。
4. THE Log_Viewer SHALL 提供"仅显示匹配行"开关（默认关闭）与级别过滤（"错误"、"警告"、"全部"，默认"全部"），按第 5 条的判定过滤行；两个过滤同时生效时取交集。
5. THE Log_Viewer SHALL 将包含 `error`、`ERR`、`Error`、`fatal`、`panic`、`Unhandled` 任一字符串（区分大小写）的行判定为错误行并以错误样式高亮；将不属于错误行且包含 `warn`（不区分大小写）的行判定为警告行并以警告样式高亮。
6. THE Log_Viewer SHALL 提供"暂停滚动"开关（默认关闭）；关闭时新日志到达自动滚到底部，开启时继续追加新日志但保持当前可见首行不变。
7. THE Log_Viewer SHALL 提供"复制所选"（未选中文本时禁用）、"复制全部"（当前已加载且通过过滤的全部行）、"清空"、"导出"操作。
8. WHEN 用户点击"清空"且泊位处于 Active_State，THE Log_Viewer SHALL 不修改日志文件，只清空界面并记录当时的文件末尾位置，此后只显示该位置之后的内容；该位置在重新打开日志页后仍保持，直到泊位离开 Active_State。
9. WHEN 用户点击"清空"且泊位不处于 Active_State，THE Log_Viewer SHALL 弹出确认对话框，确认后将日志文件截断为 0 字节并清空界面，取消则保持不变。
10. WHEN 用户点击"导出"并选择路径，THE Log_Viewer SHALL 将完整日志文件（不受清空位置与过滤影响）逐字节复制到该路径并提示成功；取消保存对话框时不创建任何文件。
11. IF 导出、清空或读取日志文件失败，THEN THE Log_Viewer SHALL 显示指明操作与原因的错误信息，保持原日志文件与界面不变，并不留下不完整的导出文件。
12. THE Log_Viewer SHALL 以 B、KB、MB 为单位（1KB = 1024B，保留 1 位小数）显示当前日志文件大小，文件大小变化后 2 秒内刷新；日志文件不存在时显示 0 B 与空日志提示。
13. WHILE 日志文件大于 64KB，THE Log_Viewer SHALL 默认只加载末尾 64KB（从第一个完整行开始）并在顶部提供"加载更多"，每次向前再加载 64KB 且保持当前可见行不跳动；加载到文件开头后隐藏或禁用"加载更多"。

---

## E. 数据统计

### Requirement 21: 会话统计

**User Story:** 作为 Berth 用户，我想看到每个泊位的会话数量与最近活动，这样了解各泊位的使用情况。

> 依赖调研：DSH session index 的文件位置与格式在设计阶段根据 dsh 源码与 dsh-plugins/dsh-launcher 的读取方式确定。

#### Acceptance Criteria

1. WHEN 用户打开某个泊位的详情，THE Session_Stats SHALL 从该泊位 DSH_HOME 的会话数据中（泊位配置了 workspace 时只取该 workspace 对应的项目目录；dsh 会话数据不区分 profile）读取会话总数（非负整数）、活跃会话数（口径在设计阶段根据 dsh 源码确定，满足 0 ≤ 活跃会话数 ≤ 会话总数）与最近活动时间（全部会话最近活动时间的最大值）。
2. WHEN Session_Stats 完成一次读取，THE Berth SHALL 在 1 秒内更新泊位详情：两项计数显示为整数，最近活动时间按本地时区显示为"YYYY-MM-DD HH:mm"；会话总数为 0 时最近活动时间显示"—"。
3. WHILE 泊位处于"运行中"，THE Session_Stats SHALL 每 30 秒（误差 ±2 秒）重新读取并更新显示。
4. WHILE 泊位不处于"运行中"，THE Session_Stats SHALL 停止定时刷新，保留最近一次结果，仅在用户重新打开详情时读取一次。
5. IF session index 文件不存在、无法读取或无法解析，THEN THE Session_Stats SHALL 将三项显示为"暂无数据"并在悬停提示中区分原因（文件不存在、无法读取、格式无法解析）；THE Berth SHALL 不弹出模态错误框，其他功能保持可用。
6. IF 单次读取超过 5 秒未完成，THEN THE Session_Stats SHALL 放弃本次读取，按第 5 条显示并说明读取超时；读取期间界面保持可交互。
7. WHEN 此前显示"暂无数据"的泊位在后续读取中成功解析，THE Session_Stats SHALL 按第 2 条恢复显示并清除错误原因。
8. THE Session_Stats SHALL 以只读方式访问 session index：不创建、修改、删除或重命名该文件，不加排他锁，不阻止 dsh 同时写入。

### Requirement 22: Token 用量与费用统计

**User Story:** 作为 Berth 用户，我想看到各泊位的 Token 用量和估算费用，这样控制使用成本。

> 依赖调研：dsh 是否在本地会话数据中记录 usage（输入/输出/缓存 token、模型名）需在设计阶段确认。若不记录，本需求的第 1–4 条降级为第 5 条行为。

#### Acceptance Criteria

1. WHERE dsh 本地会话数据包含 usage 记录，THE Usage_Stats SHALL 对所选时间范围内的记录按泊位（以泊位的 DSH_HOME 及其 workspace 对应的项目目录归属，不区分 profile）与按模型名分别汇总输入、输出、缓存 token 三项整数值，并显示合计行；模型名缺失的记录归入"未知模型"，缺失的 token 字段按 0 计。
2. THE Usage_Stats SHALL 提供时间范围选择：今日（默认）、近 7 天、近 30 天、全部；按本机本地时区计算，"近 7 天"与"近 30 天"为含今日的最近 7 与 30 个自然日；每条记录按其时间戳归入区间。
3. WHERE 用户在设置中为模型配置了输入、输出、缓存三项单价（每百万 token），THE Usage_Stats SHALL 按"token 数 ÷ 1,000,000 × 单价"计算每个模型的估算费用与合计费用，保留 4 位小数并标注"估算"；未配置单价的模型费用显示"—"、不计入合计，并在合计旁注明存在未计价模型。
4. FOR ALL 时间范围与 usage 数据，各自然日子区间、各泊位、各模型的用量之和 SHALL 分别等于该时间范围总用量（三项 token 各自成立，不变量）。
5. IF 所有泊位的本地会话数据均不包含 usage 记录，THEN THE Usage_Stats SHALL 隐藏用量与费用区域，并显示"当前 dsh 版本未提供用量数据"。
6. THE Usage_Stats SHALL 以只读方式访问会话数据：不创建、修改、删除或重命名任何文件，且不以排他方式打开文件。
7. WHEN 用户打开用量统计界面、切换时间范围或点击"刷新"，THE Usage_Stats SHALL 重新读取并在 3 秒内更新显示；读取期间显示加载状态并保留上一次结果。
8. IF 某个泊位的会话数据读取失败，THEN THE Usage_Stats SHALL 在该泊位行显示失败原因、将其从合计中排除并注明已排除的泊位数，其余照常显示；所有泊位均失败时按第 5 条隐藏区域并提示失败原因。
9. IF 用户输入的单价不是 0 至 10,000（含）之间、最多 4 位小数的数值，THEN THE Berth SHALL 拒绝保存、提示有效范围，并保留此前已保存的单价。

### Requirement 23: 进程运行指标

**User Story:** 作为 Berth 用户，我想看到每个泊位的运行时长、启动/崩溃次数与资源占用，这样判断泊位的健康状况。

#### Acceptance Criteria

1. THE Process_Metrics SHALL 为每个泊位记录：累计运行时长（秒级，自 Supervisor 创建 dsh 进程起至退出止逐次累加）、启动次数（每次由 Supervisor 创建 dsh 进程计 1 次，"复用现有服务"不计入）、崩溃次数（每次 Crash 计 1 次）、最近一次退出码与退出时间（本地时间，精确到秒）。
2. THE Process_Metrics SHALL 将累计数据持久化到 Berth 数据目录下独立于 `instances.json` 的文件：每次启动或退出事件后 1 秒内写入，"运行中"期间每 60 秒写入一次运行时长；Berth 重启后与最后一次写入的值一致。
3. WHILE 泊位处于"运行中"，THE Process_Metrics SHALL 每 2 秒（±0.5 秒）通过该泊位的 Job Object 采集整棵进程树的 CPU 占用百分比（以全部逻辑核心为 100% 归一化，保留 1 位小数）与内存（工作集之和，以 MB 保留 1 位小数）。
4. WHILE 泊位处于"运行中"或"运行中（外部）"，THE Berth SHALL 在泊位详情中显示最近一次采样的 CPU 与内存，以及最近 5 分钟（最多 150 个采样点）的趋势图；其他状态下当前值显示"—"，并显示累计数据。
5. WHILE 泊位处于"运行中（外部）"，THE Process_Metrics SHALL 按第 3 条的间隔与单位仅采集所记录 PID 的单个进程，不计入累计运行时长、启动与崩溃次数，并标注"仅主进程"。
6. WHEN 用户点击"重置统计"并确认，THE Berth SHALL 将该泊位的累计运行时长、启动次数、崩溃次数清零，清除最近退出码与退出时间，并在 1 秒内写入持久化文件；取消时保持不变。
7. FOR ALL 泊位，在任意时刻（含重置后与重启后），崩溃次数 SHALL 小于等于启动次数，且累计运行时长 SHALL 大于等于 0（不变量）。
8. WHILE 泊位处于 Active_State，THE Berth SHALL 禁用"重置统计"并在悬停提示中说明需先停止泊位。
9. IF 某次采样失败（Job Object 查询失败或外部 PID 已不存在），THEN THE Process_Metrics SHALL 跳过该采样点、当前值显示"—"、保留已有趋势，并在下一周期继续采样。
10. IF Berth 启动时指标持久化文件不存在或无法解析，THEN THE Process_Metrics SHALL 将所有泊位累计数据视为 0，在泊位详情中提示统计已重置及原因，并在下一次写入时生成新文件。

### Requirement 24: 顶部汇总面板

**User Story:** 作为 Berth 用户，我想在主窗口顶部看到全局状态汇总，这样快速掌握整体情况。

#### Acceptance Criteria

1. THE Summary_Panel SHALL 在主窗口顶部常驻显示五项指标：泊位总数、运行中数量（"运行中"或"运行中（外部）"）、失败数量（"失败"或"崩溃已停止"）、内存总和、CPU 总和。
2. THE Summary_Panel SHALL 将内存总和计算为所有运行中泊位最近一次采样的内存之和（小于 1024 MB 显示 MB 整数，否则以 GB 保留 1 位小数），CPU 总和为同一批泊位最近一次采样的 CPU 百分比之和（保留 1 位小数）。
3. IF 某个运行中泊位尚无采样值，THEN THE Summary_Panel SHALL 将其按 0 计入；运行中数量为 0 时显示 0 MB 与 0.0%，泊位总数为 0 时三个数量均显示 0。
4. WHEN 任一泊位状态变化、泊位被新增或删除、或 Process_Metrics 产生新采样，THE Summary_Panel SHALL 在 2 秒内更新全部五项指标。
5. WHEN 用户点击"运行中"或"失败"数字，THE Berth SHALL 将泊位列表筛选为对应状态并以选中样式标示；再次点击已选中的数字时取消筛选。
6. WHILE 泊位列表处于"运行中"或"失败"筛选状态，WHEN 某泊位进入或离开对应状态集合，THE Berth SHALL 在 2 秒内将其加入或移出列表。
7. WHEN Update_Center 完成一次检查且可用更新数量大于等于 1，THE Summary_Panel SHALL 显示该数量；数量为 0 或尚未完成任何检查时隐藏该项。
8. WHEN 用户点击 Summary_Panel 中的可用更新数量，THE Berth SHALL 打开 Update_Center。
9. FOR ALL 泊位集合，运行中数量与失败数量之和 SHALL 小于等于泊位总数，且三者均为大于等于 0 的整数（不变量）。

---

## F. 通用约束

### Requirement 25: 数据文件向后兼容

**User Story:** 作为已有 Berth 用户，我想升级后原有配置照常可用，这样不必重新配置。

#### Acceptance Criteria

1. WHEN Berth 读取的 `settings.json` 或 `instances.json` 缺少当前版本新增的字段，THE Berth SHALL 为每个缺失字段使用其默认值，其余已有字段的值逐字段不变，且启动过程中不弹出错误提示。
2. IF 某个已识别字段的值类型与当前版本定义不符，THEN THE Berth SHALL 对该字段使用默认值、保留其他字段，并在启动完成后提示具体的文件名和字段名。
3. WHEN Berth 写回 `settings.json` 或 `instances.json`，THE Berth SHALL 原样保留其无法识别的字段（含顶层与每个泊位条目内的字段）。
4. WHEN Berth 写回数据文件，THE Berth SHALL 写入当前支持的 schema 版本号（整数）；不含该字段的文件视为 schema 版本 0。
5. IF 读取到的 schema 版本高于当前支持的版本，THEN THE Berth SHALL 以只读模式加载：界面照常显示数据，禁止所有会写回该文件的保存操作，磁盘文件保持不变，并提示用户升级 Berth。
6. WHEN Berth 首次将低版本 schema 的数据文件以当前 schema 写回，THE Berth SHALL 先将原文件按原字节复制为同目录下的 `<文件名>.bak-<旧版本号>`（例如 `instances.json.bak-0`）再写入；同名备份已存在时不覆盖。
7. IF 备份文件创建失败，THEN THE Berth SHALL 放弃本次写回、保持原文件不变，并提示备份失败的文件名。
8. IF 数据文件不是合法 JSON 或顶层结构不符合定义，THEN THE Berth SHALL 不覆盖、不删除该文件，以只读模式加载其余可用数据，并提示无法解析的文件名。
9. FOR ALL 合法 `instances.json` 与 `settings.json`，读取后不做修改直接写回再读取 SHALL 得到等价的数据：字段集合（含无法识别的字段）与值相同，泊位条目数量和顺序相同；键顺序与空白差异不影响判定（往返性质）。

### Requirement 26: 异步执行与界面响应

**User Story:** 作为 Berth 用户，我想在长耗时操作进行时界面仍能操作，这样不会误以为程序卡死。

#### Acceptance Criteria

1. THE Berth SHALL 以不阻塞主线程的异步方式执行以下长耗时操作：网络请求、子进程调用（npm、pnpm、git、winget、dsh）、Bundle 打包与解包、依赖重建、日志文件读取、统计数据扫描（Session_Stats、Usage_Stats）。
2. WHILE 至少一项长耗时操作进行中，THE Berth SHALL 保持主线程事件循环单次阻塞不超过 100ms，使窗口拖动、滚动、页面切换与可用按钮在 100ms 内响应。
3. WHEN 用户发起一项长耗时操作，THE Berth SHALL 在 200ms 内于发起控件处或其所在卡片/对话框内显示进行中状态。
4. WHILE 一项长耗时操作进行中，THE Berth SHALL 禁用作用于同一对象（同一泊位、Profile 或插件）的同类或会修改该对象的操作，其他对象的操作保持可用。
5. WHEN 一项长耗时操作成功完成，THE Berth SHALL 在 500ms 内移除进行中状态并恢复被禁用的控件。
6. IF 一项长耗时操作失败，THEN THE Berth SHALL 在 500ms 内移除进行中状态、恢复被禁用的控件，并在发起位置显示指明失败操作与原因的错误信息。
7. THE Berth SHALL 以 `CREATE_NO_WINDOW` 方式启动第 1 条所列的全部后台子进程，运行期间不出现控制台窗口。
8. WHEN Berth 退出，THE Berth SHALL 在 5 秒内结束仍在运行的后台任务子进程及其子进程树；由 Supervisor 管理的泊位 dsh 进程不属于本条的后台任务子进程。

### Requirement 27: 平台与边界

**User Story:** 作为项目维护者，我想明确本特性的平台与边界，这样实现不会越界。

#### Acceptance Criteria

1. THE Berth SHALL 以 Windows 10 1809（内部版本 17763）为最低支持版本，全部需求在内部版本 ≥ 17763 的 Windows 10 与 Windows 11 上可用，且不依赖仅在更高版本提供的系统 API。
2. THE Berth SHALL 只通过三种途径与 dsh 交互：启动 dsh 进程时的命令行参数、注入的环境变量（含 Launch_Env），读写 Profile 目录内的配置文件与依赖，以及只读访问 `<DSH_HOME>/sessions`、`<DSH_HOME>/storages` 下的会话数据与 DSH_HOME/workspace 下的 skills 目录（Skills 启用/禁用需移动其条目）。
3. THE Berth SHALL 保持 dsh 安装目录（System_Dsh 对应安装目录与 Dsh_Version_Store 下各版本目录）内的文件在内容、数量与文件名上不变；用户在 Update_Center 或版本管理界面中显式发起的升级、安装或删除除外。
4. THE Berth SHALL 只使用现有 Qt 6.10.2 模块集合实现功能：Core、Gui、Qml、Quick、QuickControls2、Network、Widgets、WebView；另允许单独的测试目标使用 Test 模块，仅用于纯逻辑的单元测试与性质测试，不链接进 `dsh-berth` 主程序。
5. IF 某项需求的实现需要新增 Qt 模块或第三方库，THEN THE 设计文档 SHALL 为每个新增项列出名称、版本、引入原因及现有模块无法满足的理由；未列明的新增项不得出现在构建配置中。
6. THE Berth SHALL 由用户完成全部编译、构建与运行验证；实施过程中 AI 不得执行编译、构建、测试或启动 Berth 与 dsh 进程。
7. THE 每个实施任务 SHALL 附带用户可执行的手动验证步骤，至少包含前置条件、按顺序编号的操作步骤与每一步的可观察预期结果。
