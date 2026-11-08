# Kick Cat Free — Windows 编译说明

> 目标：Windows x64 的 `Kick Cat Free.vst3`

---

## 一、环境

| 项目 | 要求 |
|---|---|
| Windows | 10 / 11 x64 |
| Visual Studio | 2019 或 2022（勾选「使用 C++ 的桌面开发」） |
| CMake | 3.22+ |
| Git | 任意（JUCE 通过 FetchContent 拉取） |
| 磁盘 | 20 GB |

---

## 二、编译

### 1. 打开正确的命令提示符

**必须用「x64 Native Tools Command Prompt for VS 2022」**

开始菜单搜索 `x64 Native Tools`，或手动：

```bat
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

普通 cmd 找不到 `cl.exe`。

### 2. 配置

```bat
cd C:\path\to\KickCat-Free

cmake -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
```

**首次配置会自动下载 JUCE 8.0.4**（约 100 MB），需要联网，3~10 分钟。

### 3. 编译

```bat
cmake --build build-win --config Release --target KickForge_VST3 --parallel
```

约 5~15 分钟。

### 4. 产物

```
build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3
```

**Windows 上只出 VST3**（AU 是 macOS 专有格式）。

---

## 三、安装

```bat
xcopy /E /I "build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3" ^
      "C:\Program Files\Common Files\VST3\Kick Cat Free.vst3"
```

需要管理员权限。装完重启 DAW。

---

## 四、验证

### 检查依赖是否干净

```bat
dumpbin /dependents "build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3\Contents\x86_64-win\Kick Cat Free.vst3"
```

**不应出现：**
```
MSVCP140.dll
VCRUNTIME140.dll
api-ms-win-crt-*.dll
```

出现了说明 CRT 没静态链接，用户机器会缺 DLL 导致宿主静默跳过插件。

---

## 五、常见问题

### `error C1070` 或几百处 `C2001`

**原因：** `/utf-8` 没传到 juceaide 的嵌套子工程。
**解决：** 确认 CMakeLists 开头有：

```cmake
set(ENV{CL} "$ENV{CL} /utf-8")
```

而且必须在 `add_subdirectory(libs/JUCE)` **之前**。

### 宿主里看不到插件

**原因：** 缺 `MSVCP140.dll`。
**排查：** `dumpbin /dependents` 看是否有这个 DLL。

### 编译很慢

默认会编译所有测试工具（`SelfTest` / `StressTest` / `UIShot` 等 9 个）。

**只编译插件：**
```bat
cmake --build build-win --config Release --target KickForge_VST3 --parallel
```

---

## 六、与 macOS 版本的关系

同一份源码，两个平台共用：

| 项目 | macOS | Windows |
|---|---|---|
| 插件格式 | AU + VST3 | 仅 VST3 |
| 编译器 | Clang | MSVC |
| CRT | 系统自带 | **必须静态链接** |
| 源码编码 | UTF-8（默认） | **必须 /utf-8 + CL 环境变量** |

---

## 七、发布检查清单

```
☐ 编译 0 错误
☐ dumpbin 依赖干净（无 MSVCP140.dll）
☐ 装到 VST3 目录后 DAW 能识别
☐ 所有功能可用（导出 WAV / AI / 预设）
☐ 界面上没有多余的按钮
☐ 版本号正确（1.1.1）
```
