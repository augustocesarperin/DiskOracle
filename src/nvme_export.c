#include "nvme_export.h"
#include "pal.h"
#include "smart.h"
#include <stdio.h>
#include <string.h>
#include <strsafe.h>
#include <time.h>    // Para time_t, strftime, localtime_s
#include <windows.h> // Para StringCchPrintfA, StringCchCopyA

// Helper para escapar strings JSON (implementação MUITO básica)
static void escape_json_string(const char* input, char* output, size_t out_size) {
    if (!input || !output || out_size == 0) {
        if (output && out_size > 0) output[0] = '\0';
        return;
    }
    // TODO: Implementar escaping real para caracteres como ", \, \n, \r, \t, etc.
    // Por enquanto, apenas copia, o que pode quebrar o JSON se houver caracteres especiais.
    StringCchCopyA(output, out_size, input);
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

    fprintf(stderr, "[DEBUG NVME_EXPORT_JSON] Beginning JSON export for %s\n", device_path ? device_path : "Unknown Device");
    fflush(stderr);

    if (output_file_path) {
        fprintf(stderr, "[DEBUG NVME_EXPORT_JSON] Output target: %s\n", output_file_path);
        fflush(stderr);
        errno_t err = fopen_s(&outfile, output_file_path, "w");
        if (err != 0 || !outfile) {
            fprintf(stderr, "[ERROR NVME_EXPORT_JSON] Failed to open output file: '%s'. Error code: %d\n", output_file_path, err);
            fflush(stderr);
            return PAL_STATUS_OUTPUT_ERROR;
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
        fprintf(outfile, "\n    \"firmwareRevision\": \"N/A (Not in BasicDriveInfo)\",");
        escape_json_string(basic_info->type, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"driveType\": \"%s\",", escaped_str);
        escape_json_string(basic_info->bus_type, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "\n    \"busType\": \"%s\"", escaped_str);
    } else {
        fprintf(outfile, "\n    \"model\": \"N/A\",\n    \"serialNumber\": \"N/A\",\n    \"firmwareRevision\": \"N/A\",\n    \"driveType\": \"N/A\",\n    \"busType\": \"N/A\"");
    }
    fprintf(outfile, "\n  },"); 

    
    if (hybrid_ctx) {
        fprintf(outfile, "\n  \"accessMethodUsed\": {\n");
        escape_json_string(hybrid_ctx->last_operation_result.method_name, escaped_str, sizeof(escaped_str));
        fprintf(outfile, "    \"methodName\": \"%s\",\n", escaped_str);
        fprintf(outfile, "    \"success\": %s,\n", hybrid_ctx->last_operation_result.success ? "true" : "false");
        fprintf(outfile, "    \"executionTimeMs\": %lu,\n", hybrid_ctx->last_operation_result.execution_time_ms);
        fprintf(outfile, "    \"errorCode\": %lu\n", hybrid_ctx->last_operation_result.error_code);
        fprintf(outfile, "  },"); 
    }

    // SMART (NVMe ou ATA)
    if (sdata && hybrid_ctx && hybrid_ctx->last_operation_result.success) {
        if (sdata->is_nvme) {
            const struct nvme_smart_log* nvme = &sdata->data.nvme;
            fprintf(outfile, "\n  \"smartLogNvme\": {\n");
            fprintf(outfile, "    \"criticalWarning\": %u,\n", nvme->critical_warning);
            fprintf(outfile, "    \"temperatureKelvin\": %u,\n", nvme->temperature);
            fprintf(outfile, "    \"availableSparePercent\": %u,\n", nvme->avail_spare);
            fprintf(outfile, "    \"availableSpareThresholdPercent\": %u,\n", nvme->spare_thresh);
            fprintf(outfile, "    \"percentageUsed\": %u,\n", nvme->percent_used);
            uint64_t temp_u64;
            memcpy(&temp_u64, nvme->data_units_read, sizeof(uint64_t));
            fprintf(outfile, "    \"dataUnitsRead_x1000_512B\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->data_units_written, sizeof(uint64_t));
            fprintf(outfile, "    \"dataUnitsWritten_x1000_512B\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->host_read_commands, sizeof(uint64_t));
            fprintf(outfile, "    \"hostReadCommands\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->host_write_commands, sizeof(uint64_t));
            fprintf(outfile, "    \"hostWriteCommands\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->controller_busy_time, sizeof(uint64_t));
            fprintf(outfile, "    \"controllerBusyTimeMinutes\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->power_cycles, sizeof(uint64_t));
            fprintf(outfile, "    \"powerCycles\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->power_on_hours, sizeof(uint64_t));
            fprintf(outfile, "    \"powerOnHours\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->unsafe_shutdowns, sizeof(uint64_t));
            fprintf(outfile, "    \"unsafeShutdowns\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->media_errors, sizeof(uint64_t));
            fprintf(outfile, "    \"mediaAndDataIntegrityErrors\": %llu,\n", temp_u64);
            memcpy(&temp_u64, nvme->num_err_log_entries, sizeof(uint64_t));
            fprintf(outfile, "    \"numberOfErrorInformationLogEntries\": %llu\n", temp_u64);
            // Adicionar outros campos conforme necessário
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
        fprintf(outfile, "\n  \"smartLog\": { \"status\": \"SMART data not available or fetch failed.\" },");
    }

    // Seção 4: Alertas de Saúde (Apenas para NVMe por enquanto)
    if (alerts && sdata && sdata->is_nvme) {
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
    } else {
         fprintf(outfile, "\n  \"healthAlerts\": [],"); // Lista vazia se não for NVMe ou sem alertas
    }

    // Seção 5: Resultados do Benchmark (se disponíveis)
    if (hybrid_ctx && hybrid_ctx->benchmark_mode && hybrid_ctx->num_benchmark_results_stored > 0) {
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
    } else {
        fprintf(outfile, "\n  \"benchmarkResults\": []");
    }
    
    // Timestamp da Geração do Relatório (deve ser o último item ANTES do '}')
    time_t now = time(NULL);
    struct tm ptm_utc;
    // Usar gmtime_s para obter UTC de forma segura
    if (gmtime_s(&ptm_utc, &now) == 0) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &ptm_utc);
        fprintf(outfile, ",\n  \"reportGeneratedUtc\": \"%s\"\n", buffer);
    } else {
        fprintf(outfile, ",\n  \"reportGeneratedUtc\": \"N/A\"\n");
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