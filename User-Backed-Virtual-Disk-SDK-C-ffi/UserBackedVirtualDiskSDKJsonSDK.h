/* SPDX-License-Identifier: MIT */
#ifndef USER_BACKED_VIRTUAL_DISK_SDK_CSDK_H
#define USER_BACKED_VIRTUAL_DISK_SDK_CSDK_H
#include <stdint.h>
#if !defined(_WIN32) || !defined(_WIN64)
#error This SDK requires Windows x64.
#endif
#ifdef __cplusplus
#define USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT noexcept
extern "C" {
#else
#define USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT
#endif
#if defined(USER_BACKED_VIRTUAL_DISK_SDK_CSDK_BUILD)
#define USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API __declspec(dllexport)
#else
#define USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API __declspec(dllimport)
#endif
/* open is inert. One active session per loaded product DLL.
   call accepts length-delimited UTF-8 JSON and returns an allocated UTF-8
   response, or NULL on allocation failure. Always free with this same DLL.
   Session/operation handles are scoped to the client that returned them. */
USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API uint32_t user_backed_virtual_disk_sdk_json_open(uint64_t *session) USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT;
USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API uint32_t user_backed_virtual_disk_sdk_json_close(uint64_t session) USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT;
USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API char *user_backed_virtual_disk_sdk_json_call(uint64_t session,const char *request,uint32_t length) USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT;
USER_BACKED_VIRTUAL_DISK_SDK_CSDK_API void user_backed_virtual_disk_sdk_json_free(char *response) USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef USER_BACKED_VIRTUAL_DISK_SDK_CSDK_NOEXCEPT
#endif
