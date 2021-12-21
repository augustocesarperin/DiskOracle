#include "report.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <inttypes.h> 
#include "smart.h"
#include "pal.h"
#include "logging.h"
#include "nvme_hybrid.h"
#include "style.h"
#include "../include/info.h"

/*
static const char* prediction_result_to_string(PredictionResult res) {
    switch (res) {
        case PREDICT_OK: return "OK";
        case PREDICT_WARNING: return "Warning";
        case PREDICT_FAILURE: return "Failure";
        default: return "Unknown";
    }
}
*/

static void print_last_log(FILE *f, const char *format) {
    FILE *log_file = NULL;
    errno_t err_fopen = fopen_s(&log_file, "logs/hdguardian.log", "r");
    if (err_fopen != 0 || !log_file) return;
    char *lines[5] = {0};
    char buf[256];
    int count = 0;
    while (fgets(buf, sizeof(buf), log_file)) {
        if (lines[4]) free(lines[4]);
        for (int i = 4; i > 0; --i) lines[i] = lines[i-1];
        lines[0] = _strdup(buf);
        char *nl = strrchr(lines[0], '\n');
        if (nl) *nl = '\0';
        nl = strrchr(lines[0], '\r');
        if (nl) *nl = '\0';
        if (count < 5) count++;
    }
    fclose(log_file);

    if (strcmp(format, "json") == 0) {
        fprintf(f, ",\n  \"last_log_entries\": [\n");
        for (int i = count-1; i >= 0; --i) {
            fprintf(f, "    \"%s\"%s\n", lines[i] ? lines[i] : "", (i > 0) ? "," : "");
        }
        fprintf(f, "  ]");
    } else if (strcmp(format, "txt") == 0) {
        fprintf(f, "\nRecent Log Entries:\n");
        for (int i = count-1; i >= 0; --i) {
            fprintf(f, "  %s\n", lines[i] ? lines[i] : "");
        }
    }
    
    for (int i = 0; i < 5; ++i) if (lines[i]) free(lines[i]);
}
int report_generate(const char *device_path_in, const struct smart_data *data, const char *format, const char *output_filepath) {
    char final_filepath[1024];
    
    // Ensure the "reports" directory exists.
    const char* reports_dir = "reports";
    if (pal_ensure_directory_exists(reports_dir) != PAL_STATUS_SUCCESS) {
        fprintf(stderr, "Error: Could not create the 'reports' directory.\n");
        return 1;
    }

    if (output_filepath && strlen(output_filepath) > 0) {
        // If a specific path is provided, use it.
        // We'll place it inside the reports dir for consistency unless it's an absolute path.
        #ifdef _WIN32
        if (strchr(output_filepath, ':') != NULL) { // Simple check for absolute path on Windows
            snprintf(final_filepath, sizeof(final_filepath), "%s", output_filepath);
        } else {
            snprintf(final_filepath, sizeof(final_filepath), "%s\\%s", reports_dir, output_filepath);
        }
        #else
        if (output_filepath[0] == '/') { // Simple check for absolute path on Linux/macOS
            snprintf(final_filepath, sizeof(final_filepath), "%s", output_filepath);
        } else {
            snprintf(final_filepath, sizeof(final_filepath), "%s/%s", reports_dir, output_filepath);
        }
        #endif
    } else {
        // Generate a default filename if none is provided.
        char sanitized_device_path[256];
        // A simple sanitization: replace backslashes and colons.
        const char* p_in = device_path_in;
        char* p_out = sanitized_device_path;
        while (*p_in && (p_out - sanitized_device_path) < (sizeof(sanitized_device_path) - 1)) {
            if (*p_in == '\\' || *p_in == ':' || *p_in == '/') {
                *p_out = '_';
            } else {
                *p_out = *p_in;
            }
            p_in++;
            p_out++;
        }
        *p_out = '\0';

        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", t);

        #ifdef _WIN32
        snprintf(final_filepath, sizeof(final_filepath), "%s\\diskoracle_report_%s_%s.%s", reports_dir, sanitized_device_path, timestamp, format);
        #else
        snprintf(final_filepath, sizeof(final_filepath), "%s/diskoracle_report_%s_%s.%s", reports_dir, sanitized_device_path, timestamp, format);
        #endif
    }
    
    printf("Generating report at: %s\n", final_filepath);

    FILE* output_stream = fopen(final_filepath, "w");
    if (!output_stream) {
        perror("Error opening report file");
        return 1;
    }

    // Use the single, correct reporting function.
    // The format is handled internally by report_smart_data, but we pass it for clarity
    // or potential future use where the function might dispatch based on it.
    // For now, we assume it generates a text report.
    int result = report_smart_data(output_stream, device_path_in, (struct smart_data*)data, NULL);
    
    fclose(output_stream);
    
    if (result == 0) {
        printf("Report successfully generated.\n");
    } else {
        fprintf(stderr, "Failed to generate report.\n");
    }

    return result;
}

