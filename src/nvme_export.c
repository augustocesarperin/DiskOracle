#include "nvme_export.h"
#include "pal.h"
#include "smart.h"
#include <stdio.h>
#include <string.h>
#include <strsafe.h>
#include <time.h>    // Para time_t, strftime, localtime_s
#include <windows.h> // Para StringCchPrintfA, StringCchCopyA
#include <inttypes.h> // For PRIu64
#include "../include/pal.h" // For PAL_STATUS codes
#include "../include/smart.h" // For struct smart_data
// Helper para escapar strings JSON (implementação MUITO básica)
static void escape_json_string(const char* input, char* output, size_t out_size) {
    if (!input || !output || out_size == 0) {
        if (output && out_size > 0) output[0] = (char)'\0';
        return;
    }

    char* out_ptr = output;
    const char* in_ptr = input;
    size_t remaining_size = out_size - 1; // Leave space for null terminator

    while (*in_ptr && remaining_size > 0) {
        char char_to_escape = *in_ptr;
        char escape_seq[7] = {0}; // Max \\uXXXX + null
        int seq_len = 0;

        switch (char_to_escape) {
            case '"': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\\""); seq_len = 2; break;
            case '\\': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\\\"); seq_len = 2; break;
            // case '/': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\/"); seq_len = 2; break; // Optional
            case '\b': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\b"); seq_len = 2; break;
            case '\f': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\f"); seq_len = 2; break;
            case '\n': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\n"); seq_len = 2; break;
            case '\r': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\r"); seq_len = 2; break;
            case '\t': StringCchCopyA(escape_seq, sizeof(escape_seq), "\\t"); seq_len = 2; break;
            default:
                if (char_to_escape >= 0 && char_to_escape <= 0x1F) { // Control characters
                    StringCchPrintfA(escape_seq, sizeof(escape_seq), "\\\\u%04X", (unsigned char)char_to_escape);
                    seq_len = 6;
                } else {
                    // No escape needed for this character
                }
                break;
        }

        if (seq_len > 0) {
            if (remaining_size >= (size_t)seq_len) {
                StringCchCopyA(out_ptr, remaining_size + 1, escape_seq);
                out_ptr += seq_len;
                remaining_size -= seq_len;
            } else {
                // Not enough space for escape sequence, truncate
                break;
            }
        } else {
            // No escape needed, copy character directly
            if (remaining_size >= 1) {
                *out_ptr = char_to_escape;
                out_ptr++;
                remaining_size--;
            } else {
                // Not enough space for character, truncate
                break;
            }
        }
        in_ptr++;
    }
    *out_ptr = (char)'\0'; // Null-terminate the output string
}

static uint64_t get_nvme_counter_val(const uint8_t counter[16]) {
    uint64_t val = 0;
    memcpy(&val, counter, sizeof(uint64_t));
    return val;
}

