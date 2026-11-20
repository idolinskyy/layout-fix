cmake_minimum_required(VERSION 3.16)
project(layout-fix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

add_executable(layout-fix
  src/main.cpp
  src/app.cpp
  src/config.cpp
  src/log.cpp
  src/utf8.cpp
  src/x11/connection.cpp
  src/x11/listener.cpp
  src/x11/focus.cpp
  src/x11/retype.cpp
  src/input/word_buffer.cpp
  src/layout/groups.cpp
  src/layout/cinnamon.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp)

target_include_directories(layout-fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(layout-fix PRIVATE
  LAYOUT_FIX_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/layout-fix")
target_compile_options(layout-fix PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(layout-fix PRIVATE
  X11::X11
  ${X11_Xtst_LIB}
  PkgConfig::XKBCOMMON
  PkgConfig::GIO)

add_executable(layout-fix-test
  tests/test_core.cpp
  src/utf8.cpp
  src/input/word_buffer.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp)
target_include_directories(layout-fix-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(layout-fix-test PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME core COMMAND layout-fix-test)

include(GNUInstallDirs)
install(TARGETS layout-fix RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/layout-fix.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/layout-fix.service DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
install(FILES config/layout-fix.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/layout-fix)
install(FILES data/dicts/uk_UA.dic data/dicts/uk_UA.COPYRIGHT
        DESTINATION ${CMAKE_INSTALL_DATADIR}/layout-fix)