// Function to display NVMe health alerts
void report_display_nvme_alerts(const nvme_health_alerts_t *alerts_data) {
    if (!alerts_data || alerts_data->alert_count == 0) {
        printf("  No specific NVMe alerts to report.\n");
        fflush(stdout);
        return;
    }

    printf("\n  NVMe Alert Details:\n");
    printf("  -------------------\n");
    for (int i = 0; i < alerts_data->alert_count; ++i) {
        const nvme_alert_info_t* alert = &alerts_data->alerts[i];
        printf("  [%s] %s\n", 
               alert->is_critical ? "CRITICAL" : "WARNING ", 
               alert->description);
        printf("    Current Value : %s\n", alert->current_value_str);
        printf("    Threshold     : %s\n", alert->threshold_str);
        if (i < alerts_data->alert_count - 1) {
            printf("    ---\n");
        }
    }
    fflush(stdout);
}

// Helper function to convert 2-byte array (NVMe temperature) to uint16_t
static uint16_t nvme_temp_to_uint16(const uint8_t temp[2]) {
    uint16_t val = 0;
    memcpy(&val, temp, sizeof(uint16_t));
    return val;
}

// Helper function to convert 16-byte array (NVMe counter) to uint64_t
static uint64_t nvme_counter_to_uint64(const uint8_t counter[16]) {
    uint64_t val = 0;
    memcpy(&val, counter, sizeof(uint64_t)); // Assuming little-endian
    return val;
}

