# SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.16)

project(properdesktop-preview VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CTest)

find_package(ProperDesktopToolkit REQUIRED)
find_package(ProperDesktopFiles REQUIRED)
find_package(ProperDesktopPdf REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(Png REQUIRED IMPORTED_TARGET libpng)

add_library(preview-core STATIC
    src/imagesequence.cpp
    src/pdfdocument.cpp
    src/pdfworker.cpp
    src/previewsettings.cpp
    src/rotation.cpp
    src/turnsaver.cpp
)
target_include_directories(preview-core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(preview-core PUBLIC ProperDesktop::Toolkit ProperDesktop::Pdf Threads::Threads PRIVATE PkgConfig::Png)

# The glyphs, built in.
file(GLOB PREVIEW_ICONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/icons/*.svg)
set(PREVIEW_EMBEDDED ${CMAKE_CURRENT_BINARY_DIR}/icons.cpp)
# A list would be split into separate arguments on the way to the script.
string(REPLACE ";" "|" PREVIEW_ICON_LIST "${PREVIEW_ICONS}")
add_custom_command(OUTPUT ${PREVIEW_EMBEDDED}
    COMMAND ${CMAKE_COMMAND} -DOUTPUT=${PREVIEW_EMBEDDED}
            -DFILES=${PREVIEW_ICON_LIST} -P ${PROJECT_SOURCE_DIR}/cmake/EmbedIcons.cmake
    DEPENDS ${PREVIEW_ICONS} ${PROJECT_SOURCE_DIR}/cmake/EmbedIcons.cmake
    VERBATIM)

add_library(preview-ui STATIC
    src/application.cpp
    src/views/cards.cpp
    src/views/imagepage.cpp
    src/views/passworddialog.cpp
    src/views/pdfpage.cpp
    src/views/pdfview.cpp
    src/views/previewmenus.cpp
    src/views/previewroot.cpp
    src/views/previewwindow.cpp
    src/views/shortcutsdialog.cpp
    src/views/sidebar.cpp
    src/views/welcomepage.cpp
    ${PREVIEW_EMBEDDED}
)
target_include_directories(preview-ui PUBLIC src src/views)
target_link_libraries(preview-ui PUBLIC preview-core ProperDesktop::Toolkit ProperDesktop::Files)

add_executable(${PROJECT_NAME} src/main.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE PREVIEW_VERSION="${PROJECT_VERSION}")
target_link_libraries(${PROJECT_NAME} PRIVATE preview-ui)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

properdesktop_add_translations(properdesktop-preview SOURCES src DESKTOP_FILES properdesktop-preview.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/properdesktop-preview.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
