cmake_minimum_required(VERSION 3.16)
project(QFlyST LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets SerialPort)

add_executable(QFlyST
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    serial/serialmanager.h
    serial/serialmanager.cpp
    protocol/stm32flasher.h
    protocol/stm32flasher.cpp
    utils/hexparser.h
    utils/hexparser.cpp
    resources.qrc
)

if(WIN32)
    set_target_properties(QFlyST PROPERTIES WIN32_EXECUTABLE TRUE)
    # Windows ICO图标 (仅MSVC，因为需要动态生成ICO)
    if(MSVC)
        target_sources(QFlyST PRIVATE resources.rc)
    endif()
elseif(APPLE)
    set_target_properties(QFlyST PROPERTIES MACOSX_BUNDLE TRUE)
    # macOS ICNS图标
    set(MACOSX_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon.icns)
    if(EXISTS ${MACOSX_ICON})
        set_source_files_properties(${MACOSX_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(QFlyST PRIVATE ${MACOSX_ICON})
        set_target_properties(QFlyST PROPERTIES MACOSX_BUNDLE_ICON_FILE "app_icon.icns")
    endif()
endif()

target_link_libraries(QFlyST
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::SerialPort
)
