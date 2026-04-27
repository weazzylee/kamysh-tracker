cmake_minimum_required(VERSION 3.24)

project(kamyshtracker VERSION 2.0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KAMYSHTRACKER_STANDALONE "Build without OBS plugin helper macros" OFF)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network WebSockets)

find_package(libobs QUIET)
find_package(obs-frontend-api QUIET)

set(OBS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-30.0.0" CACHE PATH "OBS source directory for headers")
set(OBS_IMPORT_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-import-libs" CACHE PATH "OBS generated import libraries")

if(NOT TARGET OBS::libobs)
    add_library(OBS::libobs UNKNOWN IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_LOCATION "${OBS_IMPORT_LIB_DIR}/obs.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/obs-generated;${OBS_SOURCE_DIR}/libobs"
    )
endif()

if(NOT TARGET OBS::obs-frontend-api)
    set(OBS_FRONTEND_API_INCLUDE_DIR "")
    if(EXISTS "${OBS_SOURCE_DIR}/UI/obs-frontend-api/obs-frontend-api.h")
        set(OBS_FRONTEND_API_INCLUDE_DIR "${OBS_SOURCE_DIR}/UI/obs-frontend-api")
    elseif(EXISTS "${OBS_SOURCE_DIR}/frontend/api/obs-frontend-api.h")
        set(OBS_FRONTEND_API_INCLUDE_DIR "${OBS_SOURCE_DIR}/frontend/api")
    else()
        message(FATAL_ERROR
            "Could not find obs-frontend-api.h. Checked "
            "${OBS_SOURCE_DIR}/UI/obs-frontend-api and ${OBS_SOURCE_DIR}/frontend/api")
    endif()

    add_library(OBS::obs-frontend-api UNKNOWN IMPORTED)
    set_target_properties(OBS::obs-frontend-api PROPERTIES
        IMPORTED_LOCATION "${OBS_IMPORT_LIB_DIR}/obs-frontend-api.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${OBS_FRONTEND_API_INCLUDE_DIR}"
    )
endif()

add_library(kamyshtracker MODULE
    include/media-monitor.hpp
    include/media-state.hpp
    include/settings-dialog.hpp
    include/settings.hpp
    include/twitch-client.hpp
    src/media-monitor.cpp
    src/plugin-main.cpp
    src/settings-dialog.cpp
    src/settings.cpp
    src/twitch-client.cpp
)

target_include_directories(kamyshtracker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(kamyshtracker PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    windowsapp
    winhttp
    ws2_32
    crypt32
    bcrypt
)

target_compile_definitions(kamyshtracker PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    KAMYSHTRACKER_VERSION="${PROJECT_VERSION}"
)

set_target_properties(kamyshtracker PROPERTIES
    PREFIX ""
    OUTPUT_NAME "kamyshtracker"
)

install(TARGETS kamyshtracker
    RUNTIME DESTINATION obs-plugins/64bit
    LIBRARY DESTINATION obs-plugins/64bit
)

install(DIRECTORY data/
    DESTINATION data/obs-plugins/kamyshtracker
)
