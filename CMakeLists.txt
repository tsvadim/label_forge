cmake_minimum_required(VERSION 3.21)

project(LabelForge
    VERSION 1.0.0
    DESCRIPTION "Open-source label and barcode designer"
    HOMEPAGE_URL "https://github.com/labelforge/labelforge"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(LABELFORGE_BUILD_TESTS "Build the self-test program" ON)
option(LABELFORGE_BUILD_APP "Build the GUI application" ON)
# Release builds pass the version from the git tag (-DLABELFORGE_VERSION=1.2.0);
# it is shown in the program and in the exe file properties.
set(LABELFORGE_VERSION "${PROJECT_VERSION}" CACHE STRING "Program version")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Sql Svg Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------- core library
add_library(labelforge_core STATIC
    src/core/RenderContext.h
    src/core/Variables.cpp
    src/core/TextLayout.h
    src/core/TextLayout.cpp
    src/core/ImageEffects.h
    src/core/ImageEffects.cpp
    src/core/ShapePaths.h
    src/core/ShapePaths.cpp
    src/core/RemoteImage.h
    src/core/RemoteImage.cpp
    src/core/LabelObject.h
    src/core/LabelObject.cpp
    src/core/DataSource.h
    src/core/DataSource.cpp
    src/core/LabelDocument.h
    src/core/LabelDocument.cpp
    src/core/Renderer.h
    src/core/Renderer.cpp
    src/barcode/Barcode.h
    src/barcode/Barcode.cpp
    src/data/Inflate.h
    src/data/Inflate.cpp
    src/data/DataLoader.h
    src/data/DataLoader.cpp
    src/print/SheetLayout.h
    src/print/SheetLayout.cpp
    src/print/PrintJob.h
    src/print/PrintJob.cpp
    src/print/ThermalExport.h
    src/print/ThermalExport.cpp
    src/templates/TemplateLibrary.h
    src/templates/TemplateLibrary.cpp
    third_party/qrcodegen/qrcodegen.hpp
    third_party/qrcodegen/qrcodegen.cpp
)
target_include_directories(labelforge_core PUBLIC src third_party/qrcodegen)
target_link_libraries(labelforge_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Sql Qt6::Svg Qt6::Network)
if(WIN32)
    target_link_libraries(labelforge_core PUBLIC winspool)
endif()

# ---------------------------------------------------------------- application
if(LABELFORGE_BUILD_APP)
    add_library(labelforge_ui STATIC
        src/ui/Icons.h
        src/ui/Icons.cpp
        src/ui/Commands.h
        src/ui/Commands.cpp
        src/ui/ObjectItem.h
        src/ui/ObjectItem.cpp
        src/ui/LabelScene.h
        src/ui/LabelScene.cpp
        src/ui/LabelView.h
        src/ui/LabelView.cpp
        src/ui/PropertyPanel.h
        src/ui/PropertyPanel.cpp
        src/ui/DataPanel.h
        src/ui/DataPanel.cpp
        src/ui/ClipartPanel.h
        src/ui/ClipartPanel.cpp
        src/ui/DataSourceDialog.h
        src/ui/DataSourceDialog.cpp
        src/ui/SheetLayoutDialog.h
        src/ui/SheetLayoutDialog.cpp
        src/ui/PagePreview.h
        src/ui/PagePreview.cpp
        src/ui/PrintDialog.h
        src/ui/PrintDialog.cpp
        src/ui/NewLabelDialog.h
        src/ui/NewLabelDialog.cpp
        src/ui/SaveTemplateDialog.h
        src/ui/SaveTemplateDialog.cpp
        src/ui/MainWindow.h
        src/ui/MainWindow.cpp
    )
    target_link_libraries(labelforge_ui PUBLIC labelforge_core)
    target_compile_definitions(labelforge_ui PUBLIC LABELFORGE_VERSION="${LABELFORGE_VERSION}")

    set(APP_SOURCES src/main.cpp)
    if(WIN32)
        # numeric parts for FILEVERSION; "1.2.0-rc1" gives 1,2,0
        string(REGEX MATCH "^([0-9]+)\\.?([0-9]*)\\.?([0-9]*)" _lf_ver "${LABELFORGE_VERSION}")
        set(LF_VER_MAJOR 0)
        set(LF_VER_MINOR 0)
        set(LF_VER_PATCH 0)
        if(CMAKE_MATCH_1)
            set(LF_VER_MAJOR ${CMAKE_MATCH_1})
        endif()
        if(CMAKE_MATCH_2)
            set(LF_VER_MINOR ${CMAKE_MATCH_2})
        endif()
        if(CMAKE_MATCH_3)
            set(LF_VER_PATCH ${CMAKE_MATCH_3})
        endif()
        set(LF_RC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/labelforge.ico")
        configure_file(packaging/windows/labelforge.rc.in "${CMAKE_CURRENT_BINARY_DIR}/labelforge.rc" @ONLY)
        list(APPEND APP_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/labelforge.rc")
    endif()
    qt_add_executable(LabelForge WIN32 ${APP_SOURCES})
    target_link_libraries(LabelForge PRIVATE labelforge_ui)

    install(TARGETS LabelForge RUNTIME DESTINATION .)
    install(DIRECTORY clipart DESTINATION .)
endif()

# Clipart is looked up next to the executable: keep a copy in the build dir
# so the app and the tests run from there see it.
add_custom_target(labelforge_clipart ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/clipart ${CMAKE_BINARY_DIR}/clipart
    COMMENT "Copying clipart")

# ---------------------------------------------------------------- tests
if(LABELFORGE_BUILD_TESTS)
    qt_add_executable(labelforge_selftest tests/selftest.cpp)
    set_target_properties(labelforge_selftest PROPERTIES WIN32_EXECUTABLE OFF)
    target_link_libraries(labelforge_selftest PRIVATE labelforge_core)
    if(LABELFORGE_BUILD_APP)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        qt_add_executable(labelforge_uitest tests/uitest.cpp)
        set_target_properties(labelforge_uitest PROPERTIES WIN32_EXECUTABLE OFF)
        target_link_libraries(labelforge_uitest PRIVATE labelforge_ui Qt6::Test)
    endif()
endif()
