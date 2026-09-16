cmake_minimum_required(VERSION 3.25)
project(relay VERSION 0.2.2 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(sol2 CONFIG REQUIRED)
include(cmake/Velopack.cmake)

add_library(relay_core STATIC
  src/config.cpp
  src/config_reference.cpp
  src/watcher.cpp
  src/fuzzy.cpp
  src/util.cpp
  src/frecency.cpp
  src/notices.cpp
  src/command.cpp
  src/engine.cpp
  src/app_command.cpp
  src/relay_command.cpp
  src/window_command.cpp
  src/process_command.cpp
  src/lua_commands.cpp
  src/desktop.cpp
  src/updates.cpp
  src/velopack_updates.cpp
)
target_include_directories(relay_core PUBLIC src ${LUA_INCLUDE_DIR})
target_link_libraries(relay_core PUBLIC ${LUA_LIBRARIES} sol2::sol2 velopack_sdk
  shell32 ole32 user32 urlmon shlwapi propsys shcore gdi32 uuid dwmapi winhttp)
target_compile_definitions(relay_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN SOL_ALL_SAFETIES_ON=1)

add_executable(relay WIN32
  src/main.cpp
  src/app.cpp
  src/theme.cpp
  src/icons.cpp
  src/relay.rc
)

target_include_directories(relay PRIVATE src ${LUA_INCLUDE_DIR})
target_link_libraries(relay PRIVATE relay_core imgui::imgui
  d3d11 dxgi d3dcompiler dcomp dwmapi shell32 ole32 shlwapi propsys shcore gdi32 user32 uuid urlmon)
target_compile_definitions(relay PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  SOL_ALL_SAFETIES_ON=1 RELAY_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(relay_core PRIVATE /W3 /utf-8 /bigobj /MP)
  target_compile_options(relay PRIVATE /W3 /utf-8 /bigobj /MP)
  target_link_options(relay PRIVATE /ENTRY:wWinMainCRTStartup)
endif()

add_custom_command(TARGET relay POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:relay>/assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:relay>/plugins)

# One headless executable; CTest selects each suite independently.
add_executable(relay_test
  tests/main.cpp
  tests/support.cpp
  tests/config.cpp
  tests/watcher.cpp
  tests/commands.cpp
  tests/datetime.cpp
  tests/frecency.cpp
  tests/updates.cpp
)
target_link_libraries(relay_test PRIVATE relay_core)
target_compile_definitions(relay_test PRIVATE RELAY_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(relay_test PRIVATE /W3 /utf-8 /bigobj /MP)
endif()
enable_testing()
foreach(suite support config watcher commands datetime frecency updates)
  add_test(NAME ${suite} COMMAND relay_test ${suite})
endforeach()

# Velopack packs this install tree into the installer and portable distribution.
install(TARGETS relay RUNTIME DESTINATION .)
install(FILES $<TARGET_FILE:velopack_sdk> DESTINATION . RENAME velopack_libc.dll)
install(DIRECTORY assets plugins licenses DESTINATION .)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${PROJECT_VERSION}\n")
install(FILES "${CMAKE_BINARY_DIR}/version.txt" DESTINATION .)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)
