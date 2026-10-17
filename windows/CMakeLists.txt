cmake_minimum_required(VERSION 3.21)
project(ZhuyinLLM LANGUAGES C CXX)

# ZhuyinLLM for Windows
#   core/    portable engine: Zhuyin, dictionary, lattice, fusion, composition, English correction
#   engine/  LLM scorer (llama.cpp), engine, key-handling sessions, protocol
#   server/  zhuyinllm-server.exe (named pipe)            — Windows
#   tsf/     ZhuyinLLM-x64.dll / ZhuyinLLM-x86.dll (TSF)  — Windows
#   tools/, tests/  imecli and session_test (any platform)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # no VC++ redistributable needed

option(ZL_BUILD_TSF "Build the text service DLL (Windows)" ${WIN32})
option(ZL_BUILD_ENGINE "Build the engine (server, imecli, tests; needs llama.cpp)" ON)
option(ZL_LLAMA_FROM_SOURCE "Fetch and build llama.cpp instead of using an installed one" ${WIN32})
set(ZL_LLAMA_TAG "v0.7.0" CACHE STRING "llama.cpp release to build")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ZL_ARCH x64)
else()
  set(ZL_ARCH x86)
endif()

if(MINGW)
  # one self-contained file each: the DLL is loaded into every app
  add_link_options(-static -static-libgcc -static-libstdc++ -s)
endif()
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/utf-8> $<$<COMPILE_LANGUAGE:C,CXX>:/W3>)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

add_library(zl_core STATIC
  core/text.cpp core/zhuyin.cpp core/lexicon.cpp core/typo_model.cpp core/lattice.cpp
  core/composition.cpp core/english.cpp core/json.cpp)
target_include_directories(zl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(ZL_BUILD_ENGINE)
  if(ZL_LLAMA_FROM_SOURCE)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)       # runs on any x64 CPU with AVX2 (2013+)
    set(GGML_AVX2 ON CACHE BOOL "" FORCE)
    set(GGML_FMA ON CACHE BOOL "" FORCE)
    set(GGML_F16C ON CACHE BOOL "" FORCE)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(llama
      GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
      GIT_TAG ${ZL_LLAMA_TAG}
      GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(llama)
  else()
    find_package(llama CONFIG REQUIRED)
  endif()
  find_package(Threads REQUIRED)

  add_library(zl_engine STATIC engine/scorer.cpp engine/engine.cpp engine/session.cpp engine/service.cpp)
  target_link_libraries(zl_engine PUBLIC zl_core llama Threads::Threads)

  add_executable(imecli tools/imecli.cpp)
  target_link_libraries(imecli PRIVATE zl_engine)
  add_executable(session_test tests/session_test.cpp)
  target_link_libraries(session_test PRIVATE zl_engine)
  enable_testing()
  add_test(NAME session COMMAND session_test ${CMAKE_CURRENT_SOURCE_DIR}/../data)

  if(WIN32)
    add_executable(zhuyinllm-server WIN32 server/main_win.cpp)
    target_compile_definitions(zhuyinllm-server PRIVATE UNICODE _UNICODE)
    target_link_libraries(zhuyinllm-server PRIVATE zl_engine advapi32 shell32 ole32)
    if(MINGW)
      target_link_options(zhuyinllm-server PRIVATE -municode)
    endif()
    add_executable(pipe_test tests/pipe_test.cpp tsf/pipe_client.cpp)
    target_compile_definitions(pipe_test PRIVATE UNICODE _UNICODE)
    target_link_libraries(pipe_test PRIVATE zl_core advapi32)
  endif()
endif()

if(WIN32 AND ZL_BUILD_TSF)
  add_library(zhuyinllm_tsf SHARED
    tsf/dllmain.cpp tsf/text_service.cpp tsf/pipe_client.cpp tsf/ui.cpp tsf/langbar.cpp
    tsf/display_attributes.cpp tsf/zhuyinllm.rc tsf/zhuyinllm.def core/json.cpp)
  target_include_directories(zhuyinllm_tsf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(zhuyinllm_tsf PRIVATE UNICODE _UNICODE)
  set_target_properties(zhuyinllm_tsf PROPERTIES OUTPUT_NAME "ZhuyinLLM-${ZL_ARCH}" PREFIX "")
  target_link_libraries(zhuyinllm_tsf PRIVATE ole32 oleaut32 uuid advapi32 user32 gdi32)
  if(MINGW AND ZL_ARCH STREQUAL "x86")
    target_link_options(zhuyinllm_tsf PRIVATE -Wl,--enable-stdcall-fixup)
  endif()
endif()
