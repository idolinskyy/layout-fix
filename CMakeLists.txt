cmake_minimum_required(VERSION 3.16)
set(VERSION 0.3.0 CACHE STRING "layout-fix package version")
project(layout-fix VERSION ${VERSION} LANGUAGES CXX)

include(GNUInstallDirs)
file(WRITE "${CMAKE_BINARY_DIR}/layout-fix-version.txt" "${PROJECT_VERSION}\n")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(HUNSPELL QUIET IMPORTED_TARGET hunspell)
option(LAYOUT_FIX_REQUIRE_HUNSPELL "Fail if Hunspell development files are unavailable" OFF)
if(LAYOUT_FIX_REQUIRE_HUNSPELL AND NOT HUNSPELL_FOUND)
  message(FATAL_ERROR "Hunspell development files are required for the release")
endif()
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6Widgets_FOUND)
  set(_layout_fix_qt_prefixes "")
  if(DEFINED ENV{QT_PREFIX} AND NOT "$ENV{QT_PREFIX}" STREQUAL "")
    list(APPEND _layout_fix_qt_prefixes "$ENV{QT_PREFIX}")
  endif()
  file(GLOB _layout_fix_local_qt6 "$ENV{HOME}/Qt/6.*/gcc_64")
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.18)
    list(SORT _layout_fix_local_qt6 COMPARE NATURAL ORDER DESCENDING)
  else()
    list(SORT _layout_fix_local_qt6 ORDER DESCENDING)
  endif()
  list(APPEND _layout_fix_qt_prefixes ${_layout_fix_local_qt6})
  if(_layout_fix_qt_prefixes)
    list(APPEND CMAKE_PREFIX_PATH ${_layout_fix_qt_prefixes})
    unset(Qt6_DIR CACHE)
    find_package(Qt6 QUIET COMPONENTS Widgets)
  endif()
endif()
option(LAYOUT_FIX_REQUIRE_QT_TRAY "Fail if Qt 6 Widgets is unavailable" OFF)
if(LAYOUT_FIX_REQUIRE_QT_TRAY AND NOT Qt6Widgets_FOUND)
  message(FATAL_ERROR "Qt 6 Widgets is required for the release tray menu (install qt6-base-dev or set CMAKE_PREFIX_PATH/QT_PREFIX)")
endif()

set(LAYOUT_FIX_DEFAULT_QT_PREFIX "")
set(LAYOUT_FIX_USING_SYSTEM_QT OFF)
if(Qt6Widgets_FOUND)
  if(Qt6Widgets_DIR MATCHES "^/usr/")
    set(LAYOUT_FIX_USING_SYSTEM_QT ON)
  else()
    get_filename_component(_qt_cmake_dir "${Qt6Widgets_DIR}" DIRECTORY)
    get_filename_component(_qt_lib_dir "${_qt_cmake_dir}" DIRECTORY)
    get_filename_component(_qt_prefix "${_qt_lib_dir}" DIRECTORY)
    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
      string(REPLACE "$ENV{HOME}" "\${HOME:-}" _qt_prefix "${_qt_prefix}")
    endif()
    set(LAYOUT_FIX_DEFAULT_QT_PREFIX "${_qt_prefix}")
  endif()
endif()

add_executable(layout-fix
  src/main.cpp
  src/app.cpp
  src/config.cpp
  src/log.cpp
  src/utf8.cpp
  src/x11/connection.cpp
  src/x11/listener.cpp
  src/x11/focus.cpp
  src/x11/clipboard.cpp
  src/x11/retype.cpp
  src/input/word_buffer.cpp
  src/layout/groups.cpp
  src/layout/cinnamon.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp
  src/detect/selection.cpp
  src/ui/tray.cpp)

configure_file(data/layout-fix.desktop.in
  ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.desktop @ONLY)
configure_file(data/layout-fix.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.service @ONLY)
configure_file(data/layout-fix-launcher.in
  ${CMAKE_CURRENT_BINARY_DIR}/layout-fix-launcher @ONLY)

