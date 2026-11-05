# Cross-compile macOS arm64 (Apple Silicon) game modules from Linux with zig
# (pip install ziglang; wrappers in tools/mac). Only the mod's modules - the
# engine is the official OpenJK app.
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR arm64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../../tools/mac/zig-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../../tools/mac/zig-c++)
set(CMAKE_OSX_ARCHITECTURES "")
set(CMAKE_OSX_SYSROOT "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "")
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/../../tools/mac/zig-ar)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/../../tools/mac/zig-ranlib)
set(CMAKE_INSTALL_NAME_DIR "@rpath")
set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
