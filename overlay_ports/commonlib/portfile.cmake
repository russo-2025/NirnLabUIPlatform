vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO fenix31415/CommonLibSSE
    REF cea0dca157566d5baecea361536c8445614e2eed
    SHA512 4c05e11c87485f91264db5cd53514c01a93e80e6e848e89bbf1c85fdf52512a9902b8fc5aa934fe9c3935b30c6a0b001d3e78d0011537611d3c2b1d0e97a81a8
    HEAD_REF dev
)

set(OPTIONS "")

if("support-ae" IN_LIST FEATURES)
    list(APPEND OPTIONS -DSKYRIM_SUPPORT_AE)
endif()

message(STATUS "Building commonlib with OPTIONS: `${FEATURE_OPTIONS}`")

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
      ${OPTIONS}
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(
	INSTALL "${SOURCE_PATH}/LICENSE"
	DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
	RENAME copyright)
