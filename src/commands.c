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
#include "ui.h"
#include "info.h"

int execute_smart_command(const char* device_path) {
    if (!device_path) {
        fprintf(stderr, "Device path cannot be null.\n");
        return EXIT_FAILURE;
    }

    struct smart_data s_data;
    memset(&s_data, 0, sizeof(struct smart_data));

    BasicDriveInfo basic_info;
    memset(&basic_info, 0, sizeof(BasicDriveInfo));
    pal_status_t basic_info_status = pal_get_basic_drive_info(device_path, &basic_info);

    if (basic_info_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to retrieve basic device information for %s.\n", device_path);
        style_set_fg(COLOR_MAGENTA);
        fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(basic_info_status));
        style_reset();
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
            #if defined(_DEBUG)
            fprintf(stderr, "Error: NVMe Orchestrator failed for %s. Status: %d. Method attempted: %s\n",
                    device_path, smart_status, hybrid_ctx.last_operation_result.method_name);
            #endif
        }
        s_data.is_nvme = true;
    } else if (basic_info.bus_type[0] != '\0' && (strcmp(basic_info.bus_type, "ATA") == 0 || strcmp(basic_info.bus_type, "SATA") == 0)) {
        int pal_ata_status = pal_get_smart_data(device_path, &s_data);
        smart_status = (pal_ata_status == 0) ? PAL_STATUS_SUCCESS : PAL_STATUS_IO_ERROR;
         if (smart_status != PAL_STATUS_SUCCESS) {
            fprintf(stderr, "Error: Failed to get S.M.A.R.T. data for ATA drive %s.\n", device_path);
            style_set_fg(COLOR_MAGENTA);
            fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(smart_status));
            style_reset();
        }
        s_data.is_nvme = false;
    } else {
        fprintf(stderr, "Error: Unknown or unsupported drive bus_type ('%s') for S.M.A.R.T. query on %s\n",
                basic_info.bus_type, device_path);
        return EXIT_FAILURE;
    }

    if (smart_status == PAL_STATUS_SUCCESS) {
        bool data_truly_available = s_data.is_nvme ? true : (s_data.attr_count > 0);
        if (data_truly_available) {
            smart_interpret(device_path, &s_data, basic_info.firmware_rev);

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
        fprintf(stderr, "Error: Failed to fetch S.M.A.R.T. data for %s.\n", device_path);
        style_set_fg(COLOR_MAGENTA);
        fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(smart_status));
        style_reset();
        return EXIT_FAILURE;
    }
}

int execute_json_export_command(const char* device_path, const char* output_file) {
    BasicDriveInfo basic_info;
    memset(&basic_info, 0, sizeof(BasicDriveInfo));
    pal_status_t basic_info_status = pal_get_basic_drive_info(device_path, &basic_info);
    if (basic_info_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Warning: Failed to get basic drive info for %s. JSON output may be incomplete.\n", device_path);
        style_set_fg(COLOR_MAGENTA);
        fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(basic_info_status));
        style_reset();
        // Continue execution as some data might still be exportable
    }

    struct smart_data s_data;
    memset(&s_data, 0, sizeof(struct smart_data));
    
    // Re-using the core logic from execute_smart_command to get SMART data
    // a bit of code duplication for now, could be refactored later
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
    }

    nvme_health_alerts_t alerts = {0};
    if (s_data.is_nvme && smart_status == PAL_STATUS_SUCCESS) {
        nvme_analyze_health_alerts(&s_data.data.nvme, &alerts, s_data.data.nvme.spare_thresh);
    }
    
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

int handle_list_drives(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    DriveInfo drives[MAX_DRIVES];
    int drive_count = 0;
    
    pal_status_t status = pal_list_drives(drives, MAX_DRIVES, &drive_count);

    if (status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Failed to list drives.\n");
        return 1;
    }
    
    display_drive_list(drives, drive_count);
    
    return 0;
}

int handle_surface_scan(int argc, char* argv[]) {
    if (argc < 3) {
        style_set_fg(COLOR_BRIGHT_YELLOW);
        fprintf(stderr, "The Oracle requires a focus, a sacrifice... a device path.\n");
        style_reset();
        fprintf(stderr, "Usage: diskoracle --surface <device_path>\n");
        return 1;
    }
    run_surface_scan_command(argv[2]);
    return 0;
}

int handle_smart(int argc, char* argv[]) {
    if (argc < 3) {
        style_set_fg(COLOR_BRIGHT_YELLOW);
        fprintf(stderr, "To read the digital entrails, you must present the Oracle with a device path.\n");
        style_reset();
        fprintf(stderr, "Usage: diskoracle --smart <device_path>\n");
        return 1;
    }
    return execute_smart_command(argv[2]);
}

int handle_smart_json(int argc, char* argv[]) {
    if (argc < 3) {
        style_set_fg(COLOR_BRIGHT_YELLOW);
        fprintf(stderr, "The Oracle cannot weave prophecies into JSON without a device path.\n");
        style_reset();
        fprintf(stderr, "Usage: diskoracle --smart-json <device_path> [output_file]\n");
        return 1;
    }
    const char* device_path = argv[2];
    const char* output_file = (argc > 3) ? argv[3] : NULL; // Arquivo de saída é opcional

    return execute_json_export_command(device_path, output_file);
}