target_include_directories(layout-fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(layout-fix PRIVATE
  LAYOUT_FIX_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/layout-fix"
  LAYOUT_FIX_VERSION="${PROJECT_VERSION}")
target_compile_options(layout-fix PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(layout-fix PRIVATE
  X11::X11
  ${X11_Xtst_LIB}
  PkgConfig::XKBCOMMON
  PkgConfig::GIO)
if(Qt6Widgets_FOUND)
  target_link_libraries(layout-fix PRIVATE Qt6::Widgets)
  target_compile_definitions(layout-fix PRIVATE LAYOUT_FIX_HAVE_QT_TRAY=1)
  if(LAYOUT_FIX_USING_SYSTEM_QT)
    message(STATUS "Qt ${Qt6Widgets_VERSION} system tray enabled (system Qt)")
  else()
    message(STATUS "Qt ${Qt6Widgets_VERSION} system tray enabled (custom prefix: ${Qt6Widgets_DIR})")
  endif()
else()
  message(STATUS "Qt 6 Widgets not found; tray indicator disabled")
endif()

add_executable(layout-fix-test
  tests/test_core.cpp
  src/config.cpp
  src/log.cpp
  src/utf8.cpp
  src/input/word_buffer.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp
  src/detect/selection.cpp)
target_include_directories(layout-fix-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(layout-fix-test PRIVATE -Wall -Wextra -Wpedantic)

add_executable(layout-fix-x11-test
  tests/test_x11.cpp
  src/log.cpp
  src/utf8.cpp
  src/x11/connection.cpp
  src/x11/listener.cpp
  src/x11/focus.cpp
  src/x11/clipboard.cpp
  src/x11/retype.cpp
  src/input/word_buffer.cpp
  src/layout/groups.cpp
  src/detect/dictionary.cpp
  src/detect/selection.cpp)
target_include_directories(layout-fix-x11-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(layout-fix-x11-test PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(layout-fix-x11-test PRIVATE
  X11::X11
  ${X11_Xtst_LIB}
  PkgConfig::XKBCOMMON)

if(HUNSPELL_FOUND)
  foreach(target layout-fix layout-fix-test layout-fix-x11-test)
    target_compile_definitions(${target} PRIVATE LAYOUT_FIX_HAVE_HUNSPELL=1)
    target_link_libraries(${target} PRIVATE PkgConfig::HUNSPELL)
  endforeach()
  message(STATUS "Hunspell morphology enabled")
else()
  message(STATUS "Hunspell development files not found; using exact dictionary entries")
endif()

enable_testing()
add_test(NAME core COMMAND layout-fix-test)

option(LAYOUT_FIX_ENABLE_X11_TEST "Register isolated Xvfb X11 integration test in CTest" ON)
find_program(XVFB_RUN_EXECUTABLE xvfb-run)
find_program(SETXKBMAP_EXECUTABLE setxkbmap)
if(LAYOUT_FIX_ENABLE_X11_TEST AND XVFB_RUN_EXECUTABLE AND SETXKBMAP_EXECUTABLE)
  add_test(NAME x11_isolated
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-x11-test.sh
            $<TARGET_FILE:layout-fix-x11-test>)
  message(STATUS "Isolated Xvfb X11 integration test enabled")
else()
  message(STATUS "xvfb-run or setxkbmap not found; x11_isolated test skipped")
endif()

install(TARGETS layout-fix RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/layout-fix-launcher
  DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME layout-fix)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.service
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/user)
install(FILES config/layout-fix.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/layout-fix)
install(FILES data/dicts/uk_UA.dic data/dicts/uk_UA.COPYRIGHT
        DESTINATION ${CMAKE_INSTALL_DATADIR}/layout-fix)
install(FILES LICENSE debian/copyright DESTINATION ${CMAKE_INSTALL_DOCDIR})

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "layout-fix")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
  "Keyboard layout correction for X11 and Cinnamon")
set(CPACK_PACKAGE_DESCRIPTION
  "Corrects text entered with the wrong keyboard layout and converts selected text. Supports English (US), Ukrainian, and Russian when exactly one active dictionary accepts the word or context. The Qt 6 tray menu provides pause, layout switching, settings, About, and quit.")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "layout-fix project")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(LAYOUT_FIX_DEB_DEPENDS
  "libx11-6, libxtst6, libxkbcommon0, libglib2.0-0, libhunspell-1.7-0, hunspell-en-us, hunspell-uk, hunspell-ru")
if(Qt6Widgets_FOUND)
  string(APPEND LAYOUT_FIX_DEB_DEPENDS
    ", libqt6widgets6t64 | libqt6widgets6, qt6-qpa-plugins")
endif()
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${LAYOUT_FIX_DEB_DEPENDS}")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
include(CPack)
