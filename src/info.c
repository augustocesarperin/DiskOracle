#include "../include/info.h"
#include "../include/pal.h"
#include "../include/smart.h"
#include "../include/logging.h" 
#include "../include/report.h" 
#include <stdio.h>    
#include <string.h>   
#include <inttypes.h> 
#include "surface.h"
#include "ui.h"
#include <unistd.h> 
#ifdef _WIN32
#include <windows.h> 
#endif
#include <errno.h> 


static scan_state_t g_final_scan_state;

// Callback local para atualizar a UI durante o scan e salvar o estado final.
static void scan_progress_callback(const scan_state_t* state, void* user_data) {
    BasicDriveInfo* drive_info = (BasicDriveInfo*)user_data;
    // A função ui_draw_scan_progress já sabe como desenhar a barra de progresso.
    ui_draw_scan_progress(state, drive_info);
    memcpy(&g_final_scan_state, state, sizeof(scan_state_t));
}

static const char* smart_status_to_string(SmartStatus status) {
    switch (status) {
        case SMART_HEALTH_OK: return "OK";
        case SMART_HEALTH_WARNING: return "Warning";
        case SMART_HEALTH_FAILING: return "Failing";
        case SMART_HEALTH_PREFAIL: return "Prefail";
        case SMART_HEALTH_UNKNOWN: return "Unknown";
        default: return "N/A";
    }
}

// This function will be expanded to show detailed information about the drive.
void display_drive_info(const char *device_path) {
    if (device_path == NULL) {
        fprintf(stderr, "Error: A device path must be provided.\n");
        return;
    }

    // --- 1. Coletar todos os dados primeiro ---
    BasicDriveInfo drive_info;
    memset(&drive_info, 0, sizeof(BasicDriveInfo)); 
    pal_get_basic_drive_info(device_path, &drive_info);
    
    struct smart_data s_data;
    bool smart_ok = (smart_read(device_path, drive_info.model, drive_info.serial, &s_data) == 0);

    // --- 2. Limpar a tela e exibir o relatório ---
    pal_clear_screen();
    ui_draw_drive_info(&drive_info);
    if (smart_ok) {
        // NOTE: smart_interpret now calls report_smart_data with stdout
        smart_interpret(device_path, &s_data, drive_info.firmware_rev); 
    } else {
        fprintf(stderr, "Error: Could not read S.M.A.R.T. data for %s\n", device_path);
    }
    
    // --- 3. Apresentar opções e capturar entrada ---
    printf("\n[S] Save to File | [Any other key] Back to menu\n");
    int ch = pal_get_char_input();

    if (ch == 's' || ch == 'S') {
        if (pal_create_directory("reports") != PAL_STATUS_SUCCESS) {

        }

        char filename[256] = {0};
        printf("\r                                                  \r"); 
        printf("Save as (e.g., report.txt): ");
        if (pal_get_string_input(filename, sizeof(filename)) && filename[0] != '\0') {
            char full_path[512] = {0};
            snprintf(full_path, sizeof(full_path), "reports/%s", filename);

            FILE* fp = NULL;
            errno_t err = fopen_s(&fp, full_path, "w");
            
            if (err == 0 && fp != NULL) {
                fprintf(fp, "DiskOracle - Detailed Information for Device: %s\n", device_path);
                if (smart_ok) {
                    report_smart_data(fp, device_path, &s_data, drive_info.firmware_rev);
                }
                fclose(fp);
                
                char current_dir[1024];
                if (pal_get_current_directory(current_dir, sizeof(current_dir)) == PAL_STATUS_SUCCESS) {
                     printf("\nThe prophecy has been inscribed upon the scroll: %s/reports/%s\n", current_dir, filename);
                } else {
                    printf("\nThe prophecy has been inscribed upon the scroll: %s\n", full_path);
                }

            } else {
                printf("\nError: Could not open file '%s' for writing.", full_path);
            }
        } else {
            printf("\nInvalid filename or operation cancelled.");
        }
        
        #ifdef _WIN32
            Sleep(2500); 
        #else
            sleep(3);  
        #endif
    }
}

/**
 * @brief Orchestrates the command-line surface scan operation for a device.
 * 
 * This function handles the --surface command. It gets basic drive info,
 * initializes the UI, runs the scan, cleans up the UI, and displays the final report.
 * 
 * @param device_path The system path to the device (e.g., \\.\PhysicalDrive0).
 */
void run_surface_scan_command(const char *device_path) {
    if (device_path == NULL) {
        fprintf(stderr, "Error: A device path must be provided for the surface scan.\n");
        return;
    }

    BasicDriveInfo drive_info;
    memset(&drive_info, 0, sizeof(BasicDriveInfo));
    pal_get_basic_drive_info(device_path, &drive_info);
    
    printf("Preparing surface scan for %s (%s)...\n", drive_info.path, drive_info.model);

    #ifdef _WIN32
        Sleep(1500);
    #else
        sleep(1);
    #endif

    ui_init(); 
    surface_scan(device_path, "quick", scan_progress_callback, &drive_info, &g_final_scan_state);
    ui_cleanup(); 

    ui_display_scan_report(&g_final_scan_state, &drive_info);
}

void show_drive_smart_info(const char *device_path) {
    if (device_path == NULL) {
        fprintf(stderr, "Error: A device path must be provided.\n");
        return;
    }

    printf("Fetching information for %s...\n", device_path);

    BasicDriveInfo drive_info;
    memset(&drive_info, 0, sizeof(BasicDriveInfo));
    pal_get_basic_drive_info(device_path, &drive_info);
    
    // Imprime as informações básicas
    ui_draw_drive_info(&drive_info);

    struct smart_data s_data;
    if (smart_read(device_path, drive_info.model, drive_info.serial, &s_data) == 0) {
        // Passa o firmware obtido da 'drive_info' para a função de interpretação.
        smart_interpret(device_path, &s_data, drive_info.firmware_rev);
    } else {
        fprintf(stderr, "Error: Could not read S.M.A.R.T. data for %s\n", device_path);
    }
}