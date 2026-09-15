set(RELAY_VELOPACK_VERSION 1.2.0)
include(FetchContent)
FetchContent_Declare(velopack
  URL "https://github.com/velopack/velopack/releases/download/${RELAY_VELOPACK_VERSION}/velopack_libc_${RELAY_VELOPACK_VERSION}.zip"
  URL_HASH SHA256=547262ed7a1ab1ff62f580aa53851ede2f1a451ac61b8974eb7bc01117488835
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(velopack)
add_library(velopack_sdk SHARED IMPORTED)
set_target_properties(velopack_sdk PROPERTIES
  IMPORTED_IMPLIB "${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll.lib"
  IMPORTED_LOCATION "${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${velopack_SOURCE_DIR}/include")
file(WRITE "${CMAKE_BINARY_DIR}/velopack-version.txt" "${RELAY_VELOPACK_VERSION}\n")
