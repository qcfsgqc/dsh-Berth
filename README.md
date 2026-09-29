# DSH Berth

非官方 Windows 控台，用来在 dsh 外面做设置、启停和多开。不改 DeepSeek Harness 本体，也不包官方 Web UI。

- 显示名：DSH Berth
- 可执行文件：`dsh-berth`
- Qt：6.10.2（精确版本）
- 当前平台：仅 Windows

数据默认在 `%APPDATA%\dsh-Berth\`：`settings.json` 与 `instances.json`。每套实例单独端口、profile、`DSH_HOME`。

启动时扫描 `DSH_HOME`（未设置则 `%USERPROFILE%\.dsh`）下的 `profiles/`。目录里有 `package.json` 或 `cordis.patch.yml` 才算 profile。泊位列表为空时自动导入；设置页可再识别一次，已有的不重复建。

## 构建

一键编译（需要系统里的 Python 3.10，会自动找 Qt、加载 MSVC 环境、找 cmake/ninja）：

```powershell
py -3.10 scripts/build.py
py -3.10 scripts/build.py --config Debug --run
py -3.10 scripts/build.py --deploy   # 用 windeployqt 把依赖拷到 exe 旁
```

本机路径（Qt、vcvars64、cmake、ninja、构建目录）写在 `scripts/local.py`，不进 git；首次运行会从 `scripts/local.example.py` 自动复制。优先级：命令行参数 > `local.py` > 环境变量 > 自动查找。

手动构建，用 Qt 6.10.2 的 MSVC kit（64-bit）：


```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\msvc2022_64"
cmake --build build
.\build\dsh-berth.exe
```

`CMAKE_PREFIX_PATH` 换成本机 Qt 6.10.2 安装路径。CMake 会拒绝其它系统和其它 Qt 版本。

## 布局

- `src/` 实例模型、设置、进程监督、给 QML 的控制器
- `qml/` 控台界面：泊位列表、详情、设置