int nvme_export_to_json(
    const char* device_path,                  
    const BasicDriveInfo* basic_info,         
    const struct smart_data* sdata,           
    const nvme_health_alerts_t* alerts,       
    const nvme_hybrid_context_t* hybrid_ctx,  
    const char* output_file_path              
) {
    FILE* outfile = stdout; 
    char buffer[1024];      
    char escaped_str[256];  
    bool first_section_written = false;

    fprintf(stderr, "[DEBUG NVME_EXPORT_JSON] Beginning JSON export for %s\n", device_path ? device_path : "Unknown Device");
    fflush(stderr);

    if (output_file_path) {
        fprintf(stderr, "[DEBUG NVME_EXPORT_JSON] Output target: %s\n", output_file_path);
        fflush(stderr);
        errno_t err = fopen_s(&outfile, output_file_path, "w");
        if (err != 0 || !outfile) {
            fprintf(stderr, "[ERROR NVME_EXPORT_JSON] Failed to open output file: '%s'. Error code: %d\n", output_file_path, err);
            fflush(stderr);
            return PAL_STATUS_IO_ERROR;
        }
    }

    fprintf(outfile, "{\n"); 

    // Seção 1: Informações do Dispositivo (BasicDriveInfo)
    fprintf(outfile, "  \"deviceInfo\": {");
    if (basic_info) {
        escape_json_string(basic_info->model, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"model\": \"%s\",", escaped_str);
        escape_json_string(basic_info->serial, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"serialNumber\": \"%s\",", escaped_str);
        
        char firmware_str[9]; // NVMe firmware is 8 bytes + null terminator
        if (sdata && sdata->is_nvme) {
            // Copy firmware version, ensuring null termination
            memcpy(firmware_str, sdata->data.nvme.firmware, 8);
            firmware_str[8] = (char)'\0';
            // Trim trailing spaces from firmware string if any
            for (int i = 7; i >= 0 && firmware_str[i] == ' '; i--) {
                firmware_str[i] = (char)'\0';
            }
        } else {
            // For ATA or if sdata is not available, use N/A
            StringCchCopyA(firmware_str, sizeof(firmware_str), "N/A");
        }
        escape_json_string(firmware_str, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"firmwareRevision\": \"%s\",", escaped_str);

        escape_json_string(basic_info->type, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"driveType\": \"%s\",", escaped_str);
        escape_json_string(basic_info->bus_type, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"busType\": \"%s\"", escaped_str);
    } else {
        fprintf(outfile, "\n    \"model\": \"N/A\",\n    \"serialNumber\": \"N/A\",\n    \"firmwareRevision\": \"N/A\",\n    \"driveType\": \"N/A\",\n    \"busType\": \"N/A\"");
    }
    fprintf(outfile, "\n  },"); 
    first_section_written = true;
  
    if (hybrid_ctx) {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"accessMethodUsed\": {\n");
        escape_json_string(hybrid_ctx->last_operation_result.method_name, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "    \"methodName\": \"%s\",\n", escaped_str);
        fprintf(outfile, "    \"success\": %s,\n", hybrid_ctx->last_operation_result.success ? "true" : "false");
        fprintf(outfile, "    \"executionTimeMs\": %lu,\n", hybrid_ctx->last_operation_result.execution_time_ms);
        fprintf(outfile, "    \"errorCode\": %lu\n", hybrid_ctx->last_operation_result.error_code);
        fprintf(outfile, "  }"); 
        first_section_written = true;
    }

    // SMART (NVMe ou ATA)
    if (sdata && hybrid_ctx && hybrid_ctx->last_operation_result.success) {
        if (sdata->is_nvme) {
            const struct smart_nvme* nvme = &sdata->data.nvme;
            fprintf(outfile, "\n  \"smartLogNvme\": {\n");
            fprintf(outfile, "    \"criticalWarning\": %u,\n", nvme->critical_warning);

            uint16_t temp_kelvin_val = 0;
            memcpy(&temp_kelvin_val, nvme->temperature, sizeof(uint16_t));
            fprintf(outfile, "    \"temperatureKelvin\": %u,\n", temp_kelvin_val);

            fprintf(outfile, "    \"availableSparePercent\": %u,\n", nvme->avail_spare);
            fprintf(outfile, "    \"availableSpareThresholdPercent\": %u,\n", nvme->spare_thresh);
            fprintf(outfile, "    \"percentageUsed\": %u,\n", nvme->percent_used);
            fprintf(outfile, "    \"dataUnitsRead_x1000_512B\": %llu,\n", get_nvme_counter_val(nvme->data_units_read));
            fprintf(outfile, "    \"dataUnitsWritten_x1000_512B\": %llu,\n", get_nvme_counter_val(nvme->data_units_written));
            fprintf(outfile, "    \"hostReadCommands\": %llu,\n", get_nvme_counter_val(nvme->host_read_commands));
            fprintf(outfile, "    \"hostWriteCommands\": %llu,\n", get_nvme_counter_val(nvme->host_write_commands));
            fprintf(outfile, "    \"controllerBusyTimeMinutes\": %llu,\n", get_nvme_counter_val(nvme->controller_busy_time));
            fprintf(outfile, "    \"powerCycles\": %llu,\n", get_nvme_counter_val(nvme->power_cycles));
            fprintf(outfile, "    \"powerOnHours\": %llu,\n", get_nvme_counter_val(nvme->power_on_hours));
            fprintf(outfile, "    \"unsafeShutdowns\": %llu,\n", get_nvme_counter_val(nvme->unsafe_shutdowns));
            fprintf(outfile, "    \"mediaAndDataIntegrityErrors\": %llu,\n", get_nvme_counter_val(nvme->media_errors));
            fprintf(outfile, "    \"numberOfErrorInformationLogEntries\": %llu\n", get_nvme_counter_val(nvme->num_err_log_entries));
            
            fprintf(outfile, "  },");
        } else { // ATA SMART Data
            // TODO: Implementar exportação detalhada para atributos ATA
            fprintf(outfile, "\n  \"smartAttributesAta\": [\n");
            for(int i=0; i < sdata->attr_count && i < MAX_SMART_ATTRIBUTES; ++i) {
                fprintf(outfile, "    {\n");
                fprintf(outfile, "      \"id\": %u,\n", sdata->data.attrs[i].id);
                // Adicionar nome do atributo aqui seria bom (requer mapeamento ID->Nome)
                fprintf(outfile, "      \"currentValue\": %u,\n", sdata->data.attrs[i].value);
                fprintf(outfile, "      \"worstValue\": %u,\n", sdata->data.attrs[i].worst);
                fprintf(outfile, "      \"threshold\": %u,\n", sdata->data.attrs[i].threshold);
                fprintf(outfile, "      \"rawValue\": \"0x%02X%02X%02X%02X%02X%02X\"\n", 
                        sdata->data.attrs[i].raw[0], sdata->data.attrs[i].raw[1], sdata->data.attrs[i].raw[2],
                        sdata->data.attrs[i].raw[3], sdata->data.attrs[i].raw[4], sdata->data.attrs[i].raw[5]);
                fprintf(outfile, "    }%s\n", (i == sdata->attr_count - 1 || i == MAX_SMART_ATTRIBUTES -1) ? "" : ",");
            }
            fprintf(outfile, "  ],");
        }
    } else {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"smartLog\": { \"status\": \"" "SMART data not available or fetch failed." "\" }");
        first_section_written = true;
    }

    // Seção 4: Alertas de Saúde (Apenas para NVMe por enquanto)
    if (alerts && sdata && sdata->is_nvme) {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"healthAlerts\": [\n");
        if (alerts->alert_count > 0) {
            for (int i = 0; i < alerts->alert_count; ++i) {
                const nvme_alert_info_t* alert = &alerts->alerts[i];
                fprintf(outfile, "    {\n");
                // Idealmente, teríamos nomes de string para nvme_alert_type_t
                fprintf(outfile, "      \"alertTypeId\": %d,\n", alert->alert_type);
                escape_json_string(alert->description, escaped_str, sizeof(escaped_str));
                fprintf(outfile, "      \"description\": \"%s\",\n", escaped_str);
                escape_json_string(alert->current_value_str, escaped_str, sizeof(escaped_str));
                fprintf(outfile, "      \"currentValue\": \"%s\",\n", escaped_str);
                escape_json_string(alert->threshold_str, escaped_str, sizeof(escaped_str));
                fprintf(outfile, "      \"threshold\": \"%s\",\n", escaped_str);
                fprintf(outfile, "      \"isCritical\": %s\n", alert->is_critical ? "true" : "false");
                fprintf(outfile, "    }%s\n", (i == alerts->alert_count - 1) ? "" : ",");
            }
        }
        fprintf(outfile, "  ],");
        first_section_written = true;
    } else {
        if (first_section_written) fprintf(outfile, ",");
         fprintf(outfile, "\n  \"healthAlerts\": []"); // Lista vazia se não for NVMe ou sem alertas
         first_section_written = true;
    }

    // Seção 5: Resultados do Benchmark (se disponíveis)
    if (hybrid_ctx && hybrid_ctx->benchmark_mode && hybrid_ctx->num_benchmark_results_stored > 0) {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"benchmarkResults\": [\n");
        for (int i = 0; i < hybrid_ctx->num_benchmark_results_stored; ++i) {
            const nvme_access_result_t* res = &hybrid_ctx->benchmark_method_results[i];
            fprintf(outfile, "    {\n");
            escape_json_string(res->method_name, escaped_str, sizeof(escaped_str));
            fprintf(outfile, "      \"methodName\": \"%s\",\n", escaped_str);
            fprintf(outfile, "      \"success\": %s,\n", res->success ? "true" : "false");
            fprintf(outfile, "      \"executionTimeMs\": %lu,\n", res->execution_time_ms);
            fprintf(outfile, "      \"errorCode\": %lu\n", res->error_code);
            fprintf(outfile, "    }%s\n", (i == hybrid_ctx->num_benchmark_results_stored - 1) ? "" : ",");
        }
        fprintf(outfile, "  ]"); 
        first_section_written = true;
    } else {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"benchmarkResults\": []");
        first_section_written = true;
    }
    
    time_t now = time(NULL);
    struct tm ptm_utc;
    // Usar gmtime_s para obter UTC de forma segura
    if (gmtime_s(&ptm_utc, &now) == 0) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &ptm_utc);
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"reportGeneratedUtc\": \"%s\"\n", buffer);
    } else {
        if (first_section_written) fprintf(outfile, ",");
        fprintf(outfile, "\n  \"reportGeneratedUtc\": \"N/A\"\n");
    }

    fprintf(outfile, "}\n"); // Fim do objeto JSON principal

    if (outfile != stdout) {
        fclose(outfile);
        fprintf(stderr, "[INFO NVME_EXPORT_JSON] JSON report successfully written to %s\n", output_file_path);
        fflush(stderr);
    }
    return PAL_STATUS_SUCCESS;
}

