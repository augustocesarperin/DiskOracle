#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>
#include "pal.h"
#include "smart.h"
#include "surface.h"
#include "info.h"
#include "logging.h"
#include "predict.h"
#include "report.h"
#include "../include/nvme_hybrid.h"
#include "nvme_export.h"
#include "project_config.h"
#include "nvme_orchestrator.h"
#include "commands.h"
#include "style.h"

#ifdef _WIN32
#include <windows.h>
#endif

static const char* disk_oracle_ascii_art[] = {
    "  ****************************************** ",
    "  *    D I S K  :::  O R A C L E   (o)   * ",  
    "  ****************************************** ",
    NULL
};

static const char* disk_oracle_subtitle_text[] = {
    " \"Telling you when your drive is dying, hopefully.\"",
    " Developed by Augusto Cesar Perin - 2020",
    NULL
};

void print_enumerated_drives(pal_drive_info_t *drive_list, int drive_count) {
    printf("\n~~~ Available Drives ~~~\n"); 
    if (drive_count > 0) {
        for (int i = 0; i < drive_count; ++i) {
            double size_gb = (double)drive_list[i].size_bytes / (1024 * 1024 * 1024);
            if (i > 0) {
                printf("~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~ ~\n");
            }
            printf("  Index:  %d\n", i);
            printf("  Path:   %s\n", drive_list[i].device_path);
            printf("  Model:  %s\n", drive_list[i].model);
            printf("  Serial: %s\n", drive_list[i].serial);
            printf("  Type:   %s\n", drive_list[i].type);
            printf("  Size:   %.2f GB\n", size_gb);
            if (i < drive_count - 1) {
                 printf("\n");
            }
        }
        printf("\n===============================================================================\n\n");
    } else {
        printf("  No drives found or accessible.\n");
        printf("===============================================================================\n\n");
    }
    fflush(stdout);
}

void print_welcome_screen() {
    for (int i = 0; disk_oracle_ascii_art[i] != NULL; i++) {
        printf("%s\n", disk_oracle_ascii_art[i]);
    }
    printf(" Hard Drive Diagnostic & Prediction Tool v%s\n", PROJECT_VERSION);
    printf("%s\n", disk_oracle_subtitle_text[0]);
    printf("%s\n\n", disk_oracle_subtitle_text[1]);
    printf("         \\___)(_)(_) (__)  (__)  (__) (__)(__)(_)\\_)(____)(_\\/_) \n");
    printf("\n");
    fflush(stdout);
}

