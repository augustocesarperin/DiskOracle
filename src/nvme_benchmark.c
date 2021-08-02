#include "../include/nvme_hybrid.h" // Ou "nvme_hybrid.h" dependendo da config de include path
#include <stdio.h>
#include <string.h>
#include <strsafe.h>

// Inicializa/reseta os dados de benchmark
void nvme_benchmark_init(nvme_hybrid_context_t* context) {
    if (!context) return;
    fprintf(stderr, "[DEBUG NVME_BENCHMARK] Initializing benchmark data.\n");
    ZeroMemory(context->benchmark_method_results, sizeof(context->benchmark_method_results));
    context->num_benchmark_results_stored = 0;
    // context->benchmark_iterations é configurado externamente 
    // Por padrão definir para 1 ou 3 se não for configurado.
    if (context->benchmark_iterations == 0) {
        context->benchmark_iterations = 1; 
    }
}

void nvme_benchmark_record_result(
    nvme_hybrid_context_t* context, 
    const nvme_access_result_t* result_to_record
) {
    if (!context || !result_to_record) return;

    if (context->num_benchmark_results_stored < MAX_NVME_ACCESS_METHODS) {
        context->benchmark_method_results[context->num_benchmark_results_stored] = *result_to_record;
        context->num_benchmark_results_stored++;
        fprintf(stderr, "[DEBUG NVME_BENCHMARK] Recorded result for method: %s (Success: %d, Time: %lu ms)\n", 
                result_to_record->method_name, result_to_record->success, result_to_record->execution_time_ms);
    } else {
        fprintf(stderr, "[WARN NVME_BENCHMARK] Cannot record more benchmark results, array full (max: %d).\n", MAX_NVME_ACCESS_METHODS);
    }
}

// Imprime um relatório simples dos resultados do benchmark no stderr
void nvme_benchmark_print_report(const nvme_hybrid_context_t* context) {
    if (!context || context->num_benchmark_results_stored == 0) {
        fprintf(stderr, "[INFO NVME_BENCHMARK] No benchmark results to display.\n");
        return;
    }

    fprintf(stdout, "\n📊 NVMe Access Method Benchmark Results (%d iteration(s) per enabled method):\n", context->benchmark_iterations);
    fprintf(stdout, "=====================================================================================\n");
    fprintf(stdout, "%-30s | %-10s | %-15s | %-10s\n", "Method Name", "Success", "Time (ms)", "Error Code");
    fprintf(stdout, "-------------------------------------------------------------------------------------\n");

    nvme_access_method_t fastest_successful_method = NVME_ACCESS_METHOD_NONE;
    DWORD fastest_time = 0xFFFFFFFF;

    for (int i = 0; i < context->num_benchmark_results_stored; ++i) {
        const nvme_access_result_t* res = &context->benchmark_method_results[i];
        fprintf(stdout, "%-30s | %-10s | %-15lu | 0x%-8lX\n", 
                res->method_name, 
                res->success ? "Yes" : "No", 
                res->execution_time_ms, 
                res->error_code);
        if (res->success && res->execution_time_ms < fastest_time) {
            fastest_time = res->execution_time_ms;
            fastest_successful_method = res->method_used;
        }
    }
    fprintf(stdout, "=====================================================================================\n");
    if (fastest_successful_method != NVME_ACCESS_METHOD_NONE) {
        char fastest_method_name_str[64] = "Unknown";
        switch(fastest_successful_method) {
            case NVME_ACCESS_METHOD_QUERY_PROPERTY: StringCchCopyA(fastest_method_name_str, 64, NVME_METHOD_NAME_QUERY_PROPERTY); break;
            case NVME_ACCESS_METHOD_PROTOCOL_COMMAND: StringCchCopyA(fastest_method_name_str, 64, NVME_METHOD_NAME_PROTOCOL_COMMAND); break;
            case NVME_ACCESS_METHOD_SCSI_PASSTHROUGH: StringCchCopyA(fastest_method_name_str, 64, NVME_METHOD_NAME_SCSI_PASSTHROUGH); break;
            // Add other cases as methods are implemented
            default: break;
        }
        fprintf(stdout, "🚀 Fastest successful method: %s (%lu ms)\n", fastest_method_name_str, fastest_time);
    } else {
        fprintf(stdout, "⚠️ No method was successful in the benchmark.\n");
    }
    fflush(stdout);
    fflush(stderr); 
} 