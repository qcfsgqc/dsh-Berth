"""本机路径配置模板。复制为同目录下的 local.py 再修改（local.py 不进 git）。
首次运行 build.py 时若 local.py 不存在，会自动从本文件复制一份。

填 None 或留空字符串表示自动查找。命令行参数优先级高于这里。
路径建议用原始字符串 r"..."，避免反斜杠转义。
"""

# Qt 6.10.2 msvc2022_64 前缀目录（包含 bin/、lib/cmake/Qt6/）
QT_PREFIX = None  # 例如 r"C:\Qt\6.10.2\msvc2022_64"

# vcvars64.bat 路径；留空则用 vswhere 自动查找 VS
VCVARS = None  # 例如 r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

# cmake.exe / ninja.exe 路径；留空则按 PATH → VS 自带 → Qt Tools 查找
CMAKE = None  # 例如 r"C:\Qt\Tools\CMake_64\bin\cmake.exe"
NINJA = None  # 例如 r"C:\Qt\Tools\Ninja\ninja.exe"

# 构建输出根目录；相对路径以仓库根目录为基准。实际目录为 <BUILD_DIR>/<配置>，如 build/Debug、build/Release
BUILD_DIR = "build"

# 默认构建配置：Debug / Release / RelWithDebInfo / MinSizeRel
CONFIG = "Release"
