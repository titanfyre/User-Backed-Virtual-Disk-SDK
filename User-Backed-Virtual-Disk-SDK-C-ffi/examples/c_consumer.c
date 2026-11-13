#include "UserBackedVirtualDiskSDKJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(user_backed_virtual_disk_sdk_json_open(&session))return 1;char *reply=user_backed_virtual_disk_sdk_json_call(session,"{",1);if(!reply){user_backed_virtual_disk_sdk_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;user_backed_virtual_disk_sdk_json_free(reply);if(user_backed_virtual_disk_sdk_json_close(session))return 3;return failed;}
