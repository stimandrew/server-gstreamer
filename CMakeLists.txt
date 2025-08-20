cmake_minimum_required(VERSION 3.16)

project(server-gstreamer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(rknpu_yolo11_file )

set(CMAKE_INSTALL_PREFIX /home/ab/server)

include_directories(./include)

add_library(rknnrt SHARED IMPORTED )

set_target_properties(rknnrt PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libs/librknnrt.so"
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utils/ utils.out)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Quick OpenGL Multimedia SerialBus DBus)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Quick OpenGL Multimedia SerialBus DBus)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0 gstreamer-app-1.0)

set(PROJECT_SOURCES
        main.cpp
        qml.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(server-gstreamer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        gst_server.h gst_server.cpp
        cameraworker.h cameraworker.cpp
        cameracaptureworker.h cameracaptureworker.cpp

        ${rknpu_yolo11_file}
        yolo11.h
        rknpu2/yolo11.cpp
        postprocess.h
        postprocess.cpp
        modbusdeviceserver.h modbusdeviceserver.cpp
        modbusserver.h modbusserver.cpp

    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET server-gstreamer APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(server-gstreamer SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(server-gstreamer
          ${PROJECT_SOURCES}
        )
    endif()
endif()


target_link_libraries(server-gstreamer
  PRIVATE Qt${QT_VERSION_MAJOR}::Core
  Qt${QT_VERSION_MAJOR}::Quick
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::OpenGL
  Qt${QT_VERSION_MAJOR}::Multimedia
  Qt${QT_VERSION_MAJOR}::SerialBus
  Qt${QT_VERSION_MAJOR}::DBus
  ${GSTREAMER_LIBRARIES}
  imageutils
  fileutils
  imagedrawing
  rknnrt
  dl
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
)

include(GNUInstallDirs)
install(TARGETS server-gstreamer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/bus.jpg DESTINATION model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/model/coco_80_labels_list.txt DESTINATION model)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(server-gstreamer)
    qt_finalize_executable(server-gstreamer)
endif()
