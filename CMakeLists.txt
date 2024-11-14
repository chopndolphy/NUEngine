cmake_minimum_required(VERSION 3.12)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-DAVI_DISABLE_INTERCHANGE -O2 -Wall -Wextra -pedantic -g")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_CXX_FLAGS "-DNDEBUG -DAVI_DISABLE_INTERCHANGE -O3")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


project(nu-engine VERSION 1.0)

set(EXTERNAL ${CMAKE_CURRENT_SOURCE_DIR}/external/)

set(CORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core/src/)
set(CORE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/core/include/)

set(EDITOR_SRC ${CMAKE_CURRENT_SOURCE_DIR}/editor/src/)
set(EDITOR_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/editor/include)

############ CORE ##############

## DYNAMIC LIBRARY
add_library(nu-core SHARED
    ${CORE_SRC}/nuEngine.cpp
    ${CORE_SRC}/nuWindow.cpp
    ${CORE_SRC}/util/nuInstanceBuilder.cpp
    ${CORE_SRC}/util/nuWindowBuilder.cpp
)
set_target_properties(nu-core PROPERTIES VERSION ${PROJECT_VERSION})
set_target_properties(nu-core PROPERTIES SOVERSION ${PROJECT_VERSION_MAJOR})
set_target_properties(nu-core PROPERTIES PUBLIC_HEADER ${CORE_INCLUDE}/nu-core.h)
target_include_directories(nu-core PRIVATE ${CORE_INCLUDE})
target_compile_definitions(nu-core PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
include(GNUInstallDirs)
install(TARGETS nu-core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

## INCLUDE LIBRARIES
add_subdirectory(${EXTERNAL}/fastgltf)
target_link_libraries(nu-core PUBLIC fastgltf::fastgltf)
target_include_directories(nu-core INTERFACE ${EXTERNAL}/fastgltf/include)

set(GLM_INCLUDE_DIR ${EXTERNAL}/glm/glm)
target_include_directories(nu-core PUBLIC ${GLM_INCLUDE_DIR})
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)

set(STB_IMAGE_INCLUDE_DIR ${EXTERNAL}/stb_image)
target_include_directories(nu-core PUBLIC ${STB_IMAGE_INCLUDE_DIR})

add_subdirectory(${EXTERNAL}/vkbootstrap)
target_link_libraries(nu-core PUBLIC vk-bootstrap::vk-bootstrap)
set_target_properties(vk-bootstrap PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(VMA_INCLUDE_DIR ${EXTERNAL}/vma)
target_include_directories(nu-core PUBLIC ${VMA_INCLUDE_DIR})

# if (WIN32)
#     set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
# elseif (UNIX)
#     set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR)
# endif()
add_subdirectory(${EXTERNAL}/volk)
target_link_libraries(nu-core PRIVATE volk)

## SDL2 
# 1. Look for a SDL2 package, 2. look for the SDL2 component and 3. fail if none can be found
find_package(SDL2 REQUIRED CONFIG REQUIRED COMPONENTS SDL2)
# 1. Look for a SDL2 package, 2. Look for the SDL2maincomponent and 3. DO NOT fail when SDL2main is not available
find_package(SDL2 REQUIRED CONFIG COMPONENTS SDL2main)
# SDL2::SDL2main may or may not be available. It is e.g. required by Windows GUI applications
if(TARGET SDL2::SDL2main)
    # It has an implicit dependency on SDL2 functions, so it MUST be added before SDL2::SDL2 (or SDL2::SDL2-static)
    target_link_libraries(nu-core PRIVATE SDL2::SDL2main)
endif()
# Link to the actual SDL2 library. SDL2::SDL2 is the shared SDL library, SDL2::SDL2-static is the static SDL libarary.
target_link_libraries(nu-core PRIVATE SDL2::SDL2)

find_library(VULKAN vulkan)
target_link_libraries(nu-core PUBLIC ${VULKAN})

find_library(PTHREAD pthread)
target_link_libraries(nu-core PUBLIC ${PTHREAD})

find_library(XRANDR Xrandr)
target_link_libraries(nu-core PUBLIC ${XRANDR})

find_library(DL dl)
target_link_libraries(nu-core PUBLIC ${DL})

######### EDITOR ###########

## IMGUI 
file(GLOB_RECURSE IMGUI_SRC_FILES ${EXTERN}/imgui/*.cpp)

## EDITOR EXE
add_executable(nu-editor ${EDITOR_SRC}/nu-editor.cpp
    ${IMGUI_SRC_FILES}
)
target_include_directories(nu-editor PUBLIC ${EDITOR_INCLUDE} ${EXTERNAL}/imgui)

target_link_libraries(nu-editor nu-core)
target_include_directories(nu-editor PUBLIC ${CORE_INCLUDE})

