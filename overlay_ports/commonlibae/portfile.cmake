vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ZikkeyLS/CommonLibSSE
    REF f159d64746cc0c40b6a7a5e48a70173d7a42aae2
    SHA512 65ccaf8102259d2d37178e018a9defff07dd48a73ece30b8d7f2979f19908a925dba88b6ff0df78e861e41562e7d4f52dc0a134c53d31ea0ef285396ce63d392
    HEAD_REF dev
)

vcpkg_configure_cmake(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
    -DSKYRIM_SUPPORT_AE=ON
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

file(
	INSTALL "${SOURCE_PATH}/LICENSE"
	DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
	RENAME copyright)
