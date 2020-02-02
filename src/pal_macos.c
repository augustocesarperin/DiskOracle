#include "pal.h"

#ifdef __APPLE__
#include <stdio.h>
#include <stdlib.h>
int pal_list_drives() {
    printf("Discos disponíveis (macOS):\n");
    system("diskutil list | grep '^/dev/'");
    return 0;
}
int pal_get_smart(const char *device) {
    printf("SMART em macOS não implementado nesta versão\n");
    return 1;
}
#else
int pal_list_drives() { printf("PAL macOS não disponível neste SO\n"); return 1; }
int pal_get_smart(const char *device) { return 1; }
#endif
