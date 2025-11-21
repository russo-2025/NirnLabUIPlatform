cmake_minimum_required(VERSION 3.23)

option(BUILD_AS_SHARED "Build as dll skse plugin else build as static lib" ON)
option(SKYRIM_SE "Legacy Skyrim SE (1.5) build" ON)
option(BUILD_TEST_PLUGIN "build test plugin" OFF)
option(ENABLE_DEBUG_INFO "Enable debug info macros" OFF)

if(SKYRIM_SE)
    set(SKYRIM_IS_AE OFF)
    list(APPEND VCPKG_MANIFEST_FEATURES "commonlib-se")
else()
    set(SKYRIM_IS_AE ON)
    list(APPEND VCPKG_MANIFEST_FEATURES "commonlib-ae")
endif()

# Version
set(LIB_MAJOR_VERSION 33)
set(LIB_MINOR_VERSION 2)
set(API_MAJOR_VERSION 33)
set(API_MINOR_VERSION 2)

# VCPKG config
string(REPLACE "\\" "/" ENV_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
set(ENV{VCPKG_ROOT} ${ENV_VCPKG_ROOT})

set(VCPKG_TARGET_TRIPLET "x64-windows-static")

if(NOT EXISTS ${CMAKE_TOOLCHAIN_FILE})
	if(EXISTS $ENV{VCPKG_ROOT})
		cmake_path(SET CMAKE_TOOLCHAIN_FILE NORMALIZE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
	else()
		message(FATAL_ERROR "invalid VCPKG_ROOT")
	endif()
	
    set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/overlay_ports")


	# install deps from vcpkg.json
	include($ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

message("CMAKE_TOOLCHAIN_FILE = ${CMAKE_TOOLCHAIN_FILE}")


# Includes pdb files in release mode
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

# Options
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "/EHsc /MP /W4")
add_compile_definitions(WINDOWS)

project(
    NirnLabUIPlatform
    LANGUAGES CXX
    VERSION ${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}
)
include(${CMAKE_SOURCE_DIR}/cmake/set_compile_options.cmake)
set(CMAKE_GENERATOR_PLATFORM x64)

string(REPLACE "/Ob2" "/Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/Ob2" "/Ob3" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")

# Configure files
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	@ONLY
)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

file(
    GLOB 
    proj_stuff
        ${CMAKE_SOURCE_DIR}/.clang-format
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
)
source_group(" " FILES ${proj_stuff})

# Deps
find_package(CommonLibSSE CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(PalSigslot CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Paths
set(API_FOLDER_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform/NirnLabUIPlatformAPI)
if (NOT EXISTS ${API_FOLDER_PATH})
    message(FATAL_ERROR "API folder not found \"${API_FOLDER_PATH}\"")
endif()
if (NOT DEFINED OUTPUT_PATH)
    set(OUTPUT_PATH ${CMAKE_BINARY_DIR}/dist/$<CONFIG>)
endif()

set(SKSE_PLUGIN_REL_PATH "Data/SKSE/Plugins")
set(SKSE_PLUGIN_PATH ${OUTPUT_PATH}/${SKSE_PLUGIN_REL_PATH})

if(NOT DEFINED NL_UI_REL_PATH)
    set(NL_UI_REL_PATH "Data/NirnLabUIPlatform")
endif()
set(NL_UI_PATH ${OUTPUT_PATH}/${NL_UI_REL_PATH})

# ============================ CEF LIB ============================
add_library(cef INTERFACE)
find_library(CEF_LIBRARY_DEBUG libcef_dll_wrapper)
string(REPLACE "/debug/lib/" "/lib/" CEF_LIBRARY_RELEASE ${CEF_LIBRARY_DEBUG})
find_path(CEF_INCLUDE_DIR include/cef_app.h)
find_path(CEF_SHARE share/cef-prebuilt)
set(CEF_SHARE "${CEF_SHARE}/share/cef-prebuilt")
target_link_libraries(cef INTERFACE "$<IF:$<CONFIG:Debug>,${CEF_LIBRARY_DEBUG},${CEF_LIBRARY_RELEASE}>")
target_link_libraries(cef INTERFACE "${CEF_SHARE}/$<CONFIG>/libcef.lib")
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
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/dxcompiler.dll                           ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/dxil.dll                                 ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libcef.dll                               ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libEGL.dll                               ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/libGLESv2.dll                            ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/v8_context_snapshot.bin                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vk_swiftshader.dll                       ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vk_swiftshader_icd.json                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/vulkan-1.dll                             ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/chrome_100_percent.pak      ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/chrome_200_percent.pak      ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/icudtl.dat                  ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E copy ${cef_files_path}/../Resources/resources.pak               ${NL_UI_PATH}
    COMMAND ${CMAKE_COMMAND} -E make_directory                                                  ${NL_UI_PATH}/locales
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${cef_files_path}/../Resources/locales           ${NL_UI_PATH}/locales
    # Output Message
    COMMENT "Copying cef files..." VERBATIM
)

# ============================ CEF Subprocess ============================
set(CEFSubprocessProjectName "CEFSubprocess")

file(
    GLOB_RECURSE
    CEFSubprocessProjectName_src 
        "src/CEFSubprocess/**.h"
        "src/CEFSubprocess/**.hpp"
        "src/CEFSubprocess/**.cpp"
)
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/CEFSubprocess"
    FILES ${CEFSubprocessProjectName_src}
)

add_executable(
    ${CEFSubprocessProjectName}
    WIN32
        ${CEFSubprocessProjectName_src}
)

target_include_directories(
    ${CEFSubprocessProjectName}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/CEFSubprocess
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
        "src/CEFSubprocess/PCH.h"
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

add_dependencies(${CEFSubprocessProjectName} CefLibraryFiles)

# ============================ UIPlatform ============================
set(UIPlatformProjectName "NirnLabUIPlatform")

file(
    COPY
        ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    DESTINATION
        ${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform/NirnLabUIPlatformAPI
)

file(
    GLOB_RECURSE
    UIPlatformProjectName_src
        "src/UIPlatform/**.h"
        "src/UIPlatform/**.hpp"
        "src/UIPlatform/**.cpp"
)
source_group(
    TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform"
    FILES ${UIPlatformProjectName_src}
)
list(APPEND UIPlatformProjectName_src ${proj_stuff})

# Build type
if(BUILD_AS_SHARED)
    add_library(${UIPlatformProjectName} SHARED ${UIPlatformProjectName_src})
    set_target_properties(
        ${UIPlatformProjectName}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_DEBUG ${NL_UI_PATH}
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${NL_UI_PATH}
            LIBRARY_OUTPUT_DIRECTORY_DEBUG ${NL_UI_PATH}
            LIBRARY_OUTPUT_DIRECTORY_RELEASE ${NL_UI_PATH}
            #ARCHIVE_OUTPUT_DIRECTORY ${NL_UI_PATH}/archive
            #PDB_OUTPUT_DIRECTORY ${NL_UI_PATH}
    )
else()
    add_library(${UIPlatformProjectName} STATIC ${UIPlatformProjectName_src})
endif()

target_include_directories(
    ${UIPlatformProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform/NirnLabUIPlatformAPI>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/CEFSubprocess>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_compile_options(${UIPlatformProjectName})

target_precompile_headers(
    ${UIPlatformProjectName}
    PRIVATE
        "src/UIPlatform/PCH.h"
)

target_link_libraries(
    ${UIPlatformProjectName}
    PUBLIC
        CommonLibSSE::CommonLibSSE
        "d3d11.lib"
        "dxguid.lib"
        Microsoft::DirectXTK
        cef
        nlohmann_json::nlohmann_json
)

# Detours
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h")
find_library(DETOURS_LIBRARY detours REQUIRED)
target_include_directories(${UIPlatformProjectName} PRIVATE ${DETOURS_INCLUDE_DIRS})
target_link_libraries(${UIPlatformProjectName} PRIVATE ${DETOURS_LIBRARY})

if(ENABLE_DEBUG_INFO)
    target_compile_definitions(
        ${UIPlatformProjectName}
        PRIVATE
            __ENABLE_DEBUG_INFO
    )
endif()

target_compile_definitions(
    ${UIPlatformProjectName}
    PRIVATE
        NL_UI_REL_PATH="${NL_UI_REL_PATH}"
        NL_UI_SUBPROC_NAME="${CEFSubprocessProjectName}.exe"
)

# ============================ UIPlugin ============================
if(BUILD_AS_SHARED)
    set(UIPluginProjectName "NirnLabUIPlugin")

    file(
        GLOB_RECURSE
        UIPluginProjectName_src
            "src/UIPlugin/**.h"
            "src/UIPlugin/**.hpp"
            "src/UIPlugin/**.cpp"
    )
    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlugin"
        FILES ${UIPluginProjectName_src}
    )
    list(
        APPEND
        UIPluginProjectName_src
            ${proj_stuff}
            ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
    )

    add_library(${UIPluginProjectName} SHARED ${UIPluginProjectName_src})
    set_target_properties(
        ${UIPluginProjectName}
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_DEBUG ${SKSE_PLUGIN_PATH}
            RUNTIME_OUTPUT_DIRECTORY_RELEASE ${SKSE_PLUGIN_PATH}
            LIBRARY_OUTPUT_DIRECTORY_DEBUG ${SKSE_PLUGIN_PATH}
            LIBRARY_OUTPUT_DIRECTORY_RELEASE ${SKSE_PLUGIN_PATH}
            #ARCHIVE_OUTPUT_DIRECTORY ${SKSE_PLUGIN_PATH}/archive
            #PDB_OUTPUT_DIRECTORY ${SKSE_PLUGIN_PATH}
    )

    target_include_directories(
        ${UIPluginProjectName}
        PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/include
    )

    set_compile_options(${UIPluginProjectName})

    target_link_libraries(
        ${UIPluginProjectName}
        PUBLIC
            CommonLibSSE::CommonLibSSE
    )

    target_compile_definitions(
        ${UIPluginProjectName}
        PRIVATE
            NL_UI_REL_PATH="${NL_UI_REL_PATH}"
            NL_UI_LIB_NAME="${UIPlatformProjectName}"
    )
endif()

# ============================ UIPlatformTest ============================
if(BUILD_AS_SHARED)
    if(BUILD_TEST_PLUGIN)
        set(UIPlatformTestName "NirnLabUIPlatformTest")

        file(
            COPY ${API_FOLDER_PATH}
            DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatformTest/
        )

        file(
            GLOB_RECURSE
            UIPlatformTestName_src
                "src/UIPlatformTest/**.h"
                "src/UIPlatformTest/**.hpp"
                "src/UIPlatformTest/**.cpp"
        )
        source_group(
            TREE "${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatformTest"
            FILES ${UIPlatformTestName_src}
        )

        add_library(${UIPlatformTestName} SHARED ${UIPlatformTestName_src})
        set_target_properties(
            ${UIPlatformTestName}
            PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_DEBUG ${SKSE_PLUGIN_PATH}
                RUNTIME_OUTPUT_DIRECTORY_RELEASE ${SKSE_PLUGIN_PATH}
                LIBRARY_OUTPUT_DIRECTORY_DEBUG ${SKSE_PLUGIN_PATH}
                LIBRARY_OUTPUT_DIRECTORY_RELEASE ${SKSE_PLUGIN_PATH}
                #ARCHIVE_OUTPUT_DIRECTORY ${SKSE_PLUGIN_PATH}/archive
                #PDB_OUTPUT_DIRECTORY ${SKSE_PLUGIN_PATH}
        )

        target_include_directories(
            ${UIPlatformTestName}
            PUBLIC
                ${CMAKE_CURRENT_BINARY_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatformTest
        )

        set_compile_options(${UIPlatformTestName})

        target_precompile_headers(
            ${UIPlatformTestName}
            PRIVATE
                "src/UIPlatformTest/PCH.h"
        )

        target_link_libraries(
            ${UIPlatformTestName}
            PUBLIC
                CommonLibSSE::CommonLibSSE
        )
    endif()
endif()

# ================================ Install ===============================

# Подключение стандартных директорий
include(GNUInstallDirs)

# Установка библиотек и экспорт
install(TARGETS ${UIPlatformProjectName} cef
    EXPORT NirnLabUIPlatform-export
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/NirnLabUIPlatform
)

# Установка SKSE плагина (только если существует)
if(BUILD_AS_SHARED AND TARGET ${UIPluginProjectName})
    install(
        TARGETS ${UIPluginProjectName}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/NirnLabUIPlatform/Data/SKSE/Plugins
    )
endif()

# Установка заголовочных файлов
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/UIPlatform/NirnLabUIPlatformAPI/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/NirnLabUIPlatform"
    FILES_MATCHING PATTERN "*.h"
)

# Установка файлов CEF и других зависимостей
install(
    DIRECTORY
        ${NL_UI_PATH}/
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}/NirnLabUIPlatform/Data/NirnLabUIPlatform
    FILES_MATCHING
        PATTERN "*.exe"
        PATTERN "*.dll"
        PATTERN "*.pak"
        PATTERN "*.dat"
        PATTERN "*.bin"
        PATTERN "*.json"
)

# Установка подкаталогов CEF
install(
    DIRECTORY
        ${NL_UI_PATH}/CEF/
    DESTINATION
        ${CMAKE_INSTALL_BINDIR}/NirnLabUIPlatform/Data/NirnLabUIPlatform/CEF
)

# Конфигурация пакета для find_package
include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NirnLabUIPlatform-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/NirnLabUIPlatform-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NirnLabUIPlatform
)

# Установка файла конфигурации
install(
    FILES 
        ${CMAKE_CURRENT_BINARY_DIR}/NirnLabUIPlatform-config.cmake
    DESTINATION 
        ${CMAKE_INSTALL_LIBDIR}/cmake/NirnLabUIPlatform
)

# Установка экспорта целей
install(
    EXPORT NirnLabUIPlatform-export
    NAMESPACE NirnLabUIPlatform::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NirnLabUIPlatform
    FILE NirnLabUIPlatform-targets.cmake
)