void print_usage(const char *app_name) {
    // Add an extra newline for spacing if called after drive enumeration

    const char *base_name = strrchr(app_name, '\\'); 
    if (base_name) {
        base_name++; 
    } else {
        base_name = strrchr(app_name, '/'); 
        if (base_name) {
            base_name++; 
        } else {
            base_name = app_name; 
        }
    }

    printf("HOW TO ROLL:\n");
    printf("  %s <command> [args_if_any]\n\n", base_name);
    printf("THE COMMAND LINEUP:\n");

    printf("  --list-drives                  Shows all your physical disk buddies and where to find 'em.\n");
    printf("                                 Hint: The 'Device Path' shown here is your golden ticket for other commands.\n");
    printf("                                 (Note: Currently, running %s without arguments also lists drives.)\n\n", base_name);

    printf("  --smart <device_path>          Fetches and decodes SMART data for the chosen drive.\n");
    printf("                                 This is your main diagnostic tool for drive health!\n\n");

    printf("  --disk <index>                 Shortcut! Fetches SMART data for the disk at <index> from the --list-drives output.\n");
    printf("                                 (Example: %s --disk 0)\n\n", base_name);

    printf("  --smart-json <device_path> [output_file.json]\n");
    printf("                                 Super-export! Gets SMART data, device info, and NVMe health alerts to a JSON file.\n");
    printf("                                 If 'output_file.json' is omitted, the JSON treasure prints to your screen (stdout).\n\n");

    printf("  --surface <device_path> [--type <quick|deep>] [PLANNED]\n");
    printf("                                 Scans the disk surface for any gremlins hiding out.\n");
    printf("                                 Defaults to a 'quick' once-over. 'deep' is the full detective mode.\n\n");

    printf("  --info <device_path> [PLANNED] Get the lowdown: detailed intel on the specified drive.\n\n");

    printf("  --report <device_path> [--format <json|xml|csv>] [--output <filename>] [PLANNED]\n");
    printf("                                 Generates a comprehensive health report for the specified drive.\n");
    printf("                                 Default format: json. Default output: reports/device_timestamp.format\n\n");

    printf("  --help, -h, /?                 Lost in space? This command is your map! Shows this help screen. :)\n\n");
    printf("SHOW ME THE ACTION (EXAMPLES):\n");
    printf("  %s                          (Lists available drives and shows this help)\n", base_name);
    printf("  %s --list-drives\n", base_name);
    printf("  %s --smart \\\\.\\PhysicalDrive0    (Windows example)\n", base_name);
    printf("  %s --smart /dev/sda              (Linux example)\n", base_name);
    printf("  %s --disk 1\n", base_name);
    printf("  %s --smart-json \\\\.\\PhysicalDrive1\n", base_name);
    printf("  %s --smart-json /dev/nvme0n1 my_nvme_report.json\n", base_name);
    printf("  %s --surface \\\\.\\PhysicalDrive0 --type deep [EXAMPLE - PLANNED]\n\n", base_name);
    
    printf("DEVICE PATH EXAMPLES (used with --smart, --disk, --smart-json, etc.):\n");
    printf("  Linux:   /dev/sda, /dev/sdb, /dev/nvme0n1\n");
    printf("  Windows: \\\\.\\PHYSICALDRIVE0, \\\\.\\PHYSICALDRIVE1 (Case insensitive for path)\n");
    printf("           (You can often find these in Disk Management or 'wmic diskdrive get DeviceID')\n");
    printf("===============================================================================\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    style_init();

    setlocale(LC_ALL, ""); // Set locale for number formatting, etc.

    pal_status_t pal_init_status = pal_initialize();
    if (pal_init_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: PAL Initialization failed with status: %d\n", pal_init_status);
    }

    nvme_cache_global_init(DEFAULT_NVME_CACHE_AGE_SECONDS);
    // atexit(nvme_cache_global_cleanup); // Optional
    int main_ret_code = 0; // Default exit code

    if (argc == 1) {
        print_welcome_screen();
        pal_drive_info_t drive_list[16];
        int drive_count = 0;
        pal_status_t list_status = pal_list_drives(drive_list, 16, &drive_count);
        if (list_status == PAL_STATUS_SUCCESS) {
            print_enumerated_drives(drive_list, drive_count);
        } else {
            fprintf(stderr, "Error: Failed to list drives. PAL Status: %d\n", list_status);
        }
        print_usage(argv[0]); 
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "/?") == 0) {
        print_welcome_screen(); 
        print_usage(argv[0]); 
    } else if (strcmp(argv[1], "--list-drives") == 0) {
        print_welcome_screen();
        pal_drive_info_t drive_list[16];
        int drive_count = 0;
        pal_status_t list_status = pal_list_drives(drive_list, 16, &drive_count);
        if (list_status == PAL_STATUS_SUCCESS) {
            print_enumerated_drives(drive_list, drive_count);
        } else {
            fprintf(stderr, "Error: Failed to list drives. PAL Status: %d\n", list_status);
            main_ret_code = EXIT_FAILURE;
        }
    } else if (strcmp(argv[1], "--smart") == 0) {
        if (argc == 3) {
            const char* device_path = argv[2];
            main_ret_code = execute_smart_command(device_path);
        } else {
            fprintf(stderr, "Error: Missing device_path for --smart command.\n");
            print_usage(argv[0]);
            main_ret_code = EXIT_FAILURE;
        }
    } else if (strcmp(argv[1], "--disk") == 0) {
        if (argc == 3) {
            long disk_index = strtol(argv[2], NULL, 10);

            pal_drive_info_t drive_list[16];
            int drive_count = 0;
            pal_status_t list_status = pal_list_drives(drive_list, 16, &drive_count);

            if (list_status == PAL_STATUS_SUCCESS) {
                if (disk_index >= 0 && disk_index < drive_count) {
                    const char* device_path = drive_list[disk_index].device_path;
                    printf(">>> Executing --smart for disk at index %ld: %s\n", disk_index, device_path);
                    main_ret_code = execute_smart_command(device_path);
                } else {
                    fprintf(stderr, "Error: Invalid disk index %ld. Must be between 0 and %d.\n", disk_index, drive_count - 1);
                    print_enumerated_drives(drive_list, drive_count);
                    main_ret_code = EXIT_FAILURE;
                }
            } else {
                fprintf(stderr, "Error: Failed to list drives to use --disk shortcut. PAL Status: %d\n", list_status);
                main_ret_code = EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Error: Missing index for --disk command.\n");
            print_usage(argv[0]);
            main_ret_code = EXIT_FAILURE;
        }
    } else if (strcmp(argv[1], "--smart-json") == 0) {
        if (argc >= 3) {
            const char* device_path = argv[2];
            const char* output_file = (argc == 4) ? argv[3] : NULL;
            main_ret_code = execute_json_export_command(device_path, output_file);
        } else {
            fprintf(stderr, "Error: Missing device_path for --smart-json command.\n");
            print_usage(argv[0]);
            main_ret_code = EXIT_FAILURE;
        }
    } else if (strcmp(argv[1], "--surface") == 0) {
        if (argc >= 3) {
            const char* device_path = argv[2];
            const char* scan_type = "quick"; // Default to quick scan
            
            // Check for optional --type argument
            if (argc == 5 && strcmp(argv[3], "--type") == 0) {
            scan_type = argv[4];
        }
        
        SurfaceScanResult scan_result;
        int scan_status = surface_scan(device_path, scan_type, &scan_result);

            if (scan_status == 0) {
                printf("\n--- Surface Scan Report for %s ---\n", device_path);
                printf("  Scan Type:               %s\n", scan_type);
                printf("  Scan Time:               %.2f seconds\n", scan_result.scan_time_seconds);
                printf("  Total Blocks Scanned:    %llu\n", (unsigned long long)scan_result.total_sectors_scanned);
                printf("  Bad Blocks Found:        %llu\n", (unsigned long long)scan_result.bad_sectors_found);
                printf("  Read Errors:             %llu\n", (unsigned long long)scan_result.read_errors);
                printf("  Status:                  %s\n", scan_result.status_message);
                printf("-----------------------------------------\n");
                main_ret_code = (scan_result.bad_sectors_found > 0 || scan_result.read_errors > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
        } else {
                fprintf(stderr, "\nError: Surface scan failed to execute.\n");
                fprintf(stderr, "  Reason: %s\n", scan_result.status_message);
                main_ret_code = EXIT_FAILURE;
        }
        } else {
            fprintf(stderr, "Error: Missing device_path for --surface command.\n");
            print_usage(argv[0]);
            main_ret_code = EXIT_FAILURE;
        }
    } else {
        print_welcome_screen();
        fprintf(stderr, "Error: Unknown command or arguments.\n\n");
        print_usage(argv[0]);
        main_ret_code = EXIT_FAILURE;
    }

    pal_cleanup();
    nvme_cache_global_cleanup();

    return main_ret_code;
}
