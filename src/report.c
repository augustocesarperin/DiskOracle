#include "report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "predict.h"
#include "smart.h"
#include "pal.h"
#include "logging.h"
#include "nvme_hybrid.h"

static const char* prediction_result_to_string(PredictionResult res) {
    switch (res) {
        case PREDICT_OK: return "OK";
        case PREDICT_WARNING: return "WARNING";
        case PREDICT_FAILURE: return "FAILURE";
        case PREDICT_UNKNOWN: return "UNKNOWN";
        default: return "N/A";
    }
}

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
    if (!device_path_in || !data || !format) return 1;

    char actual_filepath[512];
    if (output_filepath && output_filepath[0] != '\0') {
        strncpy_s(actual_filepath, sizeof(actual_filepath), output_filepath, _TRUNCATE);
    } else {
        time_t t = time(NULL);
        struct tm tm_info_s; // Use a stack-allocated struct
        errno_t err_localtime = localtime_s(&tm_info_s, &t);
        if (err_localtime != 0) {
            // Handle error, perhaps log it or return an error code
            perror("Failed to get local time");
            return 1; // Or some other appropriate error handling
        }
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_info_s);
        
        const char *dev_name_start = strrchr(device_path_in, '/');
        if (!dev_name_start) dev_name_start = strrchr(device_path_in, '\\');
        if (!dev_name_start) dev_name_start = device_path_in; else dev_name_start++;
        
        snprintf(actual_filepath, sizeof(actual_filepath), "reports/%s_%s.%s", dev_name_start, timestamp, format);
    }

    FILE *f = NULL;
    errno_t err_fopen_report = fopen_s(&f, actual_filepath, "w");
    if (err_fopen_report != 0 || !f) {
        perror("Failed to open report file");
        return 1;
    }

    PredictionResult pred_res = predict_failure(device_path_in, data);
    const char *prediction_str = prediction_result_to_string(pred_res);
    
    const char *dev_name_report = strrchr(device_path_in, '/');
    if (!dev_name_report) dev_name_report = strrchr(device_path_in, '\\');
    if (!dev_name_report) dev_name_report = device_path_in; else dev_name_report++;

    if (strcmp(format, "json") == 0) {
        fprintf(f, "{\n");
        fprintf(f, "  \"device_path\": \"%s\",\n", device_path_in);
        fprintf(f, "  \"device_name\": \"%s\",\n", dev_name_report);
        fprintf(f, "  \"report_format\": \"%s\",\n", format);
        fprintf(f, "  \"prediction_status\": \"%s\"", prediction_str);
        print_last_log(f, format);
        fprintf(f, "\n}\n");
    } else if (strcmp(format, "txt") == 0) {
        fprintf(f, "Disk Health Report for: %s (%s)\n", dev_name_report, device_path_in);
        fprintf(f, "Report Format: %s\n", format);
        fprintf(f, "----------------------------------------------------\n");
        fprintf(f, "Prediction Status: %s\n", prediction_str);
        print_last_log(f, format);
        fprintf(f, "----------------------------------------------------\n");
    } else if (strcmp(format, "csv") == 0) {
        fprintf(f, "device_path,device_name,report_format,prediction_status,log1,log2,log3,log4,log5\n");
        fprintf(f, "\"%s\",\"%s\",\"%s\",\"%s\"", device_path_in, dev_name_report, format, prediction_str);
        
        FILE *log_file_csv = NULL;
        errno_t err_fopen_log_csv = fopen_s(&log_file_csv, "logs/hdguardian.log", "r");
        if (err_fopen_log_csv == 0 && log_file_csv) {
            char log_buf_csv[256];
            int log_count_csv = 0;
            char *recent_logs[5] = {NULL};
            while(fgets(log_buf_csv, sizeof(log_buf_csv), log_file_csv) && log_count_csv < 5){
                char *nl = strrchr(log_buf_csv, '\n'); if (nl) *nl = '\0';
                nl = strrchr(log_buf_csv, '\r'); if (nl) *nl = '\0';
                recent_logs[log_count_csv++] = _strdup(log_buf_csv);
            }
            for(int k=0; k < log_count_csv; k++) {
                 fprintf(f, ",\"%s\"", recent_logs[k] ? recent_logs[k] : "");
                 if(recent_logs[k]) free(recent_logs[k]);
            }
            for(int k = log_count_csv; k < 5; k++) {fprintf(f, ",");}
            fclose(log_file_csv);
        }
        fprintf(f, "\n");
    } else {
        fprintf(stderr, "Unsupported report format: %s\n", format);
        fclose(f);
        return 2;
    }

    fclose(f);
    printf("Report successfully generated: %s\n", actual_filepath);
    return 0;
}

// Function to display NVMe health alerts
void report_display_nvme_alerts(const nvme_health_alerts_t *alerts_data) {
    if (!alerts_data || alerts_data->alert_count == 0) {
        printf("  Nenhum alerta NVMe específico a reportar.\n");
        fflush(stdout);
        return;
    }

    printf("\n  Detalhes dos Alertas NVMe:\n");
    printf("  --------------------------\n");
    for (int i = 0; i < alerts_data->alert_count; ++i) {
        const nvme_alert_info_t* alert = &alerts_data->alerts[i];
        printf("  [%s] %s\n", 
               alert->is_critical ? "CRITICO" : "AVISO  ", 
               alert->description);
        printf("    Valor Atual : %s\n", alert->current_value_str);
        printf("    Limiar      : %s\n", alert->threshold_str);
        if (i < alerts_data->alert_count - 1) {
            printf("    ---\n");
        }
    }
    fflush(stdout);
}
