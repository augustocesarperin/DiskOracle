#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    printf("HDGuardian - Advanced Storage Diagnostic Tool\n");
    printf("(C) 2020-2021\n");
    if (argc == 1) {
        printf("Usage: hdguardian [--list-drives | --smart <device>]\n");
        return 0;
    }
    // Argument parsing (placeholder)
    if (strcmp(argv[1], "--list-drives") == 0) {
        pal_list_drives();
    } else if (strcmp(argv[1], "--smart") == 0 && argc > 2) {
        smart_read(argv[2]);
        smart_interpret(argv[2]);
    } else if (strcmp(argv[1], "--scan-quick") == 0 && argc > 2) {
        surface_scan_quick(argv[2]);
    } else if (strcmp(argv[1], "--scan-deep") == 0 && argc > 2) {
        surface_scan_deep(argv[2]);
    } else {
        printf("Unknown command.\n");
    }
    return 0;
}
