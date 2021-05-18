#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pal.h"
#include "smart.h"
#include "surface.h"
#include "info.h"
#include "logging.h"

int surface_scan(const char *device_path, const char *scan_type);
void display_drive_info(const char *device_path);

void print_usage() {
    printf("=============== DiskOracle ===============\n");
    printf("   Your friendly neighborhood disk health checkup tool!\n");
    printf("   Developed by Augusto César Perin (2020).\n\n");
    printf("HOW TO ROLL:\n");
    printf("  diskoracle <command> [args_if_any]\n\n");
    printf("THE COMMAND LINEUP:\n");
    printf("  --list-drives                  Shows all your physical disk buddies and where to find 'em.\n");
    printf("                                 Hint: The 'Device Path' shown here is your golden ticket for other commands.\n\n");
    printf("  --smart <device_path>          Fetches and decodes SMART data for the chosen drive.\n\n");
    printf("  --surface <device_path> [--type <quick|deep>]\n");
    printf("                                 Scans the disk surface for any gremlins hiding out.\n");
    printf("                                 Defaults to a 'quick' once-over. \n");
    printf("                                 'deep' is the full detective mode, might take a bit.\n\n");
    printf("  --info <device_path>           Get the lowdown: detailed intel on the specified drive.\n\n");
    printf("  --help                         Lost in space? This command is your map! :)\n\n");
    printf("SHOW ME THE ACTION:\n");
    printf("  diskoracle --list-drives\n");
    printf("  diskoracle --smart /dev/sda       (Linux/macOS example)\n");
    printf("  diskoracle --smart \\\\\\.\\PhysicalDrive0 (Windows example)\n");
    printf("  diskoracle --surface /dev/sdb --type deep\n");
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        print_usage();
        return 0;
    }

    const char* app_name = "diskoracle";

    if (strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    } else if (strcmp(argv[1], "--list-drives") == 0) {
        if (argc == 2) {
            pal_list_drives();
        } else {
            printf("Hold on! The '--list-drives' command doesn't take any extra arguments. Try it solo!\n");
            print_usage();
            return 1;
        }
    } else if (strcmp(argv[1], "--smart") == 0) {
        if (argc == 3) {
            struct smart_data s_data;
            memset(&s_data, 0, sizeof(struct smart_data));

            if (smart_read(argv[2], &s_data) == 0) {
                smart_interpret(argv[2], &s_data);
            } else {
                fprintf(stderr, "Oops! DiskOracle couldn't fetch SMART data for %s. Please double-check the device path.\n", argv[2]);
                char log_buffer[256];
                snprintf(log_buffer, sizeof(log_buffer), "EPIC FAIL: SMART read bombed for %s", argv[2]);
                log_event(log_buffer);
                return 1;
            }
        } else {
            printf("The '--smart' command needs a little help! Please provide a <device_path> so DiskOracle can work its magic.\n");
            print_usage();
            return 1;
        }
    } else if (strcmp(argv[1], "--surface") == 0) {
        const char *device_path = NULL;
        const char *scan_type = "quick";

        if (argc == 3) {
            device_path = argv[2];
        } else if (argc == 5 && strcmp(argv[3], "--type") == 0) {
            device_path = argv[2];
            scan_type = argv[4];
            if (strcmp(scan_type, "quick") != 0 && strcmp(scan_type, "deep") != 0) {
                printf("Hmm, that scan type '%s' for '--surface' isn't on the guest list. Try 'quick' or 'deep'.\n", scan_type);
                print_usage();
                return 1;
            }
        } else {
            printf("To kick off a '--surface' scan, I need the <device_path>. Feeling fancy? Add '--type <quick|deep>'.\n");
            print_usage();
            return 1;
        }
        
        int scan_status = surface_scan(device_path, scan_type);
        if (scan_status != 0) {
            fprintf(stderr, "Uh oh! The surface scan (%s) on %s hit a snag (status: %d). Might wanna check that out.\n", scan_type, device_path, scan_status);
        }
    } else if (strcmp(argv[1], "--info") == 0) {
        if (argc == 3) {
            display_drive_info(argv[2]);
        } else {
            printf("Looks like the '--info' command is missing something! It needs a <device_path> to know which drive to tell you about.\n");
            print_usage();
            return 1;
        }
    } else {
        printf("Did we just time travel? I don't recognize '%s' or something's missing. Use-help' for a refresher!\n", argv[1]);
        print_usage();
        return 1;
    }

    return 0;
}
