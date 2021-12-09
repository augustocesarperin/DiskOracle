#include "nvme_orchestrator.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 

pal_status_t nvme_orchestrator_get_smart_data(
    const char *device_path,
    struct smart_data *out_smart_data,
    nvme_hybrid_context_t *hybrid_ctx
) {
    if (!device_path || !out_smart_data || !hybrid_ctx) {
        return PAL_STATUS_INVALID_PARAMETER;
    }

    if (sizeof(hybrid_ctx->smart_cache.cached_data) < NVME_LOG_PAGE_SIZE_BYTES) { // Sanity check, should always be equal if NVME_LOG_PAGE_SIZE_BYTES is used for array decl
        return PAL_STATUS_INVALID_PARAMETER; 
    }

    memset(out_smart_data, 0, sizeof(struct smart_data));
    out_smart_data->drive_type = DRIVE_TYPE_NVME;
    
    hybrid_ctx->last_operation_result.success = FALSE; 
    hybrid_ctx->last_operation_result.error_code = (DWORD)PAL_STATUS_ERROR; 
    hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_PAL_GET_SMART;
    hybrid_ctx->last_operation_result.execution_time_ms = 0;
    memset(hybrid_ctx->last_operation_result.method_name, 0, sizeof(hybrid_ctx->last_operation_result.method_name));

    pal_status_t status = PAL_STATUS_ERROR;
    uint32_t bytes_returned = 0;

#ifdef _WIN32
    status = pal_get_smart_data(device_path, out_smart_data);
    
    if (status == PAL_STATUS_SUCCESS) {
        if (out_smart_data->is_nvme) {
            // Apenas precisamos copiar para o cache do hybrid_ctx se necessário e verificar bytes_returned.
            memcpy(hybrid_ctx->smart_cache.cached_data, &out_smart_data->data.nvme.raw_health_log, NVME_LOG_PAGE_SIZE_BYTES);
            bytes_returned = NVME_LOG_PAGE_SIZE_BYTES; 
            strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "pal_get_smart_data (NVMe OK)", _TRUNCATE);
            hybrid_ctx->last_operation_result.success = TRUE;
          

        } else { 
            status = PAL_STATUS_WRONG_DRIVE_TYPE; 
                                               
            strncpy_s(hybrid_ctx->last_operation_result.method_name, sizeof(hybrid_ctx->last_operation_result.method_name), "pal_get_smart_data (Not NVMe)", _TRUNCATE);
            hybrid_ctx->last_operation_result.method_used = (nvme_access_method_t)ORCH_NVME_ACCESS_METHOD_UNKNOWN; // Ou um método ATA
            hybrid_ctx->last_operation_result.success = FALSE;
            hybrid_ctx->last_operation_result.error_code = (DWORD)status;
        }
    } else { 
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