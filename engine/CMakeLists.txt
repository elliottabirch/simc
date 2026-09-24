##################
# 'engine' library
##################

# define source files
include(../source_files/cmake_engine.txt)

# Define engine library
add_library(engine ${source_files})
target_include_directories(engine PUBLIC . ./include ./lib)
sc_common_compiler_options(engine)

# Make cmake selections visible to C++ code
if(SC_NO_THREADING)
    target_compile_definitions(engine PUBLIC SC_NO_THREADING)
endif()
if(SC_NO_NETWORKING)
    target_compile_definitions(engine PUBLIC SC_NO_NETWORKING)
endif()

# Detect pthreads
if(NOT SC_NO_THREADING)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(engine Threads::Threads)
endif()

# Networking libs
if(NOT SC_NO_NETWORKING)
    if(NOT WIN32)
        find_package(CURL REQUIRED)
        if (CURL_FOUND)
            target_link_libraries(engine ${CURL_LIBRARIES})
            target_include_directories(engine PUBLIC ${CURL_INCLUDE_DIRS})
        else(CURL_FOUND)
            message(FATAL_ERROR "libcurl not found")
        endif()
    else()
        target_link_libraries(engine crypt32)
        target_link_libraries(engine wininet)
    endif()
endif()

# 260924-tlz (write-time zstd for the RL translog, owner ruling 2026-09-24, R-TLZ): the RL
# translog writer (rl_translog.cpp) streams every row through libzstd rather than a raw fwrite.
# Prefer the package's own CMake config (exports the zstd::libzstd target); fall back to
# pkg-config's libzstd.pc when no CMake config is installed. Deliberately NO silent fallback to
# an uncompressed writer if zstd cannot be found -- configure FAILS loudly instead, per the same
# "compressed output is the only writer path, no opt-out" rule the fork side itself follows.
find_package(zstd CONFIG QUIET)
if(zstd_FOUND)
    target_link_libraries(engine zstd::libzstd)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
    target_link_libraries(engine PkgConfig::ZSTD)
endif()

if(SC_EVENT_QUEUE_DEBUG)
    target_compile_definitions(engine PUBLIC EVENT_QUEUE_DEBUG)
endif()

target_compile_definitions(engine PRIVATE "SC_GIT_REV=\"${GIT_COMMIT_HASH}\"" "SC_GIT_BRANCH=\"${GIT_BRANCH}\"")

add_custom_target(file_toucher
        COMMAND ${CMAKE_COMMAND} -E touch_nocreate ${CMAKE_CURRENT_SOURCE_DIR}/util/git_info.cpp)

add_dependencies(engine file_toucher)

# Default Blizzard Community Platform API key
if (DEFINED SC_DEFAULT_APIKEY)
  target_compile_definitions(engine PRIVATE "SC_DEFAULT_APIKEY=\"${SC_DEFAULT_APIKEY}\"")
endif()
