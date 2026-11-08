cmake_minimum_required(VERSION 3.22)

project(KickForge VERSION 1.1.1 LANGUAGES C CXX)   # 免费版，见下方 FREE 标记

#==============================================================================
# Windows：全局设置 /utf-8（**必须在 JUCE 之前**）
#
# ⚠️ 为什么不能只在下面的 if(MSVC) 块里加：
#
# JUCE 在 **configure 阶段**会构建一个叫 juceaide 的辅助工具，
# 而且是在**嵌套的 CMake 子工程**里构建的
# （见 libs/JUCE/extras/Build/juceaide/CMakeLists.txt:133 的
#   execute_process(... --build .../tools)）。
#
# 子工程有自己的变量作用域 —— 父工程里后面才 add_compile_options()
# 对它完全无效。所以 /utf-8 必须在 add_subdirectory(libs/JUCE)
# **之前**就设成全局默认值。
#
# 不设的后果（中文 Windows 实测）：
#   error C1070: 文件中的 #if/#endif 对不匹配
#   —— 实际是 UTF-8 注释里的中文字节被按 GBK 解读，
#      把预处理指令的配对搞乱了，报在一个无辜的 JUCE 头文件上。
#==============================================================================
#==============================================================================
# Windows：静态链接 C++ 运行时（必须在 juce_add_plugin 之前）
#
# ⚠️ 这行原来放在文件靠后的位置（juce_generate_juce_header 之后），
# 结果**完全不生效** —— juce_add_plugin 已经执行完，target 的
# MSVC_RUNTIME_LIBRARY 属性早就定死了。
#
# 症状：dumpbin /dependents 里能看到 MSVCP140.dll / VCRUNTIME140.dll /
#       api-ms-win-crt-*.dll，用户机器没装 VC++ Redistributable 时
#       宿主会静默跳过插件（文件在、路径对，但插件列表里没有）。
#       同时链接期会有 LNK4098: 默认库"LIBCMT"与其他库的使用冲突。
#       实测踩过。
#
# 必须满足两个条件才生效：
#   1. 设置位置在 juce_add_plugin() 之前
#   2. policy CMP0091 为 NEW
#==============================================================================
if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} /utf-8")
    add_compile_options(/utf-8)
    add_compile_definitions(UNICODE _UNICODE)

    # ⚠️ 上面三种方式**都传不到 juceaide 的嵌套子工程**。
    #
    # JUCE 在 configure 阶段这样构建 juceaide：
    #
    #   execute_process(COMMAND \${CMAKE_COMMAND} .
    #       -B\${JUCE_BINARY_DIR}/tools
    #       "-G\${CMAKE_GENERATOR}"
    #       -DCMAKE_MAKE_PROGRAM=... -DCMAKE_BUILD_TYPE=Debug
    #       -DJUCE_BUILD_HELPER_TOOLS=ON ...)
    #
    # 它只显式传了几个 -D，**不继承父工程的任何编译选项**。
    # 实测子工程的 CMakeCache.txt 里是：
    #   CMAKE_CXX_FLAGS = /DWIN32 /D_WINDOWS /EHsc
    # 完全没有 /utf-8 —— 于是 juceaide 仍然按 GBK 解析 UTF-8 源码，
    # 报 error C1070。
    #
    # 可靠的办法是设 **CL 环境变量**：MSVC 的 cl.exe 会读取它，
    # 而且 execute_process 创建的子进程**会继承环境变量**。
    # 这样嵌套的 cmake / msbuild / cl 都能拿到 /utf-8。
    set(ENV{CL} "$ENV{CL} /utf-8")
    message(STATUS "MSVC: /utf-8 已加入 CL 环境变量（覆盖嵌套子工程）")

    # 同时给 _CL_（CMake 转发的那个），双保险
    set(ENV{_CL_} "$ENV{_CL_} /utf-8")
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE：优先用 libs/JUCE（本地已有 8.0.4），没有则 FetchContent 拉取。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    add_subdirectory(libs/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        51d11a2be6d5c97ccf12b4e5e827006e19f0555a  # JUCE 8.0.4
        GIT_SHALLOW    FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()

# AU 只在 macOS 存在；其他平台只出 VST3。
if (APPLE)
    set(KF_FORMATS AU VST3)
else()
    set(KF_FORMATS VST3)
endif()

juce_add_plugin(KickForge
    COMPANY_NAME "XianzhuCatMusic"
    # 制造商标识与「弦烛之声」系列一致（Xzzs），便于宿主里归到同一厂商下
    PLUGIN_MANUFACTURER_CODE Xzzs
    # ── 免费版插件码 ──────────────────────────────────────────────
    # KFC1 = Kick Cat Free。
    # 不能沿用 KF01 —— 那与付费版 Kick Cat 的 AU 类型码完全相同，
    # macOS 会把两个插件当成同一个，只加载其中一个（实测踩过）。
    # 代价：免费版和付费版在宿主里是两个独立插件，工程不通用。
    # 🔻 改回付费版：把 KFC1 换回 KF01
    PLUGIN_CODE KFC1
    FORMATS ${KF_FORMATS}
    PRODUCT_NAME "Kick Cat Free"
    # ── 免费版独立 Bundle ID ────────────────────────────────────────
    # 不设的话 JUCE 会按 target 名生成 com.XianzhuCatMusic.KickForge，
    # 与付费版完全相同 —— macOS 会把两个插件当成同一个，只加载其一。
    # 🔻 改回付费版：删掉这一行（恢复默认）
    BUNDLE_ID "com.XianzhuCatMusic.KickCatFree"
    DESCRIPTION "XianzhuCatMusic Kick Cat Free — 节点式包络合成底鼓"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    # 构建产物留在 build/ 下，需要时再手动拷贝，避免构建依赖用户目录写权限
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Synth)

juce_generate_juce_header(KickForge)

target_sources(KickForge PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Envelope.cpp
    Source/EnvelopeEditor.cpp
    Source/ModulationMatrix.cpp
    Source/ModulationPanel.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
    Source/SubSynth.cpp
    Source/ClickLayer.cpp
    Source/Distortion.cpp
    Source/MasterChain.cpp
    Source/Equalizer.cpp
    Source/WaveDisplay.cpp
    Source/SampleLibrary.cpp
    Source/EqCurveEditor.cpp
    Source/LevelMeter.cpp
    Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/LlamaBackend.cpp Source/AiChatPanel.cpp
    Source/WavExporter.cpp
    Source/Crypto.cpp
    Source/KickLookAndFeel.cpp
    Source/DspUtils.cpp)

target_include_directories(KickForge PRIVATE Source)

#==============================================================================
# Windows（MinGW 交叉编译）需要的系统库
#
# 缺 wininet → undefined reference to __imp_InternetCloseHandle
# 缺 winmm   → undefined reference to __imp_timeBeginPeriod
# 这两个是 JUCE 的 FallbackDownloadTask / SystemStats 用到的。
#==============================================================================
if (WIN32)
    target_link_libraries(KickForge PRIVATE
        wininet ws2_32 winmm shlwapi version
        ole32 oleaut32 uuid gdi32 user32 comdlg32 comctl32
        imm32 dwmapi d2d1 d3d11 dxgi dcomp
        crypt32 secur32 bcrypt sapi dbghelp)
endif()

#==============================================================================
# 加密库
#
# 本项目不依赖 libsodium：Source/Crypto.cpp 是无外部依赖的实现。
# kf_apply_sodium 保留为空函数，供下文的目标定义统一调用。
#==============================================================================
function(kf_apply_sodium _target)
endfunction()

#==============================================================================
# llama.cpp（本地 LLM 推理）
#
# 为什么不用静态链接：
#   Homebrew 的 llama.cpp 只提供 .dylib。实测把 dylib 放进插件包内
#   一起 ad-hoc 签名后可以正常 dlopen（Team ID 一致），
#   所以采用"内嵌 dylib + @loader_path 相对路径"的方案。
#
# 用 @loader_path 而不是绝对路径，插件才能被拷到别的机器上用。
#==============================================================================
option(KF_ENABLE_LLM "Build with local LLM support (llama.cpp)" ON)

# Universal 构建时自动关闭 LLM。
#
# 原因：Homebrew 的 llama.cpp 是 **arm64-only**，而我们要出
# arm64 + x86_64 的通用包，链接 x86_64 那半边会因缺少 llama 符号失败。
# llama.cpp 体积大、
# 编译慢，暂不为它做 universal。
#
# 结果：Intel Mac 上 AI 音色生成不可用，其余功能完整。
if (CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    message(STATUS "universal build: 关闭本地 LLM（llama.cpp 仅 arm64）")
    set(KF_ENABLE_LLM OFF)
endif()

# 需要内嵌 dylib 的 bundle（AU / VST3）
set(KF_BUNDLE_DIRS
    "$<TARGET_BUNDLE_DIR:KickForge_AU>"
    "$<TARGET_BUNDLE_DIR:KickForge_VST3>")

if (KF_ENABLE_LLM AND APPLE)
    find_path(KF_LLAMA_INCLUDE llama.h
        PATHS /opt/homebrew/include /opt/homebrew/opt/llama.cpp/include /usr/local/include)
    find_library(KF_LLAMA_LIB llama
        PATHS /opt/homebrew/lib /opt/homebrew/opt/llama.cpp/lib /usr/local/lib)
    find_path(KF_GGML_INCLUDE ggml.h
        PATHS /opt/homebrew/opt/ggml/include /opt/homebrew/include)
    find_library(KF_GGML_LIB ggml PATHS /opt/homebrew/opt/ggml/lib /opt/homebrew/lib)
    find_library(KF_GGML_BASE_LIB ggml-base PATHS /opt/homebrew/opt/ggml/lib /opt/homebrew/lib)

    if (KF_LLAMA_INCLUDE AND KF_LLAMA_LIB AND KF_GGML_INCLUDE)
        target_include_directories(KickForge PRIVATE ${KF_LLAMA_INCLUDE} ${KF_GGML_INCLUDE})
        target_link_libraries(KickForge PRIVATE ${KF_LLAMA_LIB} ${KF_GGML_LIB} ${KF_GGML_BASE_LIB})
        # 注意：这里用 KF_LLM_COMPILED。
        #
        # 工具目标会通过 $<REMOVE_ITEM:$<TARGET_PROPERTY:KickForge,COMPILE_DEFINITIONS>,KF_LLM_COMPILED=1>
        # 复制插件的编译定义（PUBLIC 和 PRIVATE 都会出现在这个属性里），
        # 所以无论 PUBLIC 还是 PRIVATE，工具都会拿到该宏 ——
        # 而工具并不链接 llama.cpp，编译 LlamaBackend.cpp 时会找不到 llama.h。
        #
        # 解决办法：让守卫宏只在**插件目标**上单独定义一次，
        # 且这个名字不会被上面的继承机制带出去（下面把继承改成剔除它）。
        target_compile_definitions(KickForge PRIVATE KF_LLM_COMPILED=1)

        # 把 dylib 装进插件的 Contents/Frameworks，
        # 并把安装名改成 @loader_path 相对路径
        set(KF_LLAMA_DYLIBS
            ${KF_LLAMA_LIB}
            ${KF_GGML_LIB}
            ${KF_GGML_BASE_LIB})

        # 关键：必须把链接路径改成相对 @loader_path，
        # 否则插件只在装了 Homebrew 的机器上能用 ——
        # 可执行文件里存的是 /opt/homebrew/... 绝对路径。
                # 把 dylib 拷进 bundle 的 Contents/Frameworks。
        #
        # 注意这里**没有**把链接路径改成 @loader_path：
        # 当前阶段只在本机（已装 Homebrew）使用，绝对路径可以正常工作。
        # 将来要做通用分发时，需要加上 install_name_tool 改相对路径，
        # 并处理签名 —— 那是一项独立工作。
        foreach(_target KickForge_AU KickForge_VST3)
            foreach(_lib ${KF_LLAMA_DYLIBS})
                get_filename_component(_real ${_lib} REALPATH)
                get_filename_component(_realname ${_real} NAME)

                add_custom_command(TARGET ${_target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${_real}
                            "$<TARGET_BUNDLE_DIR:${_target}>/Contents/Frameworks/${_realname}"
                    COMMENT "Embed ${_realname} -> ${_target}"
                    VERBATIM)
            endforeach()
        endforeach()

    else()
        message(WARNING "llama.cpp not found - building without local LLM support")
    endif()
endif()

target_compile_definitions(KickForge
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(KickForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        # 预设文件摘要校验
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# 符号剥离
#
# 目的：减小二进制体积、加快加载。
# 未剥离时 `nm` 能列出所有内部符号名。
#
# `-Wl,-x` 去掉局部符号（local symbols），保留导出符号 ——
# 插件的入口点（AU/VST3 的工厂函数）必须保留，否则宿主加载不了。
#
# 注意：这是**提高门槛**，不是安全措施。控制流本身还在，
# 有经验的人仍可用字符串交叉引用和模式匹配定位关键逻辑。
# ---------------------------------------------------------------------------
if (APPLE AND CMAKE_BUILD_TYPE STREQUAL "Release")
    # juce_add_plugin 会为每种格式生成 KickForge_AU / KickForge_VST3 等目标，
    # 真正的链接发生在那几个目标上，所以要分别设置。
    foreach(_fmt AU VST3 Standalone)
        if (TARGET KickForge_${_fmt})
            target_link_options(KickForge_${_fmt} PRIVATE
                "LINKER:-x"
                "LINKER:-S")
        endif()
    endforeach()
endif()


# 自测工具目标需要插件的编译定义（JUCE 配置、插件元数据等），
# 但**不能**要 KF_LLM_COMPILED —— 工具不链接 llama.cpp，
# 而 LlamaBackend.cpp 靠这个宏在"真实现 / 空实现"之间切换。
#
# 为什么不用 $<TARGET_PROPERTY:...> 直接继承：
# 它会把 PRIVATE 定义一并带出，而 CMake **没有**能在生成表达式里
# 剔除单个条目的写法（$<REMOVE_ITEM> 不是合法生成表达式，实测报错）。
#
# 为什么不用 separate_arguments 过滤：
# 它会把 JucePlugin_Name="Kick Cat" 的引号吃掉，变成
# JucePlugin_Name=Kick Cat，编译时报 "use of undeclared identifier 'Kick'"。
#
# 所以用 string(REPLACE) 直接对**字符串**做替换 —— 保留原有格式。
get_target_property(_kf_plugin_defs KickForge COMPILE_DEFINITIONS)

set(KF_TOOL_DEFINES "${_kf_plugin_defs}")
string(REPLACE "KF_LLM_COMPILED=1" "" KF_TOOL_DEFINES "${KF_TOOL_DEFINES}")
string(REPLACE ";;" ";" KF_TOOL_DEFINES "${KF_TOOL_DEFINES}")
string(REGEX REPLACE "^;|;$" "" KF_TOOL_DEFINES "${KF_TOOL_DEFINES}")

if (KF_TOOL_DEFINES MATCHES "KF_LLM_COMPILED")
    message(WARNING "KF_LLM_COMPILED 未被过滤掉")
else()
    message(STATUS "工具目标的编译定义已过滤 KF_LLM_COMPILED")
endif()

# ---------------------------------------------------------------------------
# 离线自检工具：不依赖宿主导入 DSP，验证各项功能真实生效。
# 用 -DKF_BUILD_TOOLS=ON 打开。
# ---------------------------------------------------------------------------
option(KF_BUILD_TOOLS "Build KickForge offline self-test tools" ON)

if (KF_BUILD_TOOLS)
    add_executable(KickForgeSelfTest
        Tools/SelfTest_main.cpp
        Source/Envelope.cpp
        Source/SubSynth.cpp
        Source/ClickLayer.cpp
        Source/Distortion.cpp
        Source/MasterChain.cpp
        Source/Equalizer.cpp
        Source/SampleLibrary.cpp
        Source/DspUtils.cpp)

    target_include_directories(KickForgeSelfTest PRIVATE Source)

    target_compile_definitions(KickForgeSelfTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(KickForgeSelfTest PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# 完整插件链路测试：直接实例化 KickForgeProcessor，端到端验证。
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgePluginTest
        Tools/PluginChain_main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Envelope.cpp
        Source/EnvelopeEditor.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/SubSynth.cpp
        Source/ClickLayer.cpp
        Source/Distortion.cpp
        Source/MasterChain.cpp
        Source/Equalizer.cpp
        Source/WaveDisplay.cpp
        Source/SampleLibrary.cpp
        Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp
        Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp
        Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp
        Source/DspUtils.cpp)

    target_include_directories(KickForgePluginTest PRIVATE Source)

    target_compile_definitions(KickForgePluginTest PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Kick Cat"
        JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic"
        JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0)

    target_link_libraries(KickForgePluginTest PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp)

    # 复用插件目标生成的 JuceHeader / 配置定义
    target_compile_definitions(KickForgePluginTest PRIVATE
        ${KF_TOOL_DEFINES})

endif()

# ---------------------------------------------------------------------------
# UI 截图工具：验证编辑器真实可绘制。
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgeUIShot
        Tools/UIShot_main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Envelope.cpp
        Source/EnvelopeEditor.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/SubSynth.cpp
        Source/ClickLayer.cpp
        Source/Distortion.cpp
        Source/MasterChain.cpp
        Source/Equalizer.cpp
        Source/WaveDisplay.cpp
        Source/SampleLibrary.cpp
        Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp
        Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp
        Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp
        Source/DspUtils.cpp)

    target_include_directories(KickForgeUIShot PRIVATE Source)

    target_compile_definitions(KickForgeUIShot PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="Kick Cat"
        JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic"
        JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(KickForgeUIShot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp)

    target_compile_definitions(KickForgeUIShot PRIVATE
        ${KF_TOOL_DEFINES})

endif()

# ---------------------------------------------------------------------------
# EQ 曲线交互测试：模拟真实鼠标事件，验证拖动/滚轮真的改变参数。
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgeEqTest
        Tools/EqInteraction_main.cpp
        Source/EqCurveEditor.cpp
        Source/Equalizer.cpp)

    target_include_directories(KickForgeEqTest PRIVATE Source)

    target_compile_definitions(KickForgeEqTest PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(KickForgeEqTest PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp)
endif()

if (KF_BUILD_TOOLS)
    add_executable(KickForgePresetCheck
        Tools/PresetCheck_main.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Envelope.cpp
        Source/EnvelopeEditor.cpp Source/SubSynth.cpp Source/ClickLayer.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/Distortion.cpp Source/MasterChain.cpp Source/Equalizer.cpp
        Source/WaveDisplay.cpp Source/SampleLibrary.cpp Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp Source/DspUtils.cpp)
    target_include_directories(KickForgePresetCheck PRIVATE Source)
    target_compile_definitions(KickForgePresetCheck PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Kick Cat" JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic" JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01 JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(KickForgePresetCheck PRIVATE juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(KickForgePresetCheck PRIVATE ${KF_TOOL_DEFINES})
endif()

# ---------------------------------------------------------------------------
# WAV 导出 + 预设库测试
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgeExportTest
        Tools/ExportTest_main.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Envelope.cpp
        Source/EnvelopeEditor.cpp Source/SubSynth.cpp Source/ClickLayer.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/Distortion.cpp Source/MasterChain.cpp Source/Equalizer.cpp
        Source/WaveDisplay.cpp Source/SampleLibrary.cpp Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp Source/DspUtils.cpp)
    target_include_directories(KickForgeExportTest PRIVATE Source)
    target_compile_definitions(KickForgeExportTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Kick Cat" JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic" JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01 JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(KickForgeExportTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(KickForgeExportTest PRIVATE ${KF_TOOL_DEFINES})
endif()


# ---------------------------------------------------------------------------
# 压力 / 边界测试（Bug 搜索）
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgeStressTest
        Tools/StressTest_main.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Envelope.cpp
        Source/EnvelopeEditor.cpp Source/SubSynth.cpp Source/ClickLayer.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/Distortion.cpp Source/MasterChain.cpp Source/Equalizer.cpp
        Source/WaveDisplay.cpp Source/SampleLibrary.cpp Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp Source/DspUtils.cpp)
    target_include_directories(KickForgeStressTest PRIVATE Source)
    target_compile_definitions(KickForgeStressTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Kick Cat" JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic" JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01 JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(KickForgeStressTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(KickForgeStressTest PRIVATE ${KF_TOOL_DEFINES})
endif()



# ---------------------------------------------------------------------------
# 波形选择功能测试
# ---------------------------------------------------------------------------
if (KF_BUILD_TOOLS)
    add_executable(KickForgeWaveTest
        Tools/WaveTest_main.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Envelope.cpp
        Source/EnvelopeEditor.cpp Source/SubSynth.cpp Source/ClickLayer.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/Distortion.cpp Source/MasterChain.cpp Source/Equalizer.cpp
        Source/WaveDisplay.cpp Source/SampleLibrary.cpp Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp Source/DspUtils.cpp)
    target_include_directories(KickForgeWaveTest PRIVATE Source)
    target_compile_definitions(KickForgeWaveTest PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Kick Cat" JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic" JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01 JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(KickForgeWaveTest PRIVATE juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(KickForgeWaveTest PRIVATE ${KF_TOOL_DEFINES})
endif()















if (KF_BUILD_TOOLS)
    add_executable(KickForgeBugHunt Tools/BugHunt_main.cpp
        Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Envelope.cpp
        Source/EnvelopeEditor.cpp Source/SubSynth.cpp Source/ClickLayer.cpp
        Source/ModulationMatrix.cpp
        Source/ModulationPanel.cpp
        Source/Distortion.cpp Source/MasterChain.cpp Source/Equalizer.cpp
        Source/WaveDisplay.cpp Source/SampleLibrary.cpp Source/EqCurveEditor.cpp
        Source/LevelMeter.cpp Source/PresetManager.cpp Source/FactoryPresets.cpp Source/AiPatch.cpp Source/LocalPatchEngine.cpp Source/AiApplier.cpp Source/AiConfig.cpp Source/ModelManager.cpp Source/ChatBackend.cpp Source/AiChatPanel.cpp Source/LlamaBackend.cpp Source/WavExporter.cpp
        Source/KickLookAndFeel.cpp Source/DspUtils.cpp)
    target_include_directories(KickForgeBugHunt PRIVATE Source)
    target_compile_definitions(KickForgeBugHunt PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="Kick Cat" JucePlugin_CompanyName="XianzhuCatMusic"
        JucePlugin_Manufacturer="XianzhuCatMusic" JucePlugin_ManufacturerCode=Xzzs
        JucePlugin_PluginCode=KF01 JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(KickForgeBugHunt PRIVATE juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(KickForgeBugHunt PRIVATE ${KF_TOOL_DEFINES})
endif()
