void handle_error_log_command(const char* device_path) {
    printf("--- Verifying device type for %s ---\n", device_path);
    PAL_BUS_TYPE bus_type = pal_get_device_bus_type(device_path);

    if (bus_type != PAL_BUS_TYPE_NVME) {
        fprintf(stderr, "\n[ERROR] Incorrect Device Type\n");
        fprintf(stderr, "The specified device (%s) is not an NVMe drive.\n", device_path);
        fprintf(stderr, "The --error-log command requires a direct NVMe device path.\n\n");
        fprintf(stderr, "Detected Bus Type: ");
        switch (bus_type) {
            case PAL_BUS_TYPE_ATA:
            case PAL_BUS_TYPE_SATA:
                fprintf(stderr, "ATA/SATA\n");
                break;
            case PAL_BUS_TYPE_USB:
                fprintf(stderr, "USB\n");
                break;
            case PAL_BUS_TYPE_SCSI:
                fprintf(stderr, "SCSI\n");
                break;
            default:
                fprintf(stderr, "Unknown or Unsupported\n");
                break;
        }
        fprintf(stderr, "\nTo find the correct NVMe device in Windows, run this PowerShell command:\n");
        fprintf(stderr, "Get-WmiObject -Class Win32_DiskDrive | Select-Object Index, Model, InterfaceType\n");
        fprintf(stderr, "Then use the 'Index' for the NVMe drive (e.g., \\\\.\\PhysicalDrive<Index>).\n");
        return;
    }

    printf("Device is NVMe. Proceeding with command...\n\n");

    uint8_t identify_buffer[4096];
    pal_status_t identify_status = pal_get_nvme_identify_data(device_path, identify_buffer);

    if (identify_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "[FATAL DIAGNOSIS] The 'Identify Controller' command failed.\n");
        fprintf(stderr, "This is the final confirmation that the storage driver is not processing IOCTL_STORAGE_PROTOCOL_COMMAND correctly.\n");
        fprintf(stderr, "The most likely cause is a vendor-specific driver (e.g., Intel RST, Samsung NVMe Driver) that is overriding the standard Microsoft driver.\n\n");
        fprintf(stderr, "RECOMMENDED ACTION:\n");
        fprintf(stderr, "1. Open Device Manager.\n");
        fprintf(stderr, "2. Go to 'Storage controllers'.\n");
        fprintf(stderr, "3. Right-click your NVMe controller -> 'Update driver'.\n");
        fprintf(stderr, "4. Choose 'Browse my computer...' -> 'Let me pick from a list...'.\n");
        fprintf(stderr, "5. Select 'Standard NVM Express Controller' and install it.\n");
        fprintf(stderr, "6. Reboot and try again.\n");
        return;
    }

    uint8_t elpe = identify_buffer[547] & 0x0F;
    printf("-> 'Identify Controller' command SUCCEEDED.\n");
    printf("-> Firmware reports %u (+1) Error Log Page Entries (ELPE).\n\n", elpe);

    if (elpe == 0) {
        fprintf(stderr, "[FINAL DIAGNOSIS] The device firmware does not support the Error Information Log (LID 0x01).\n");
        fprintf(stderr, "This is common for consumer-grade SSDs. The drive's error count can still be monitored via the S.M.A.R.T. log (--smart command).\n");
        return;
    }

    struct smart_data s_data = {0};
    pal_status_t smart_status = pal_get_smart_data(device_path, &s_data);
    if (smart_status != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Could not retrieve S.M.A.R.T. data to check for error logs.\n");
        fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(smart_status));
        return;
    }

    if (!s_data.is_nvme) {
        fprintf(stderr, "Error: Inconsistency detected. Device was identified as NVMe but SMART data is not NVMe format.\n");
        return;
    }

    uint64_t num_entries_64 = 0;
    memcpy(&num_entries_64, s_data.data.nvme.num_err_log_entries, sizeof(num_entries_64));
    
    uint8_t num_error_entries = (num_entries_64 > 255) ? 255 : (uint8_t)num_entries_64;

    if (num_error_entries == 0) {
        style_set_fg(COLOR_BRIGHT_GREEN);
        printf("The Oracle gazes into the disk's past... and finds a flawless record. No errors logged.\n");
        style_reset();
        return;
    }
    
    printf("The Oracle has found %u error(s) etched into the drive's memory. Deciphering...\n\n", num_error_entries);

    for (uint8_t i = 0; i < num_error_entries; ++i) {
        NVMeErrorLogEntry log_entry;
        pal_status_t log_status = pal_get_nvme_error_log(device_path, i, &log_entry);

        if (log_status != PAL_STATUS_SUCCESS) {
            fprintf(stderr, "\n[DIAGNOSIS] Failed to retrieve Error Log Entry %u.\n", i);
            fprintf(stderr, "Since 'Identify Controller' succeeded but this failed, the storage driver is selectively blocking the Error Log Page (LID 0x01).\n");
            fprintf(stderr, "This is a known issue with some vendor-specific drivers. See the recommended action above to switch to the standard Microsoft driver.\n");
            fprintf(stderr, "Oracle's Whisper: %s\n", pal_get_error_string(log_status));
            return;
        } else {
            ui_display_error_log_entry(&log_entry, i);
        }
    }
}

int handle_help(int argc, char* argv[]) {
    print_full_help();
    return 0; 
} 