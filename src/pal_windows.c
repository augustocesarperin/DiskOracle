#include "pal.h"

#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
int pal_list_drives() {
    DWORD drives = GetLogicalDrives();
    printf("Unidades disponíveis:\n");
    for (char d = 'A'; d <= 'Z'; ++d) {
        if (drives & (1 << (d - 'A')))
            printf("- %c:\\\n", d);
    }
    return 0;
}
int pal_get_smart(const char *device) {
    printf("SMART em Windows não implementado nesta versão\n");
    return 1;
}
#else
int pal_list_drives() { printf("PAL Windows não disponível neste SO\n"); return 1; }
int pal_get_smart(const char *device) { return 1; }
#endif
