#include "nvme_hybrid.h" // Corrigido para include direto
#include "pal.h"         // Corrigido para include direto
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <strsafe.h>
#include <time.h> // Para time_t, difftime, se usarmos para TTL de forma mais simples que SYSTEMTIME

// Função para inicializar/resetar o cache dentro do contexto
void nvme_cache_init(nvme_hybrid_context_t* context) {
    if (!context) return;
    fprintf(stderr, "[DEBUG NVME_CACHE] Initializing/Resetting SMART cache for device: %s\n", context->device_path);
    context->smart_cache.is_valid = FALSE;
    ZeroMemory(context->smart_cache.cached_data, NVME_LOG_PAGE_SIZE_BYTES);
    ZeroMemory(&context->smart_cache.last_update_time, sizeof(SYSTEMTIME));
    ZeroMemory(context->smart_cache.device_signature, sizeof(context->smart_cache.device_signature));
    // context->cache_duration_seconds e context->cache_enabled são configurados externamente (e.g., por CLI parser)
}

// Tenta obter dados SMART do cache.
// Retorna TRUE se hit e dados válidos, FALSE caso contrário.
// Se hit, preenche out_buffer, out_bytes_returned, e cache_hit_result.
BOOL nvme_cache_get(
    nvme_hybrid_context_t* context, 
    BYTE* out_buffer, 
    DWORD* out_bytes_returned,
    nvme_access_result_t* cache_hit_result // Para preencher se for cache hit
) {
    if (!context || !out_buffer || !out_bytes_returned || !cache_hit_result || !context->cache_enabled) {
        return FALSE;
    }

    fprintf(stderr, "[DEBUG NVME_CACHE] Checking cache for device: %s (Current Sig: '%s')\n", context->device_path, context->current_device_signature);

    if (!context->smart_cache.is_valid) {
        fprintf(stderr, "[DEBUG NVME_CACHE] Cache is invalid.\n");
        return FALSE;
    }

    // 1. Verificar assinatura do dispositivo (se implementado e disponível)
    if (strlen(context->current_device_signature) > 0 && 
        strncmp(context->current_device_signature, context->smart_cache.device_signature, sizeof(context->smart_cache.device_signature)) != 0) {
        fprintf(stderr, "[DEBUG NVME_CACHE] Device signature mismatch. Cache invalidated. Cached Sig: '%s', Current Sig: '%s'\n", 
                context->smart_cache.device_signature, context->current_device_signature);
        nvme_cache_invalidate(context); // Invalida o cache
        return FALSE;
    }

    // 2. Verificar TTL (Time-To-Live)
    SYSTEMTIME currentTimeSt;
    FILETIME currentTimeFt, lastUpdateFt;
    ULARGE_INTEGER currentTimeUl, lastUpdateUl;
    GetSystemTime(&currentTimeSt);

    if (!SystemTimeToFileTime(&context->smart_cache.last_update_time, &lastUpdateFt) || 
        !SystemTimeToFileTime(&currentTimeSt, &currentTimeFt)) {
        fprintf(stderr, "[ERROR NVME_CACHE] Could not convert system times to file times for TTL check.\n");
        nvme_cache_invalidate(context); // Invalida por precaução
        return FALSE;
    }

    lastUpdateUl.LowPart = lastUpdateFt.dwLowDateTime;
    lastUpdateUl.HighPart = lastUpdateFt.dwHighDateTime;
    currentTimeUl.LowPart = currentTimeFt.dwLowDateTime;
    currentTimeUl.HighPart = currentTimeFt.dwHighDateTime;

    ULONGLONG diff_100ns = currentTimeUl.QuadPart - lastUpdateUl.QuadPart;
    DWORD elapsed_seconds = (DWORD)(diff_100ns / 10000000ULL); // 100ns units to seconds

    fprintf(stderr, "[DEBUG NVME_CACHE] Cache age: %lu seconds. Cache duration: %lu seconds.\n", elapsed_seconds, context->cache_duration_seconds);

    if (elapsed_seconds >= context->cache_duration_seconds) {
        fprintf(stderr, "[DEBUG NVME_CACHE] Cache expired (age: %lu >= duration: %lu). Cache invalidated.\n", elapsed_seconds, context->cache_duration_seconds);
        nvme_cache_invalidate(context);
        return FALSE;
    }

    // Cache HIT!
    fprintf(stderr, "[INFO NVME_CACHE] Cache HIT for device %s! Age: %lu s.\n", context->device_path, elapsed_seconds);
    memcpy(out_buffer, context->smart_cache.cached_data, NVME_LOG_PAGE_SIZE_BYTES);
    *out_bytes_returned = NVME_LOG_PAGE_SIZE_BYTES;

    // Preencher method_result para o cache hit
    cache_hit_result->method_used = NVME_ACCESS_METHOD_CACHE;
    StringCchCopyA(cache_hit_result->method_name, sizeof(cache_hit_result->method_name), NVME_METHOD_NAME_CACHE);
    cache_hit_result->execution_time_ms = 0; // Cache access é considerado instantâneo para este propósito
    cache_hit_result->success = TRUE;
    cache_hit_result->error_code = 0;
    
    return TRUE;
}

