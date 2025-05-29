cmake_minimum_required(VERSION 3.23)

project(
    CEFSubprocess
    LANGUAGES CXX
)

if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
	if(EXISTS $ENV{VCPKG_ROOT})
		cmake_path(SET CMAKE_TOOLCHAIN_FILE NORMALIZE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
	else()
		message(FATAL_ERROR "invalid VCPKG_ROOT")
	endif()
	
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/../../overlay_ports")

    set(CMAKE_BUILD_TYPE Release)
    set(VCPKG_BUILD_TYPE Release)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static")
    set(VCPKG_CRT_LINKAGE static)
    set(VCPKG_LIBRARY_LINKAGE static)

	# install deps from vcpkg.json
	include($ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

# ============================ CEF LIB ============================
add_library(cef INTERFACE)
target_link_libraries(cef INTERFACE ${CEF_LIBRARY_RELEASE})

target_link_libraries(cef INTERFACE "${CEF_SHARE}/Release/cef_sandbox.lib")
target_link_libraries(cef INTERFACE "${CEF_SHARE}/Release/libcef.lib")

target_include_directories(cef INTERFACE
    $<BUILD_INTERFACE:${CEF_INCLUDE_DIR}>
)
set(cef_files_path "${CEF_SHARE}/$<IF:$<CONFIG:Debug>,Debug,Release>")
add_custom_target(
    CefLibraryFiles
    # Cef files
    COMMAND ${CMAKE_COMMAND} -E make_directory                                                  ${NL_UI_PATH}/
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/chrome_elf.dll                           ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/d3dcompiler_47.dll                       ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libcef.dll                               ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libEGL.dll                               ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libGLESv2.dll                            ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/snapshot_blob.bin                        ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/v8_context_snapshot.bin                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vk_swiftshader.dll                       ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vk_swiftshader_icd.json                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vulkan-1.dll                             ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/icudtl.dat                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory                                                  ${NL_UI_PATH}/CEF
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/chrome_100_percent.pak      ${NL_UI_PATH}/CEF
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/chrome_200_percent.pak      ${NL_UI_PATH}/CEF
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/icudtl.dat                  ${NL_UI_PATH}/CEF
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/resources.pak               ${NL_UI_PATH}/CEF
    COMMAND ${CMAKE_COMMAND} -E make_directory                                                  ${NL_UI_PATH}/CEF/locales
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${cef_files_path}/../Resources/locales           ${NL_UI_PATH}/CEF/locales
    # Output Message
    COMMENT "Copying cef files..." VERBATIM
)

# ============================ CEF Subprocess ============================

include(${CMAKE_SOURCE_DIR}/../../cmake/set_compile_options.cmake)

set(CEFSubprocessProjectName "CEFSubprocess")

file(
    GLOB_RECURSE
    CEFSubprocessProjectName_src 
        "**.h"
        "**.hpp"
        "**.cpp"
)
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/"
    FILES ${CEFSubprocessProjectName_src}
)

add_executable(
    ${CEFSubprocessProjectName}
    WIN32
        ${CEFSubprocessProjectName_src}
)

find_package(spdlog CONFIG REQUIRED)

target_include_directories(
    ${CEFSubprocessProjectName}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/
)

target_link_libraries(
    ${CEFSubprocessProjectName}
    PUBLIC
        cef
        spdlog::spdlog
)

set_compile_options(${CEFSubprocessProjectName})

target_precompile_headers(
    ${CEFSubprocessProjectName}
    PRIVATE
        "PCH.h"
)

set_target_properties(
    ${CEFSubprocessProjectName}
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${NL_UI_PATH}
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${NL_UI_PATH}
        LIBRARY_OUTPUT_DIRECTORY_DEBUG ${NL_UI_PATH}
        LIBRARY_OUTPUT_DIRECTORY_RELEASE ${NL_UI_PATH}
        #ARCHIVE_OUTPUT_DIRECTORY ${NL_UI_PATH}/archive
        #PDB_OUTPUT_DIRECTORY ${NL_UI_PATH}
)

target_compile_definitions(
    ${CEFSubprocessProjectName}
    PRIVATE
        CEF_SUBPROCESS_PROJECT_NAME="${CEFSubprocessProjectName}"
)