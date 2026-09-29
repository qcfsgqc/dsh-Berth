#!/usr/bin/env python3
"""DSH Berth 一键编译脚本（Windows + Python 3.10，只用标准库）。

用法（在仓库任意位置运行均可）：
    py -3.10 scripts/build.py                 # 按 local.py 的 CONFIG（默认 Release）编译
    py -3.10 scripts/build.py --config Debug
    py -3.10 scripts/build.py --clean         # 删掉当前配置的构建目录后重新配置
    py -3.10 scripts/build.py --deploy        # 编译后跑 windeployqt，把依赖拷到 exe 旁
    py -3.10 scripts/build.py --run           # 编译后启动 dsh-berth.exe
    py -3.10 scripts/build.py --qt D:\\Qt\\6.10.2\\msvc2022_64

本机路径写在 scripts/local.py（不进 git，首次运行自动从 local.example.py 复制）。
各配置的构建目录互相独立：<BUILD_DIR>/<config>，如 build/Debug、build/Release。
优先级：命令行参数 > local.py > 环境变量 > 自动查找。
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

QT_VERSION = "6.10.2"
QT_KIT = "msvc2022_64"
SCRIPTS_DIR = Path(__file__).resolve().parent
ROOT = SCRIPTS_DIR.parent
LOCAL_FILE = SCRIPTS_DIR / "local.py"
LOCAL_EXAMPLE = SCRIPTS_DIR / "local.example.py"
EXE_NAME = "dsh-berth.exe"
CONFIGS = ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"]


def log(msg: str) -> None:
    print(f"[build] {msg}", flush=True)


def fail(msg: str) -> "None":
    print(f"[build] 错误：{msg}", file=sys.stderr, flush=True)
    sys.exit(1)


# ---------------------------------------------------------------- local.py

def load_local() -> SimpleNamespace:
    """读取 scripts/local.py；不存在时从模板复制一份。"""
    if not LOCAL_FILE.is_file():
        if LOCAL_EXAMPLE.is_file():
            shutil.copyfile(LOCAL_EXAMPLE, LOCAL_FILE)
            log(f"已从模板生成 {LOCAL_FILE}，需要时改里面的路径")
        else:
            log("未找到 local.py 和 local.example.py，全部自动查找")
            return SimpleNamespace()

    spec = importlib.util.spec_from_file_location("berth_local", LOCAL_FILE)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(module)
    except Exception as exc:  # 配置文件写错时给出明确提示
        fail(f"读取 {LOCAL_FILE} 失败：{exc}")
    return SimpleNamespace(**{k: v for k, v in vars(module).items() if k.isupper()})


def opt_path(value: object) -> Path | None:
    """把 local.py 里的值转成 Path；None / 空字符串视为未配置。"""
    if value is None or (isinstance(value, str) and not value.strip()):
        return None
    return Path(str(value)).expanduser()


# ---------------------------------------------------------------- Qt

def is_qt_prefix(path: Path) -> bool:
    return (path / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").is_file()


def find_qt(configured: Path | None) -> Path:
    if configured is not None:
        if not is_qt_prefix(configured):
            fail(f"配置的 Qt 前缀无效（缺少 lib/cmake/Qt6）：{configured}")
        return configured.resolve()

    candidates: list[Path] = []
    for var in ("QT_PREFIX", "QTDIR", "QT_DIR"):
        value = os.environ.get(var)
        if value:
            candidates.append(Path(value))
    for drive in "CDEFGH":
        for base in (f"{drive}:\\Qt", f"{drive}:\\Qt6", f"{drive}:\\Tools\\Qt"):
            candidates.append(Path(base) / QT_VERSION / QT_KIT)

    for path in candidates:
        if is_qt_prefix(path):
            return path.resolve()

    fail(
        f"找不到 Qt {QT_VERSION} ({QT_KIT})。"
        f"请在 {LOCAL_FILE} 里设置 QT_PREFIX，或用 --qt 指定"
    )
    raise AssertionError  # 不可达


# ---------------------------------------------------------------- MSVC

def find_vs_install() -> Path | None:
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    result = subprocess.run(
        [
            str(vswhere), "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    lines = result.stdout.strip().splitlines()
    return Path(lines[0]) if result.returncode == 0 and lines else None


def env_path(env: dict[str, str]) -> str:
    for key, value in env.items():
        if key.upper() == "PATH":
            return value
    return ""


def prepend_path(env: dict[str, str], directory: Path) -> None:
    for key in list(env):
        if key.upper() == "PATH":
            env[key] = f"{directory}{os.pathsep}{env[key]}"
            return
    env["PATH"] = str(directory)


def load_msvc_env(env: dict[str, str], vcvars: Path | None, vs: Path | None) -> dict[str, str]:
    """未指定 vcvars 且当前已在 VS 开发者命令行中则沿用；否则执行 vcvars64.bat 抓取环境。"""
    if vcvars is None:
        if shutil.which("cl", path=env_path(env)):
            log("检测到 cl.exe，沿用当前 MSVC 环境")
            return env
        if vs is None:
            fail(f"找不到带 C++ 工具集的 Visual Studio。请在 {LOCAL_FILE} 里设置 VCVARS")
        vcvars = vs / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        fail(f"找不到 {vcvars}")

    log(f"加载 MSVC 环境：{vcvars}")
    # /u 让 cmd 内建命令（set）以 UTF-16LE 输出，避免代码页导致的乱码
    cmd = f'cmd /d /u /s /c ""{vcvars}" >nul 2>&1 && set"'
    result = subprocess.run(cmd, capture_output=True, env=env)
    if result.returncode != 0:
        fail("vcvars64.bat 执行失败")

    new_env: dict[str, str] = {}
    for line in result.stdout.decode("utf-16-le", errors="replace").splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            new_env[key] = value
    if not shutil.which("cl", path=env_path(new_env)):
        fail("加载 vcvars64 后仍找不到 cl.exe")
    return new_env


# ---------------------------------------------------------------- 工具

def find_tool(name: str, configured: Path | None, env: dict[str, str], extra_dirs: list[Path]) -> Path:
    if configured is not None:
        if not configured.is_file():
            fail(f"配置的 {name} 不存在：{configured}")
        return configured
    found = shutil.which(name, path=env_path(env))
    if found:
        return Path(found)
    for directory in extra_dirs:
        exe = directory / f"{name}.exe"
        if exe.is_file():
            return exe
    fail(f"找不到 {name}。请在 {LOCAL_FILE} 里设置 {name.upper()}，或通过 Qt Maintenance Tool 安装")
    raise AssertionError


def drop_stale_cache(build_dir: Path, cl_path: Path) -> None:
    """构建目录里缓存的编译器与本次不同（如之前用 VS 18 配置过）时，删掉 CMake 缓存重新配置。"""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return
    cached = None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_CXX_COMPILER:"):
            cached = line.partition("=")[2].strip()
            break
    if not cached:
        return
    cached_path = Path(os.path.realpath(cached))
    if os.path.normcase(str(cached_path)) == os.path.normcase(str(cl_path)):
        return
    log(f"缓存的编译器与本次不同，重置 CMake 缓存：{cached_path}")
    cache.unlink()
    shutil.rmtree(build_dir / "CMakeFiles", ignore_errors=True)


def run(cmd: list[str], env: dict[str, str], cwd: Path = ROOT) -> None:
    log(" ".join(f'"{c}"' if " " in c else c for c in cmd))
    result = subprocess.run(cmd, cwd=cwd, env=env)
    if result.returncode != 0:
        fail(f"命令失败（退出码 {result.returncode}）")


# ---------------------------------------------------------------- main

def main() -> None:
    if sys.platform != "win32":
        fail("仅支持 Windows")
    if sys.version_info < (3, 10):
        fail("需要 Python 3.10+，请用 py -3.10 scripts/build.py 运行")

    local = load_local()
    default_config = getattr(local, "CONFIG", None) or "Release"
    if default_config not in CONFIGS:
        fail(f"local.py 里的 CONFIG 无效：{default_config}，可选 {CONFIGS}")

    parser = argparse.ArgumentParser(description="DSH Berth 一键编译")
    parser.add_argument("--config", default=default_config, choices=CONFIGS)
    parser.add_argument("--qt", help=f"Qt 前缀路径，如 C:\\Qt\\{QT_VERSION}\\{QT_KIT}")
    parser.add_argument("--clean", action="store_true", help="删除构建目录后重新配置")
    parser.add_argument("--deploy", action="store_true", help="编译后运行 windeployqt")
    parser.add_argument("--run", action="store_true", help="编译后启动程序")
    args = parser.parse_args()

    build_root = opt_path(getattr(local, "BUILD_DIR", None)) or Path("build")
    if not build_root.is_absolute():
        build_root = ROOT / build_root
    build_dir = build_root / args.config  # 每种配置单独一个目录，互不覆盖
    log(f"构建目录：{build_dir}")

    qt_prefix = find_qt(opt_path(args.qt) or opt_path(getattr(local, "QT_PREFIX", None)))
    log(f"Qt: {qt_prefix}")
    qt_tools = qt_prefix.parents[1] / "Tools"  # 例如 C:\Qt\Tools

    vs = find_vs_install()
    env = load_msvc_env(dict(os.environ), opt_path(getattr(local, "VCVARS", None)), vs)

    vs_cmake_dir = (vs / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake") if vs else None
    cmake = find_tool("cmake", opt_path(getattr(local, "CMAKE", None)), env, [
        qt_tools / "CMake_64" / "bin",
        *([vs_cmake_dir / "CMake" / "bin"] if vs_cmake_dir else []),
    ])
    ninja = find_tool("ninja", opt_path(getattr(local, "NINJA", None)), env, [
        qt_tools / "Ninja",
        *([vs_cmake_dir / "Ninja"] if vs_cmake_dir else []),
    ])
    cl = shutil.which("cl", path=env_path(env))
    if not cl:
        fail("MSVC 环境里找不到 cl.exe")
    cl_path = Path(os.path.realpath(cl))
    log(f"CMake: {cmake}")
    log(f"Ninja: {ninja}")
    log(f"cl:    {cl_path}")

    if args.clean and build_dir.exists():
        log(f"清理 {build_dir}")
        shutil.rmtree(build_dir)
    else:
        drop_stale_cache(build_dir, cl_path)

    run([
        str(cmake), "-S", str(ROOT), "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}",
        f"-DCMAKE_PREFIX_PATH={qt_prefix.as_posix()}",
        f"-DCMAKE_BUILD_TYPE={args.config}",
        f"-DCMAKE_CXX_COMPILER={cl_path.as_posix()}",
    ], env)
    run([str(cmake), "--build", str(build_dir), "--parallel"], env)

    exe = build_dir / EXE_NAME
    if not exe.is_file():
        fail(f"编译结束但没找到 {exe}")
    log(f"完成：{exe}")

    qt_bin = qt_prefix / "bin"
    if args.deploy:
        windeployqt = qt_bin / "windeployqt.exe"
        if not windeployqt.is_file():
            fail(f"找不到 {windeployqt}")
        mode = "--debug" if args.config == "Debug" else "--release"
        run([str(windeployqt), mode, "--qmldir", str(ROOT / "qml"), str(exe)], env)

    if args.run:
        run_env = dict(env)
        prepend_path(run_env, qt_bin)  # 未 deploy 时也能找到 Qt DLL
        log(f"启动 {exe}")
        subprocess.Popen([str(exe)], cwd=build_dir, env=run_env)


if __name__ == "__main__":
    main()