// Atualiza o cache com novos dados SMART
void nvme_cache_update(
    nvme_hybrid_context_t* context, 
    const BYTE* data_to_cache, 
    DWORD data_size
) {
    if (!context || !data_to_cache || data_size < NVME_LOG_PAGE_SIZE_BYTES || !context->cache_enabled) {
        return;
    }

    fprintf(stderr, "[DEBUG NVME_CACHE] Updating cache for device: %s (Sig: '%s')\n", context->device_path, context->current_device_signature);
    
    memcpy(context->smart_cache.cached_data, data_to_cache, NVME_LOG_PAGE_SIZE_BYTES);
    GetSystemTime(&context->smart_cache.last_update_time);
    StringCchCopyA(context->smart_cache.device_signature, 
                   sizeof(context->smart_cache.device_signature), 
                   context->current_device_signature);
    context->smart_cache.is_valid = TRUE;
    
    fprintf(stderr, "[DEBUG NVME_CACHE] Cache updated successfully.\n");
}

// Invalida o cache (e.g., se uma operação de escrita no disco ocorreu, ou TTL expirou)
void nvme_cache_invalidate(nvme_hybrid_context_t* context) {
    if (!context) return;
    fprintf(stderr, "[DEBUG NVME_CACHE] Invalidating cache for device: %s\n", context->device_path);
    context->smart_cache.is_valid = FALSE;
    // Opcionalmente, zerar last_update_time e device_signature também
    // ZeroMemory(&context->smart_cache.last_update_time, sizeof(SYSTEMTIME));
    // ZeroMemory(context->smart_cache.device_signature, sizeof(context->smart_cache.device_signature));
}

// Gera uma assinatura de dispositivo simples baseada no modelo e serial.
// Esta é uma implementação básica; pode ser expandida (e.g. hash).
void nvme_cache_generate_signature(
    const char* model, 
    const char* serial, 
    char* out_signature, 
    size_t signature_buffer_size
) {
    if (!out_signature || signature_buffer_size == 0) return;
    
    if (model && serial) {
        StringCchPrintfA(out_signature, signature_buffer_size, "MODEL=%s&SERIAL=%s", model, serial);
    } else if (model) {
        StringCchPrintfA(out_signature, signature_buffer_size, "MODEL=%s", model);
    } else if (serial) {
        StringCchPrintfA(out_signature, signature_buffer_size, "SERIAL=%s", serial);
    } else {
        StringCchCopyA(out_signature, signature_buffer_size, "UNKNOWN_DEVICE");
    }
    // Garantir terminação nula em caso de truncamento por StringCchPrintfA
    out_signature[signature_buffer_size - 1] = '\0';
    fprintf(stderr, "[DEBUG NVME_CACHE] Generated device signature: '%s'\n", out_signature);
} 