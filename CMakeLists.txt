cmake_minimum_required(VERSION 3.14)
project(StashPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found — set LOGOS_MODULE_BUILDER_ROOT")
endif()

logos_module(
    NAME stash
    SOURCES
        src/core/StashBackend.h
        src/core/StashBackend.cpp
        src/core/StorageClient.h
        src/core/StorageClient.cpp
        src/core/LogosStorageTransport.cpp
        src/core/PinningClient.h
        src/core/PinningClient.cpp
        src/plugin/StashPlugin.h
        src/plugin/StashPlugin.cpp
    INCLUDE_DIRS
        src
        src/generated
)
