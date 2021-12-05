#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>
#include "config.h"
#include "pal.h"
#include "smart.h"
#include "surface.h"
#include "info.h"
#include "logging.h"
#include "predict.h"
#include "report.h"
#include "../include/nvme_hybrid.h"
#include "nvme_export.h"
#include "nvme_orchestrator.h"
#include "commands.h"
#include "style.h"
#include "ui.h"

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

// Variável global para este arquivo, para armazenar o estado final do scan.
// Uma solução mais avançada poderia usar um ponteiro no user_data.
static scan_state_t g_final_scan_state;

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

// Função de "ponte" que conecta o callback do scan com a função de desenho da UI
void main_scan_callback(const scan_state_t* state, void* user_data) {
    BasicDriveInfo* drive_info = (BasicDriveInfo*)user_data;
    ui_draw_scan_progress(state, drive_info);
    // Copia o estado atual para ser usado no relatório final
    memcpy(&g_final_scan_state, state, sizeof(scan_state_t));
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

    if (argc < 2) {
        print_welcome_screen();
        return 0;
    }

    if (strcmp(argv[1], "--info") == 0) {
        // ... (lógica do --info)
    } else if (strcmp(argv[1], "--surface") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: --surface requires a device path.\n");
            return 1;
        }
        const char* device_path = argv[2];
        
        BasicDriveInfo drive_info;
        if (pal_get_basic_drive_info(device_path, &drive_info) != PAL_STATUS_SUCCESS) {
            fprintf(stderr, "Error: Could not get basic info for device %s\n", device_path);
            return 1;
        }

        ui_init();
        
        // Zera o estado global antes de iniciar
        memset(&g_final_scan_state, 0, sizeof(scan_state_t));
        g_final_scan_state.start_time = time(NULL); // Garante que o tempo de início seja registrado

        surface_scan(device_path, "quick", main_scan_callback, &drive_info);
        
        ui_cleanup();
        
        // Exibe o relatório final usando o último estado capturado
        ui_display_scan_report(&g_final_scan_state, &drive_info);

    } else {
        print_usage(argv[0]);
    }

    return main_ret_code;
}
