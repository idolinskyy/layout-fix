cmake_minimum_required(VERSION 3.16)
set(VERSION 0.1.0 CACHE STRING "layout-fix package version")
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
find_package(Qt6 6.12 QUIET COMPONENTS Widgets
  HINTS "$ENV{HOME}/Qt/6.12.0/gcc_64/lib/cmake/Qt6")
option(LAYOUT_FIX_REQUIRE_QT_TRAY "Fail if Qt 6.12 Widgets is unavailable" OFF)
if(LAYOUT_FIX_REQUIRE_QT_TRAY AND NOT Qt6Widgets_FOUND)
  message(FATAL_ERROR "Qt 6.12 Widgets is required for the release tray menu")
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
  src/x11/retype.cpp
  src/input/word_buffer.cpp
  src/layout/groups.cpp
  src/layout/cinnamon.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp)

configure_file(data/layout-fix.desktop.in
  ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.desktop @ONLY)
configure_file(data/layout-fix.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/layout-fix.service @ONLY)

target_include_directories(layout-fix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(layout-fix PRIVATE
  LAYOUT_FIX_DATA_DIR="${CMAKE_INSTALL_FULL_DATADIR}/layout-fix")
target_compile_options(layout-fix PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(layout-fix PRIVATE
  X11::X11
  ${X11_Xtst_LIB}
  PkgConfig::XKBCOMMON
  PkgConfig::GIO)
if(Qt6Widgets_FOUND)
  target_link_libraries(layout-fix PRIVATE Qt6::Widgets)
  target_compile_definitions(layout-fix PRIVATE LAYOUT_FIX_HAVE_QT_TRAY=1)
  message(STATUS "Qt 6.12 system tray enabled")
else()
  message(STATUS "Qt 6.12 Widgets not found; tray indicator disabled")
endif()

add_executable(layout-fix-test
  tests/test_core.cpp
  src/utf8.cpp
  src/input/word_buffer.cpp
  src/detect/dictionary.cpp
  src/detect/detector.cpp)
target_include_directories(layout-fix-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(layout-fix-test PRIVATE -Wall -Wextra -Wpedantic)

if(HUNSPELL_FOUND)
  foreach(target layout-fix layout-fix-test)
    target_compile_definitions(${target} PRIVATE LAYOUT_FIX_HAVE_HUNSPELL=1)
    target_link_libraries(${target} PRIVATE PkgConfig::HUNSPELL)
  endforeach()
  message(STATUS "Hunspell morphology enabled")
else()
  message(STATUS "Hunspell development files not found; using exact dictionary entries")
endif()

enable_testing()
add_test(NAME core COMMAND layout-fix-test)

install(TARGETS layout-fix RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(PROGRAMS data/layout-fix-launcher
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
  "Corrects text entered with the wrong keyboard layout. Supports English (US), Ukrainian, and Russian when exactly one active dictionary accepts the word or context. The Qt 6.12 tray menu provides pause, layout switching, config access, and quit.")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "layout-fix project")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "libx11-6, libxtst6, libxkbcommon0, libglib2.0-0, libhunspell-1.7-0, hunspell-en-us, hunspell-uk, hunspell-ru")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
include(CPack)
