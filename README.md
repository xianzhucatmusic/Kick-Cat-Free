# Kick Cat Free

底鼓合成器插件，基于 JUCE 8.0.4。macOS 出 AU / VST3 / Standalone，Windows 出 VST3。

完全开源，无任何功能限制。

---

## 功能概览

### 音频引擎

| 模块 | 说明 |
|---|---|
| **Sub 振荡器** | 加法合成，6 种波形（Sine / Triangle / Saw / Square / Noise / Pulse），可调泛音数量与衰减速度（Harmonic Tilt）、次谐波、噪声层、双二阶滤波与滤波包络 |
| **Click 层** | 4 个独立槽位，每层可选程序生成的内置采样，或切换到内置波形 |
| **8 段参数均衡** | 可选节点的曲线编辑界面，每段独立开关、频率 / 增益 / Q / 斜率 |
| **动态处理** | 10 种失真模型 → 压缩 → 限幅（顺序固定，不可调换） |
| **瞬态整形** | 快慢双包络跟随器，可分别加权起音与延音 |

### 调制系统

- **调制源**：4 个通用 ADSR 包络（各自独立时长）+ 2 个 LFO（自由运行 / 宿主速度同步 + 相位重置）+ 力度 / 键位 / 随机
- **调制目标**：31 个（EQ 8 段增益与频率 Q、失真 4 项、Sub 13 项、Click 3 项、动态 3 项、总输出）
- **路由**：最多 12 条，每条可设源、目标、深度

设计上用**叠加偏移**而非替换参数 —— 深度为 0 时与无调制**逐样本一致**，所以旧预设不需要迁移。

### 本地 AI 音色生成

用自然语言描述音色（例如「更闷、尾巴短一点的 808」），由本地 llama.cpp + Qwen2.5-1.5B 生成参数。

模型不可用时自动回退到本地关键词引擎，功能不会完全失效 —— 不依赖任何在线服务。

### 其他

- 工厂预设 + 用户预设管理
- WAV 导出
- 内置单元测试 / 压力测试工具

---

## 构建

### 环境要求

| 项目 | 要求 |
|---|---|
| CMake | 3.22+ |
| 编译器 | macOS：Xcode Command Line Tools（Clang）<br>Windows：Visual Studio 2019 / 2022（勾选「使用 C++ 的桌面开发」） |
| 磁盘 | 约 5 GB |
| 网络 | 首次配置需联网拉取 JUCE 8.0.4 |

JUCE 不需要手动准备：若本地存在 `libs/JUCE` 就直接复用，否则 CMake 通过 FetchContent 自动克隆。

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target KickForge_All --parallel
```

产物：

```
build/KickForge_artefacts/Release/AU/Kick Cat Free.component
build/KickForge_artefacts/Release/VST3/Kick Cat Free.vst3
build/KickForge_artefacts/Release/Standalone/Kick Cat Free.app
```

安装到用户插件目录：

```bash
cp -R "build/KickForge_artefacts/Release/AU/Kick Cat Free.component" ~/Library/Audio/Plug-Ins/Components/
cp -R "build/KickForge_artefacts/Release/VST3/Kick Cat Free.vst3"        ~/Library/Audio/Plug-Ins/VST3/
```

Apple Silicon 上未签名的 AU 首次加载可能被拒绝，执行一次即可：

```bash
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Kick Cat Free.component"
```

### Windows

必须用 **「x64 Native Tools Command Prompt for VS 2022」**，普通 cmd 找不到 `cl.exe`。

```bat
cmake -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release --target KickForge_VST3 --parallel
```

产物：`build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3`

安装（管理员权限）：

```bat
xcopy /E /I "build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3" ^
      "C:\Program Files\Common Files\VST3\Kick Cat Free.vst3"
```

也可以直接双击 `build-windows.bat`，脚本会检查环境并完成上述三步。

**依赖自检**（重要）：

```bat
dumpbin /dependents "build-win\KickForge_artefacts\Release\VST3\Kick Cat Free.vst3\Contents\x86_64-win\Kick Cat Free.vst3"
```

输出中**不应**出现 `MSVCP140.dll` / `VCRUNTIME140.dll`。出现了说明 CRT 没有静态链接，用户机器缺少 VC++ 运行库时宿主会静默跳过该插件。

更多编译细节与常见报错见 [Windows编译说明.md](Windows编译说明.md)。

---

## 目录结构

```
KickCat-Free/
├── CMakeLists.txt
├── Source/                      插件源码
│   ├── PluginProcessor.*        音频引擎主体
│   ├── PluginEditor.*           界面
│   ├── SubSynth.*               Sub 振荡器
│   ├── ClickLayer.*             Click 层
│   ├── Envelope.*  EnvelopeEditor.*
│   ├── Equalizer.* EqCurveEditor.*
│   ├── Distortion.* MasterChain.*
│   ├── ModulationMatrix.* ModulationPanel.*   调制系统
│   ├── SampleLibrary.*          Click 层采样
│   ├── PresetManager.* FactoryPresets.*       预设
│   ├── AiApplier.* AiChatPanel.* AiPatch.*
│   ├── LocalPatchEngine.*       本地关键词引擎
│   ├── LlamaBackend.* ModelManager.*          llama.cpp 推理
│   ├── WavExporter.*            WAV 导出
│   ├── Crypto.*                 AEAD 工具
│   └── Theme.h KickLookAndFeel.* PawGraphics.h
├── Tools/                       测试与控制台工具
│   ├── SelfTest_main.cpp        自检
│   ├── StressTest_main.cpp      压力测试
│   ├── WaveTest_main.cpp        波形 / DSP 测试
│   ├── ExportTest_main.cpp      WAV 导出测试
│   ├── PluginChain_main.cpp     插件链测试
│   ├── EqInteraction_main.cpp   EQ 交互测试
│   ├── BugHunt_main.cpp         边界与异常排查
│   ├── UIShot_main.cpp          界面截图
│   └── PresetCheck_main.cpp     预设读写检查
├── toolchain-mingw.cmake        MinGW 交叉编译工具链
├── build-windows.bat            Windows 一键构建
└── Windows编译说明.md
```

---

## 测试工具

工程默认会编译一组控制台工具。只想出插件时指定 target：

```bash
cmake --build build --config Release --target KickForge_VST3 --parallel
```

运行自检：

```bash
./build/Tools/SelfTest_artefacts/Release/SelfTest
./build/Tools/StressTest_artefacts/Release/StressTest
```

---

## 许可证

源代码以 **GPL-3.0** 发布，详见 [LICENSE](LICENSE)。

JUCE 8.0.4 同样以 GPL-3.0 提供，二者兼容。

---

## 反馈

问题、建议、Bug 报告请开 Issues。
