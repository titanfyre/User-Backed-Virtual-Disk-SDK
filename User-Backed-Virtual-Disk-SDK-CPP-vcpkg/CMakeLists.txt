cmake_minimum_required(VERSION 3.24)
project(UserBackedVirtualDiskSDK VERSION 0.1.1 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanUserBackedVirtualDiskSDK STATIC
  "sdk/SDK/Disk.cpp"
)
add_library(UserBackedVirtualDiskSDK::sdk ALIAS TitanUserBackedVirtualDiskSDK)
set_target_properties(TitanUserBackedVirtualDiskSDK PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanUserBackedVirtualDiskSDK PUBLIC cxx_std_17)
target_compile_definitions(TitanUserBackedVirtualDiskSDK PUBLIC )
target_compile_definitions(TitanUserBackedVirtualDiskSDK PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanUserBackedVirtualDiskSDK PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanUserBackedVirtualDiskSDK PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK>"
  "$<INSTALL_INTERFACE:include/SDK>"
)
target_link_libraries(TitanUserBackedVirtualDiskSDK PUBLIC advapi32 cfgmgr32 comctl32 comdlg32 crypt32 gdi32 newdev ole32 setupapi shell32 user32 winhttp wintrust)
install(TARGETS TitanUserBackedVirtualDiskSDK EXPORT UserBackedVirtualDiskSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/user-backed-virtual-disk-sdk)
install(EXPORT UserBackedVirtualDiskSDKTargets NAMESPACE UserBackedVirtualDiskSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UserBackedVirtualDiskSDK)
configure_file("UserBackedVirtualDiskSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/UserBackedVirtualDiskSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/UserBackedVirtualDiskSDKConfigVersion.cmake" VERSION 0.1.1 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/UserBackedVirtualDiskSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/UserBackedVirtualDiskSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/UserBackedVirtualDiskSDK)