int report_smart_data(FILE* output_stream, const char *device_path, struct smart_data *data, const char* firmware_rev) {
    bool use_colors = (output_stream == stdout);

    fprintf(output_stream, "\n### SMART Health Report for %s ###\n", device_path);

    if (data == NULL) {
        fprintf(output_stream, "  Could not retrieve SMART data.\n");
        return 1;
    }

    if (data->is_nvme) {
        // NVMe-specific report
        fprintf(output_stream, "Drive Type: NVMe\n\n");
        fprintf(output_stream, "NVMe SMART Log:\n");
        fprintf(output_stream, "-------------------------------------------------------------------------------\n");
        const struct smart_nvme *nvme = &data->data.nvme;

        fprintf(output_stream, "  %-35s : %s\n", "Firmware Revision", firmware_rev ? firmware_rev : "N/A");
        fprintf(output_stream, "  %-35s : 0x%02X\n", "Critical Warning Flags", nvme->critical_warning);

        uint16_t temp_k = nvme_temp_to_uint16(nvme->temperature);
        fprintf(output_stream, "  %-35s : %u K (%.1f C)\n", "Temperature", temp_k, (double)temp_k - 273.15);
        
        fprintf(output_stream, "  %-35s : %u%%\n", "Available Spare", nvme->avail_spare);
        fprintf(output_stream, "  %-35s : %u%%\n", "Available Spare Threshold", nvme->spare_thresh);
        fprintf(output_stream, "  %-35s : %u%%\n", "Percentage Used", nvme->percent_used);
        
        fprintf(output_stream, "\n  ~~~ Data Units ~~~\n");
        fprintf(output_stream, "  %-35s : %" PRIu64 " (x 1000 blocks of 512 bytes)\n", "Data Units Read", nvme_counter_to_uint64(nvme->data_units_read));
        fprintf(output_stream, "  %-35s : %" PRIu64 " (x 1000 blocks of 512 bytes)\n", "Data Units Written", nvme_counter_to_uint64(nvme->data_units_written));
        
        fprintf(output_stream, "\n  ~~~ Command Counts ~~~\n");
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Host Read Commands", nvme_counter_to_uint64(nvme->host_read_commands));
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Host Write Commands", nvme_counter_to_uint64(nvme->host_write_commands));

        fprintf(output_stream, "\n  ~~~ Controller Activity ~~~\n");
        fprintf(output_stream, "  %-35s : %" PRIu64 " minutes\n", "Controller Busy Time", nvme_counter_to_uint64(nvme->controller_busy_time));

        fprintf(output_stream, "\n  ~~~ Power Statistics ~~~\n");
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Power Cycles", nvme_counter_to_uint64(nvme->power_cycles));
        fprintf(output_stream, "  %-35s : %" PRIu64 " hours\n", "Power On Hours", nvme_counter_to_uint64(nvme->power_on_hours));
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Unsafe Shutdowns", nvme_counter_to_uint64(nvme->unsafe_shutdowns));
        
        fprintf(output_stream, "\n  ~~~ Error Information ~~~\n");
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Media and Data Integrity Errors", nvme_counter_to_uint64(nvme->media_errors));
        fprintf(output_stream, "  %-35s : %" PRIu64 "\n", "Error Information Log Entries", nvme_counter_to_uint64(nvme->num_err_log_entries));
        fprintf(output_stream, "-------------------------------------------------------------------------------\n");

    } else {
        fprintf(output_stream, "Drive Type: ATA/SATA\n\n");
        fprintf(output_stream, "  %-4s %-24s %-8s %-5s %-5s %-6s %-19s %s\n", "ID", "Attribute Name", "Flags", "Value", "Worst", "Thresh", "Raw Value", "Status");
        fprintf(output_stream, "  -------------------------------------------------------------------------------\n");
        
        for (int i = 0; i < data->attr_count; i++) {
            const struct smart_attr *attr = &data->data.attrs[i];
            char attr_name_str[30];
            smart_get_ata_attribute_name(attr->id, attr_name_str, sizeof(attr_name_str));

            uint64_t raw_val = raw_to_uint64(attr->raw);
            char status_str[15] = "OK"; 
            
            if (attr->flags & 0x0001) { // Prefailure attribute
                if (attr->value <= attr->threshold) {
                    strcpy(status_str, "Prefail");
                }
            }

            fprintf(output_stream, "  %-4u %-24s 0x%04X   %-5u %-5u %-6u %-19" PRIu64 " ",
                attr->id, attr_name_str, attr->flags, attr->value, attr->worst,
                attr->threshold, raw_val);
            
            if (use_colors) {
                if (strcmp(status_str, "Prefail") == 0) {
                    style_set_fg(COLOR_BRIGHT_YELLOW);
                } else {
                    style_set_fg(COLOR_BRIGHT_GREEN);
                }
            }
            fprintf(output_stream, "%-10s", status_str);
            if (use_colors) {
                style_reset();
            }
            fprintf(output_stream, "\n");
        }
        fprintf(output_stream, "  -------------------------------------------------------------------------------\n");
        
        // Run the Oracle's analysis on the data
        run_smart_analysis(output_stream, data);
    }

    fprintf(output_stream, "===================== End of Report for %s ======================\n\n", device_path);
    fflush(output_stream); 
    return 0;
}
