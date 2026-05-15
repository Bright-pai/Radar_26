cmake_minimum_required(VERSION 3.18)
project(Radar_26 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RADAR26_WITH_DAHENG "Enable Daheng GXIAPI support" OFF)

set(TENSORRT_ROOT "/usr/local/TensorRT-10.8.0.43" CACHE PATH "TensorRT root path")
set(DAHENG_SDK_ROOT "/usr" CACHE PATH "Daheng SDK root path")

find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs videoio)
find_package(CUDAToolkit REQUIRED)

find_library(OPENCV_FREETYPE_LIB
    NAMES opencv_freetype opencv_freetyped
    HINTS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
)

find_path(TENSORRT_INCLUDE_DIR
    NAMES NvInfer.h
    HINTS
        ${TENSORRT_ROOT}/include
        /usr/include
        /usr/include/x86_64-linux-gnu
)

find_library(TENSORRT_NVINFER
    NAMES nvinfer
    HINTS
        ${TENSORRT_ROOT}/lib
        ${TENSORRT_ROOT}/lib64
    ${TENSORRT_ROOT}/targets/x86_64-linux-gnu/lib
        /usr/lib
        /usr/lib/x86_64-linux-gnu
)


find_library(TENSORRT_NVINFER_PLUGIN
    NAMES nvinfer_plugin
    HINTS
        ${TENSORRT_ROOT}/lib
        ${TENSORRT_ROOT}/lib64
    ${TENSORRT_ROOT}/targets/x86_64-linux-gnu/lib
        /usr/lib
        /usr/lib/x86_64-linux-gnu
)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/config_loader.cpp
    src/daheng_camera.cpp
    src/radar_app.cpp
    src/serial_monitor.cpp
    src/serial_port.cpp
    src/serial_protocol.cpp
    src/tracker_filter.cpp
    src/trt_detector.cpp
)

add_executable(${PROJECT_NAME}_calibration
    src/calibration_main.cpp
    src/config_loader.cpp
    src/daheng_camera.cpp
)

add_executable(${PROJECT_NAME}_camera_test
    src/camera_test_main.cpp
    src/config_loader.cpp
    src/daheng_camera.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${TENSORRT_INCLUDE_DIR}
)

target_include_directories(${PROJECT_NAME}_calibration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${PROJECT_NAME}_camera_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RADAR26_WITH_TENSORRT=1)

target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBS} pthread CUDA::cudart ${TENSORRT_NVINFER})
target_link_libraries(${PROJECT_NAME}_calibration PRIVATE ${OpenCV_LIBS})
target_link_libraries(${PROJECT_NAME}_camera_test PRIVATE ${OpenCV_LIBS})

if(OPENCV_FREETYPE_LIB)
    target_link_libraries(${PROJECT_NAME}_calibration PRIVATE ${OPENCV_FREETYPE_LIB})
    target_compile_definitions(${PROJECT_NAME}_calibration PRIVATE RADAR26_WITH_OPENCV_FREETYPE=1)
else()
    target_compile_definitions(${PROJECT_NAME}_calibration PRIVATE RADAR26_WITH_OPENCV_FREETYPE=0)
endif()

get_filename_component(TENSORRT_LIB_DIR ${TENSORRT_NVINFER} DIRECTORY)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH ${TENSORRT_LIB_DIR})
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH ${TENSORRT_LIB_DIR})
if(TENSORRT_NVINFER_PLUGIN)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${TENSORRT_NVINFER_PLUGIN})
endif()

if(RADAR26_WITH_DAHENG)
    find_path(DAHENG_INCLUDE_DIR
        NAMES GxIAPI.h
        HINTS
            ${CMAKE_CURRENT_SOURCE_DIR}/camera
            ${DAHENG_SDK_ROOT}/include
            /opt/DahengSDK/include
            /usr/include
    )

    find_library(DAHENG_GXIAPI
        NAMES gxiapi
        HINTS
            ${DAHENG_SDK_ROOT}/lib
            ${DAHENG_SDK_ROOT}/lib64
            /opt/DahengSDK/lib
            /usr/lib
            /usr/lib/x86_64-linux-gnu
    )

    if(NOT DAHENG_INCLUDE_DIR OR NOT DAHENG_GXIAPI)
        message(FATAL_ERROR "RADAR26_WITH_DAHENG=ON but Daheng SDK was not found.")
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE ${DAHENG_INCLUDE_DIR})
    target_include_directories(${PROJECT_NAME}_calibration PRIVATE ${DAHENG_INCLUDE_DIR})
    target_include_directories(${PROJECT_NAME}_camera_test PRIVATE ${DAHENG_INCLUDE_DIR})

    target_link_libraries(${PROJECT_NAME} PRIVATE ${DAHENG_GXIAPI})
    target_link_libraries(${PROJECT_NAME}_calibration PRIVATE ${DAHENG_GXIAPI})
    target_link_libraries(${PROJECT_NAME}_camera_test PRIVATE ${DAHENG_GXIAPI})

    get_filename_component(DAHENG_LIB_DIR ${DAHENG_GXIAPI} DIRECTORY)
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH ${DAHENG_LIB_DIR})
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH ${DAHENG_LIB_DIR})
    set_property(TARGET ${PROJECT_NAME}_calibration APPEND PROPERTY BUILD_RPATH ${DAHENG_LIB_DIR})
    set_property(TARGET ${PROJECT_NAME}_calibration APPEND PROPERTY INSTALL_RPATH ${DAHENG_LIB_DIR})
    set_property(TARGET ${PROJECT_NAME}_camera_test APPEND PROPERTY BUILD_RPATH ${DAHENG_LIB_DIR})
    set_property(TARGET ${PROJECT_NAME}_camera_test APPEND PROPERTY INSTALL_RPATH ${DAHENG_LIB_DIR})
endif()

if(RADAR26_WITH_DAHENG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RADAR26_WITH_DAHENG=1)
    target_compile_definitions(${PROJECT_NAME}_calibration PRIVATE RADAR26_WITH_DAHENG=1)
    target_compile_definitions(${PROJECT_NAME}_camera_test PRIVATE RADAR26_WITH_DAHENG=1)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE RADAR26_WITH_DAHENG=0)
    target_compile_definitions(${PROJECT_NAME}_calibration PRIVATE RADAR26_WITH_DAHENG=0)
    target_compile_definitions(${PROJECT_NAME}_camera_test PRIVATE RADAR26_WITH_DAHENG=0)
endif()


set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY BUILD_RPATH /usr/lib)
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH /usr/lib)
set_property(TARGET ${PROJECT_NAME}_calibration APPEND PROPERTY BUILD_RPATH /usr/lib)
set_property(TARGET ${PROJECT_NAME}_calibration APPEND PROPERTY INSTALL_RPATH /usr/lib)
set_property(TARGET ${PROJECT_NAME}_camera_test APPEND PROPERTY BUILD_RPATH /usr/lib)
set_property(TARGET ${PROJECT_NAME}_camera_test APPEND PROPERTY INSTALL_RPATH /usr/lib)
