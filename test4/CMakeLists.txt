cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# ---------------------------------------------------------------------------
# Flash 占用实测（STM32F103C8 = 64KB Flash / 20KB RAM，已开 -ffunction-sections
# + -fdata-sections + --gc-sections，没用到的 HAL 代码会被裁掉）：
#     Debug  (-O0) : FLASH 32384 B (49.4%)   RAM 13656 B (66.7%)   OK
#     Release(-Os) : FLASH 19844 B (30.3%)   RAM 13640 B (66.6%)   OK
# 两种预设都能编过。Release 体积更小，日常/验收建议用 Release。
# ---------------------------------------------------------------------------

# Set the project name
set(CMAKE_PROJECT_NAME test4)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here（本文件只生成一次、不会被 CubeMX 覆盖，用户源码放这里）
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/motor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/encoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/adc_knob.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/pid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/vofa.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
