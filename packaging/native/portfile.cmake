vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/User-Backed-Virtual-Disk-SDK/releases/download/sdk-v0.1.1/user-backed-virtual-disk-sdk-sdk-0.1.1.tar.gz"
    FILENAME "user-backed-virtual-disk-sdk-sdk-0.1.1.tar.gz"
    SHA512 93b3db288196d7c535c27283ee95739d76a33f24c27e5146904fc2dd476bbf36f43bba1acc7c81fcd51a3bc5961735a95061971d7f5a7fd32c39d35c2cef2503
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME UserBackedVirtualDiskSDK CONFIG_PATH lib/cmake/UserBackedVirtualDiskSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
