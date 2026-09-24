#include <windows.h>
#include <stdio.h>
#include "resource.h"
/* Verify the linked executable, including non-native DPI sizes, without
   starting the application or changing any audio settings. */
int wmain(int argc,wchar_t **argv) {
    if(argc!=2) return 2;
    HMODULE module=LoadLibraryExW(argv[1],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if(!module) return 1;
    const int ids[]={IDI_FSA_APP,IDI_FSA_STEREO,IDI_FSA_51,IDI_FSA_71,IDI_FSA_ATMOS,IDI_FSA_DTSX};
    const int sizes[]={16,20,24,32,40,48,64};
    unsigned verified=0; int failed=0;
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) {
        HRSRC group=FindResourceW(module,MAKEINTRESOURCEW(ids[i]),RT_GROUP_ICON);
        const WORD *header=group?(const WORD*)LockResource(LoadResource(module,group)):NULL;
        if(!header || header[1]!=1 || header[2]!=(i==0?4:3)) { failed=1; continue; }
        for(unsigned j=0;j<sizeof(sizes)/sizeof(sizes[0]);j++) {
            HICON icon=(HICON)LoadImageW(module,MAKEINTRESOURCEW(ids[i]),IMAGE_ICON,sizes[j],sizes[j],0);
            ICONINFO info={0}; BITMAP bitmap={0};
            if(!icon || !GetIconInfo(icon,&info) || !GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap) ||
               bitmap.bmWidth!=sizes[j] || bitmap.bmHeight!=sizes[j]) failed=1;
            else verified++;
            if(info.hbmColor) DeleteObject(info.hbmColor);
            if(info.hbmMask) DeleteObject(info.hbmMask);
            if(icon) DestroyIcon(icon);
        }
    }
    FreeLibrary(module);
    printf("%s icon resources: %u size checks\n",failed?"FAIL":"PASS",verified);
    return failed;
}
