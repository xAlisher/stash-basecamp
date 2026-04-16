cmake_minimum_required(VERSION 3.28)
project(stash-basecamp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ── Qt ───────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick QuickControls2)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# ── libsodium (needed by logos SDK headers) ───────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium)

# ── Nix store paths ───────────────────────────────────────────────────────────
if(DEFINED ENV{LOGOS_LIBLOGOS_HEADERS})
    set(LOGOS_LIBLOGOS_HEADERS "$ENV{LOGOS_LIBLOGOS_HEADERS}")
else()
    set(LOGOS_LIBLOGOS_HEADERS
        "/nix/store/092zxk8qbm9zxqigq1z0a5l901a068cz-logos-liblogos-headers-0.1.0/include")
endif()

if(DEFINED ENV{LOGOS_CPP_SDK_ROOT})
    set(LOGOS_CPP_SDK "$ENV{LOGOS_CPP_SDK_ROOT}")
else()
    set(LOGOS_CPP_SDK
        "/nix/store/047dmhc4gi7yib02i1fbwidxpksqvcc2-logos-cpp-sdk")
endif()

# ── logos-storage-nim static library ─────────────────────────────────────────
set(LOGOS_STORAGE_DIR "${CMAKE_SOURCE_DIR}/third_party/logos-storage-nim")

# ── Core plugin sources ───────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/plugin/StashPlugin.h
    src/plugin/StashPlugin.cpp
    src/core/StashBackend.h
    src/core/StashBackend.cpp
    src/core/LibStorageTransport.h
    src/core/LibStorageTransport.cpp
    src/core/StorageClient.h
    src/core/StorageClient.cpp
)

add_library(stash_plugin MODULE ${PLUGIN_SOURCES})

set_target_properties(stash_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "stash_plugin"
    COMPILE_OPTIONS ""
)

target_include_directories(stash_plugin PRIVATE
    src
    "${LOGOS_STORAGE_DIR}/include"
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)

target_link_libraries(stash_plugin PRIVATE
    Qt6::Core
    Qt6::Qml
    "${LOGOS_STORAGE_DIR}/lib/libstorage.a"
    "${LOGOS_STORAGE_DIR}/lib/libbacktrace.a"
    "${LOGOS_STORAGE_DIR}/lib/libminiupnpc.a"
    "${LOGOS_STORAGE_DIR}/lib/libnatpmp.a"
    dl pthread
)

configure_file(
    src/plugin/plugin_metadata.json
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin/plugin_metadata.json
    COPYONLY
)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

set(TEST_COMMON_SOURCES
    src/core/StashBackend.h
    src/core/StashBackend.cpp
    src/core/StorageClient.h
    src/core/StorageClient.cpp
)

set(TEST_COMMON_LIBS
    Qt6::Core
    Qt6::Test
)

# StashBackend unit tests — mock transport, no real node.
add_executable(test_stash_backend
    tests/test_stash_backend.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_stash_backend PRIVATE src)
target_link_libraries(test_stash_backend PRIVATE ${TEST_COMMON_LIBS})
add_test(NAME test_stash_backend COMMAND test_stash_backend)

# StashPlugin unit tests — no libstorage.a linkage (initLogos never called).
add_executable(test_stash_plugin
    tests/test_stash_plugin.cpp
    tests/logos_api_stub.cpp
    tests/nim_runtime_stub.cpp
    "${LOGOS_CPP_SDK}/include/cpp/logos_api.h"
    src/plugin/StashPlugin.h
    src/plugin/StashPlugin.cpp
    src/core/LibStorageTransport.h
    src/core/LibStorageTransport.cpp
    ${TEST_COMMON_SOURCES}
)
target_include_directories(test_stash_plugin PRIVATE
    src
    "${LOGOS_STORAGE_DIR}/include"
    "${LOGOS_LIBLOGOS_HEADERS}"
    "${LOGOS_CPP_SDK}/include"
    "${LOGOS_CPP_SDK}/include/cpp"
    "${LOGOS_CPP_SDK}/include/core"
)
target_link_libraries(test_stash_plugin PRIVATE
    ${TEST_COMMON_LIBS}
    "${LOGOS_STORAGE_DIR}/lib/libstorage.a"
    "${LOGOS_STORAGE_DIR}/lib/libbacktrace.a"
    "${LOGOS_STORAGE_DIR}/lib/libminiupnpc.a"
    "${LOGOS_STORAGE_DIR}/lib/libnatpmp.a"
    dl pthread
)
add_test(NAME test_stash_plugin COMMAND test_stash_plugin)

# ── Install ───────────────────────────────────────────────────────────────────
set(LOGOS_APP_DATA "$ENV{HOME}/.local/share/Logos/LogosApp")
set(STASH_MODULE_DIR "${LOGOS_APP_DATA}/modules/stash")
set(STASH_UI_DIR     "${LOGOS_APP_DATA}/plugins/stash_ui")

install(CODE "
    set(_modules_dir \"\\$ENV{DESTDIR}${LOGOS_APP_DATA}/modules\")
    file(GLOB _old \"\\${_modules_dir}/stash.*\")
    foreach(_dir \\${_old})
        file(REMOVE_RECURSE \"\\${_dir}\")
    endforeach()
")

install(TARGETS stash_plugin LIBRARY DESTINATION "${STASH_MODULE_DIR}")
install(FILES modules/stash/manifest.json DESTINATION "${STASH_MODULE_DIR}")
install(FILES
    plugins/stash_ui/manifest.json
    plugins/stash_ui/metadata.json
    plugins/stash_ui/Main.qml
    DESTINATION "${STASH_UI_DIR}"
)

find_program(PATCHELF_EXECUTABLE patchelf)
if(PATCHELF_EXECUTABLE)
    install(CODE "
        set(_plugin \"${STASH_MODULE_DIR}/stash_plugin.so\")
        set(_build_so \"${CMAKE_BINARY_DIR}/stash_plugin.so\")
        if(EXISTS \"\\${_plugin}\")
            execute_process(
                COMMAND bash -c \"ldd '\\${_build_so}' | grep nix/store | awk '{print \\\\\\$3}' | xargs -I{} dirname {} | sort -u | tr '\\\\n' ':' | sed 's/:$//'\"
                OUTPUT_VARIABLE _nix_paths
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            set(_rpath \"\\\\\\$ORIGIN:\\${_nix_paths}:/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu\")
            execute_process(
                COMMAND ${PATCHELF_EXECUTABLE} --set-rpath \"\\${_rpath}\" \"\\${_plugin}\"
            )
            message(STATUS \"Patched RUNPATH on stash_plugin.so\")
        endif()
    ")
endif()

# Mirror to LogosBasecamp
install(CODE "
    set(_mod_src \"${STASH_MODULE_DIR}\")
    set(_mod_dst \"\\$ENV{HOME}/.local/share/Logos/LogosBasecamp/modules/stash\")
    file(COPY \"\\${_mod_src}/\" DESTINATION \"\\${_mod_dst}\")

    set(_ui_src \"${STASH_UI_DIR}\")
    set(_ui_dst \"\\$ENV{HOME}/.local/share/Logos/LogosBasecamp/plugins/stash_ui\")
    file(GLOB _ui_files \"\\${_ui_src}/*\")
    foreach(_f \\${_ui_files})
        file(COPY \"\\${_f}\" DESTINATION \"\\${_ui_dst}\")
    endforeach()
    message(STATUS \"Mirror to LogosBasecamp complete\")
")
