/*
 * nvme_benchmark.c
 * ----------------
 *
 * ATENÇÃO: Funcionalidade de Benchmark para Métodos de Acesso NVMe.
 *
 * PROPÓSITO:
 * Este módulo foi projetado para testar e comparar o desempenho (velocidade e sucesso)
 * de diferentes métodos que o sistema pode usar para acessar dispositivos NVMe
 * (ex: para obter dados SMART). É primariamente uma FERRAMENTA DE DESENVOLVIMENTO E DIAGNÓSTICO.
 *
 * STATUS ATUAL DE INTEGRAÇÃO:
 * - A funcionalidade de benchmark definida neste arquivo NÃO é ativada por padrão
 *   durante a execução normal do utilitário DiskOracle (ex: via comando --smart).
 * - A ativação do modo benchmark (ex: através de `hybrid_ctx.benchmark_mode = true;`)
 *   precisaria ser feita explicitamente em outras partes do código (ex: `main.c`
 *   ou `nvme_orchestrator.c`) para que este módulo seja executado.
 *
 * CONSIDERAÇÕES PARA O USUÁRIO FINAL:
 * - Na sua configuração atual, este arquivo NÃO impacta diretamente a funcionalidade
 *   principal do DiskOracle como vista pelo usuário final.
 * - Seu conteúdo pode ser considerado \"peso morto\" no build final se a funcionalidade
 *   de benchmark não for exposta ou utilizada ativamente para otimizar o programa.
 *
 * MODIFICAÇÕES RECENTES (Outubro 2023 - Exemplo de data):
 * - Mensagens de depuração internas (`fprintf` com \"[DEBUG NVME_BENCHMARK]\") foram removidas
 *   para simplificar o código fonte, já que o relatório final do benchmark
 *   (`nvme_benchmark_print_report`) é a saída principal desta funcionalidade quando ativa.
 * - Foram aplicadas melhorias de portabilidade (ex: memset, snprintf, stdint.h).
 */

#include "../include/nvme_hybrid.h" 
#include <stdio.h>
#include <string.h>
#include <stdint.h> // Adicionado para uint32_t e UINT32_MAX

// #include <strsafe.h> // Não é mais necessário pois StringCchCopyA foi removido

// logging.h poderia ser incluído por nvme_hybrid.h ou diretamente se DEBUG_PRINT fosse usado.
// Como os fprintfs de debug foram removidos e não foram substituídos por DEBUG_PRINT,
// este include direto pode não ser estritamente necessário aqui.
// #include "../include/logging.h"


// Inicializa/reseta os dados de benchmark
void nvme_benchmark_init(nvme_hybrid_context_t* context) {
    if (!context) return;
    // fprintf(stderr, \"[DEBUG NVME_BENCHMARK] Initializing benchmark data.\\n\"); // Removido
    
    // ZeroMemory(context->benchmark_method_results, sizeof(context->benchmark_method_results));
    memset(context->benchmark_method_results, 0, sizeof(context->benchmark_method_results));
    
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
        // fprintf(stderr, \"[DEBUG NVME_BENCHMARK] Recorded result for method: %s (Success: %d, Time: %lu ms)\\n\", 
        //         result_to_record->method_name, result_to_record->success, result_to_record->execution_time_ms); // Removido
    } else {
        fprintf(stderr, "[WARN NVME_BENCHMARK] Cannot record more benchmark results, array full (max: %d).\n", MAX_NVME_ACCESS_METHODS);
    }
}

// Imprime um relatório simples dos resultados do benchmark
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
    // DWORD fastest_time = 0xFFFFFFFF;
    uint32_t fastest_time = UINT32_MAX; // Usando uint32_t de stdint.h

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
            case NVME_ACCESS_METHOD_QUERY_PROPERTY: 
                snprintf(fastest_method_name_str, sizeof(fastest_method_name_str), "%s", NVME_METHOD_NAME_QUERY_PROPERTY); 
                break;
            case NVME_ACCESS_METHOD_PROTOCOL_COMMAND: 
                snprintf(fastest_method_name_str, sizeof(fastest_method_name_str), "%s", NVME_METHOD_NAME_PROTOCOL_COMMAND); 
                break;
            case NVME_ACCESS_METHOD_SCSI_PASSTHROUGH: 
                snprintf(fastest_method_name_str, sizeof(fastest_method_name_str), "%s", NVME_METHOD_NAME_SCSI_PASSTHROUGH); 
                break;
            // Add other cases as methods are implemented
            default: break;
        }
        fprintf(stdout, "🚀 Fastest successful method: %s (%u ms)\n", fastest_method_name_str, fastest_time); // Changed %lu to %u for uint32_t
    } else {
        fprintf(stdout, "⚠️ No method was successful in the benchmark.\n");
    }
    fflush(stdout);
    fflush(stderr);
} 