// Stubs para outros formatos - devem ser implementados em fases futuras (provavelmente nunca)
/*
int nvme_export_to_xml(const char* device_path, const BasicDriveInfo* basic_info, const struct smart_data* sdata, const nvme_health_alerts_t* alerts, const nvme_hybrid_context_t* hybrid_ctx, const char* output_file_path) {
    fprintf(stderr, "[WARN NVME_EXPORT] XML export is not yet implemented.\n");
    return PAL_STATUS_UNSUPPORTED;
}

int nvme_export_to_csv(const char* device_path, const BasicDriveInfo* basic_info, const struct smart_data* sdata, const nvme_health_alerts_t* alerts, const nvme_hybrid_context_t* hybrid_ctx, const char* output_file_path) {
    fprintf(stderr, "[WARN NVME_EXPORT] CSV export is not yet implemented.\n");
    return PAL_STATUS_UNSUPPORTED;
}

int nvme_export_to_html(const char* device_path, const BasicDriveInfo* basic_info, const struct smart_data* sdata, const nvme_health_alerts_t* alerts, const nvme_hybrid_context_t* hybrid_ctx, const char* output_file_path) {
    fprintf(stderr, "[WARN NVME_EXPORT] HTML export is not yet implemented.\n");
    return PAL_STATUS_UNSUPPORTED;
}
*/ 
