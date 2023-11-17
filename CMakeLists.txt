cmake_minimum_required(VERSION 2.26)
project(vulkan_tut)

set(CMAKE_CXX_STANDARD 23)

find_package(Vulkan REQUIRED)
find_package(fmt CONFIG REQUIRED)

file(COPY shaders DESTINATION ${CMAKE_BINARY_DIR})

add_executable(
        vulkan_tut
        testenvsetup.cpp
        main.cpp
)

target_link_libraries(vulkan_tut glfw ${GLFW_LIBRARIES} Vulkan::Vulkan fmt::fmt)
