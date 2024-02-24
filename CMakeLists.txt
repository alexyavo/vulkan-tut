cmake_minimum_required(VERSION 2.26)
project(vulkan_tut)

set(CMAKE_CXX_STANDARD 20)

find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

file(GLOB SPV_SHADERS "shaders/*.spv")
file(COPY ${SPV_SHADERS} DESTINATION "${CMAKE_BINARY_DIR}/shaders")
file(COPY "textures" DESTINATION ${CMAKE_BINARY_DIR})
file(COPY "models" DESTINATION ${CMAKE_BINARY_DIR})

add_executable(
        vulkan_tut
        testenvsetup.cpp
        main.cpp
        common.h
)

target_link_libraries(vulkan_tut glfw ${GLFW_LIBRARIES} Vulkan::Vulkan fmt::fmt)
