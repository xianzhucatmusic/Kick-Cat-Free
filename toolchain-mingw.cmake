#==============================================================================
# MinGW-w64 交叉编译工具链（macOS → Windows x64）
#
# 用法：
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=toolchain-mingw.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#==============================================================================

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /opt/homebrew/${TOOLCHAIN_PREFIX})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 静态链接 libgcc / libstdc++ / winpthread，成品不依赖 MinGW 运行时 DLL
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

#==============================================================================
# g++ 16 兼容：JUCE 8.0.4 没有显式 include <cstring> / <cstdio>，
# GCC 16 收紧后 memset/strlen/memcpy 都找不到（实测 84 处 error）。
# 强制预包含这两个头文件。
#==============================================================================
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -include cstring -include cstdio -include cstdlib")
set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT} -include string.h -include stdio.h")
