#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "pal.h"
#include "smart.h"
#include "report.h"
#include "nvme_orchestrator.h"
#include "../include/nvme_hybrid.h"
#include "nvme_export.h"
#include "style.h"

int execute_smart_command(const char* device_path) {
    struct smart_data s_data;
    memset(&s_data, 0, sizeof(struct smart_data));

    BasicDriveInfo basic_info;
    memset(&basic_info, 0, sizeof(BasicDriveInfo));
    pal_status_t basic_info_status = pal_get_basic_drive_info(device_path, &basic_info);

    if (basic_info_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to retrieve basic device information for %s. PAL Status: %d\n", device_path, basic_info_status);
        return EXIT_FAILURE;
    } 

    pal_status_t smart_status = PAL_STATUS_ERROR;
    nvme_hybrid_context_t hybrid_ctx = {0}; // Initialize all fields to zero/false/NULL

    if (basic_info.bus_type[0] != '\0' && strcmp(basic_info.bus_type, "NVMe") == 0) {
        hybrid_ctx.cache_enabled = false;
        hybrid_ctx.benchmark_mode = false;
        hybrid_ctx.verbose_logging = TRUE; // Consider making this configurable

        smart_status = nvme_orchestrator_get_smart_data(device_path, &s_data, &hybrid_ctx);

        if (smart_status != PAL_STATUS_SUCCESS) {
            fprintf(stderr, "Error: NVMe Orchestrator failed for %s. Status: %d. Method attempted: %s\n",
                    device_path, smart_status, hybrid_ctx.last_operation_result.method_name);
        }
        s_data.is_nvme = true;
    } else if (basic_info.bus_type[0] != '\0' && (strcmp(basic_info.bus_type, "ATA") == 0 || strcmp(basic_info.bus_type, "SATA") == 0)) {
        int pal_ata_status = pal_get_smart_data(device_path, &s_data);
        smart_status = (pal_ata_status == 0) ? PAL_STATUS_SUCCESS : PAL_STATUS_IO_ERROR;
         if (smart_status != PAL_STATUS_SUCCESS) {
            fprintf(stderr, "Error: Failed to get SMART data for ATA drive %s (Standard PAL call). PAL_GET_SMART_DATA_STATUS: %d\n", device_path, pal_ata_status);
        }
        s_data.is_nvme = false;
    } else {
        fprintf(stderr, "Error: Unknown or unsupported drive bus_type ('%s') for SMART query on %s\n",
                basic_info.bus_type, device_path);
        return EXIT_FAILURE;
    }

    if (smart_status == PAL_STATUS_SUCCESS) {
        bool data_truly_available = s_data.is_nvme ? true : (s_data.attr_count > 0);
        if (data_truly_available) {
            smart_interpret(device_path, &s_data);

            SmartStatus health_summary = smart_get_health_summary(&s_data);
            printf("\nOverall Drive Health Summary: ");
            switch (health_summary) {
                case SMART_HEALTH_OK:
                    style_set_fg(COLOR_BRIGHT_GREEN);
                    printf("[OK]\n");
                    break;
                case SMART_HEALTH_WARNING:
                    style_set_fg(COLOR_BRIGHT_YELLOW);
                    printf("[WARNING]\n");
                    break;
                case SMART_HEALTH_PREFAIL:
                    style_set_fg(COLOR_YELLOW);
                    printf("[PRE-FAIL]\n");
                    break;
                case SMART_HEALTH_FAILING:
                    style_set_fg(COLOR_BRIGHT_RED);
                    printf("[FAILING]\n");
                    break;
                case SMART_HEALTH_UNKNOWN:
                default:
                    style_set_fg(COLOR_CYAN);
                    printf("[UNKNOWN]\n");
                    break;
            }
            style_reset();
            fflush(stdout);

            if (s_data.is_nvme) {
                printf("\n>>> Analyzing NVMe specific health alerts... <<<\n");
                nvme_health_alerts_t nvme_alerts_data;
                nvme_analyze_health_alerts(&s_data.data.nvme, &nvme_alerts_data, s_data.data.nvme.spare_thresh);
                report_display_nvme_alerts(&nvme_alerts_data);
            }

            printf("===============================================================================\n");
            fflush(stdout);
            return EXIT_SUCCESS;
        } else {
             fprintf(stderr, "Warning: SMART data reported as unavailable for %s, (status: %d, is_nvme: %d, ata_attrs: %d)\n",
                     device_path, smart_status, s_data.is_nvme, s_data.attr_count);
             return EXIT_FAILURE;
        }
    } else {
        fprintf(stderr, "Error: Failed to fetch or process SMART data for %s (Overall Status: %d).\nPlease ensure the device path is correct, the application has necessary permissions, and the drive type is supported.\n", device_path, smart_status);
        return EXIT_FAILURE;
    }
}

int execute_json_export_command(const char* device_path, const char* output_file) {
    BasicDriveInfo basic_info;
    memset(&basic_info, 0, sizeof(BasicDriveInfo));
    pal_status_t basic_info_status = pal_get_basic_drive_info(device_path, &basic_info);
    if (basic_info_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Warning: Failed to get basic drive info for %s. JSON output may be incomplete. PAL Status: %d\n", device_path, basic_info_status);
        // Continue execution as some data might still be exportable
    }

    struct smart_data s_data;
    memset(&s_data, 0, sizeof(struct smart_data));
    
    // Re-using the core logic from execute_smart_command to get SMART data
    // This is a bit of code duplication for now, could be refactored later
    pal_status_t smart_status = PAL_STATUS_ERROR;
    nvme_hybrid_context_t hybrid_ctx = {0};

    if (basic_info.bus_type[0] != '\0' && strcmp(basic_info.bus_type, "NVMe") == 0) {
        smart_status = nvme_orchestrator_get_smart_data(device_path, &s_data, &hybrid_ctx);
        s_data.is_nvme = true;
    } else if (basic_info.bus_type[0] != '\0' && (strcmp(basic_info.bus_type, "ATA") == 0 || strcmp(basic_info.bus_type, "SATA") == 0)) {
        int pal_ata_status = pal_get_smart_data(device_path, &s_data);
        smart_status = (pal_ata_status == 0) ? PAL_STATUS_SUCCESS : PAL_STATUS_IO_ERROR;
        s_data.is_nvme = false;
    } else {
        fprintf(stderr, "Error: Unknown or unsupported drive bus_type ('%s') for JSON export on %s\n", basic_info.bus_type, device_path);
        return EXIT_FAILURE;
    }

    if (smart_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Warning: Failed to read SMART data for %s. JSON will not contain detailed SMART attributes.\n", device_path);
        // Continue execution, as we might still have basic info to export
    }

    nvme_health_alerts_t alerts = {0};
    if (s_data.is_nvme && smart_status == PAL_STATUS_SUCCESS) {
        nvme_analyze_health_alerts(&s_data.data.nvme, &alerts, s_data.data.nvme.spare_thresh);
    }
    
    // Call the *correct* export function
    int export_result = nvme_export_to_json(
        device_path,
        &basic_info,
        &s_data,
        &alerts,
        &hybrid_ctx,
        output_file
    );

    if (export_result == PAL_STATUS_SUCCESS) {
        if (output_file) {
            printf("Success: Data for %s exported to JSON file: %s\n", device_path, output_file);
        }
    } else {
        fprintf(stderr, "Error: Failed to export data to JSON for %s. Export function returned status %d.\n", device_path, export_result);
    }

    return (export_result == PAL_STATUS_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
} 