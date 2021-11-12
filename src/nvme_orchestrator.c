#include "nvme_orchestrator.h"
#include "pal_shared_nvme.h" // For nvme_smart_log_parse (assuming it will be here or in smart.c)
#include <stdio.h>           // For fprintf, stderr (debugging)
#include <string.h>          // For memset, memcpy

// Placeholder for Linux adapted function - for future phases
/*
pal_status_t pal_linux_nvme_access_ioctl_admin_cmd_adapted(
    pal_drive_info_t *drive_info,
    uint8_t *buffer,
    uint32_t buffer_size,
    uint32_t *bytes_returned,
    nvme_hybrid_context_t *hybrid_ctx
);
*/

pal_status_t nvme_orchestrator_get_smart_data(
    const char *device_path,
    struct smart_data *out_smart_data,
    nvme_hybrid_context_t *hybrid_ctx
) {
    if (!device_path || !out_smart_data || !hybrid_ctx) {
        return PAL_STATUS_INVALID_PARAMETER;
    }

    // This check used drive_info, which is no longer a direct parameter.
    // This logic needs to be re-evaluated. For now, commenting out to allow compilation.
    // The orchestrator might need to fetch pal_drive_info_t first using device_path,
    // or this validation happens at a different layer.

    // Ensure raw buffer is available in hybrid_ctx
    // Corrected to use smart_cache.cached_data and NVME_LOG_PAGE_SIZE_BYTES
    if (sizeof(hybrid_ctx->smart_cache.cached_data) < NVME_LOG_PAGE_SIZE_BYTES) { // Sanity check, should always be equal if NVME_LOG_PAGE_SIZE_BYTES is used for array decl
        return PAL_STATUS_INVALID_PARAMETER; 
    }

    memset(out_smart_data, 0, sizeof(struct smart_data));
    out_smart_data->drive_type = DRIVE_TYPE_NVME;
    
    // Initialize relevant parts of hybrid_ctx last_operation_result
    hybrid_ctx->last_operation_result.success = FALSE; // Default to error
    hybrid_ctx->last_operation_result.error_code = (DWORD)PAL_STATUS_ERROR; 
    hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_PAL_GET_SMART;
    hybrid_ctx->last_operation_result.execution_time_ms = 0;
    memset(hybrid_ctx->last_operation_result.method_name, 0, sizeof(hybrid_ctx->last_operation_result.method_name));

    pal_status_t status = PAL_STATUS_ERROR;
    uint32_t bytes_returned = 0;

#ifdef _WIN32
    status = pal_get_smart_data(device_path, out_smart_data);
    
    if (status == PAL_STATUS_SUCCESS) {
        // Se pal_get_smart_data retornou SUCESSO, consideramos os dados fundamentalmente válidos
        // A verificação de is_nvme é para determinar o tipo de dados SMART recebidos.
        if (out_smart_data->is_nvme) {
            // Dados brutos já estão em out_smart_data->data.nvme.raw_health_log
            // e pal_get_smart_data deve ter preenchido os campos parseados de out_smart_data->data.nvme
            // Apenas precisamos copiar para o cache do hybrid_ctx se necessário e verificar bytes_returned.
            memcpy(hybrid_ctx->smart_cache.cached_data, &out_smart_data->data.nvme.raw_health_log, NVME_LOG_PAGE_SIZE_BYTES);
            bytes_returned = NVME_LOG_PAGE_SIZE_BYTES; // Assumindo que os dados completos foram lidos por pal_get_smart_data
            
            strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "pal_get_smart_data (NVMe OK)", _TRUNCATE);
            hybrid_ctx->last_operation_result.success = TRUE;
            // A lógica de parsing manual que estava aqui anteriormente foi removida pois pal_get_smart_data deve cuidar disso.

        } else { // Não é NVMe (portanto, deve ser ATA se pal_get_smart_data teve sucesso)
            status = PAL_STATUS_WRONG_DRIVE_TYPE; // Ou talvez isso não seja um erro se o orquestrador puder lidar com ATA.
                                                 // Por agora, tratamos como se esperasse apenas NVMe aqui.
            strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "pal_get_smart_data (Not NVMe)", _TRUNCATE);
            hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_UNKNOWN; // Ou um método ATA
            hybrid_ctx->last_operation_result.success = FALSE;
            hybrid_ctx->last_operation_result.error_code = (DWORD)status;
        }
    } else { // pal_get_smart_data falhou
        strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "pal_get_smart_data (Call Failed)", _TRUNCATE);
        hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_UNKNOWN;
        hybrid_ctx->last_operation_result.success = FALSE;
        hybrid_ctx->last_operation_result.error_code = (DWORD)status;
    }
    return status; 
#elif __linux__
    status = PAL_STATUS_UNSUPPORTED;
    strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "Linux_NYI", _TRUNCATE);
    hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_UNKNOWN;
    hybrid_ctx->last_operation_result.success = FALSE;
    hybrid_ctx->last_operation_result.error_code = (DWORD)status;
    return status;
#else
    status = PAL_STATUS_UNSUPPORTED;
    strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "Platform_NYI", _TRUNCATE);
    hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_UNKNOWN;
    hybrid_ctx->last_operation_result.success = FALSE;
    hybrid_ctx->last_operation_result.error_code = (DWORD)status;
    return status;
#endif
}