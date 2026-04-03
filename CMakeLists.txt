# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(PROJECT_VER "v2.0.0")
file(GENERATE OUTPUT "${CMAKE_CURRENT_LIST_DIR}/version.txt" CONTENT "${PROJECT_VER}\n")
project(EEG_detect)

# Export firmware bin to repo root for release upload
set(EXPORT_BIN_NAME "v2.0.0_EEG_detect.bin")
add_custom_command(
    TARGET app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
            "${CMAKE_CURRENT_LIST_DIR}/${EXPORT_BIN_NAME}"
    COMMENT "Exporting firmware to ${EXPORT_BIN_NAME}"
)
