#include "pal.h"
#include <stdio.h>

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int pal_list_drives() {
    DIR *dir;
    struct dirent *entry;
    dir = opendir("/sys/block");
    if (!dir) {
        perror("opendir /sys/block");
        return 1;
    }
    printf("Available block devices:\n");
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        // Filter out loop, ram, etc.
        if (strncmp(entry->d_name, "loop", 4) == 0 || strncmp(entry->d_name, "ram", 3) == 0) continue;
        char dev_path[256];
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", entry->d_name);
        printf("- %s\n", dev_path);
    }
    closedir(dir);
    return 0;
}


int pal_get_smart(const char *device) {
    // TODO: Implement Linux SMART retrieval
    printf("[PAL] Linux: get_smart not implemented yet for %s\n", device);
    return 0;
}
