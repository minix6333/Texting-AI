# Cross-compiling for Windows with MinGW-w64 (i686), e.g. on macOS: brew install mingw-w64
#   cmake -B build-i686 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
