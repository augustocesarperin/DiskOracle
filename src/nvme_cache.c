#include "nvme_hybrid.h" // Contém as novas definições de nvme_global_cache_t, etc.
#include "smart.h"       // Para struct smart_nvme
#include "logging.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// Variável de cache global estática
static nvme_global_cache_t g_nvme_cache;
static bool g_cache_initialized = false;

void nvme_cache_global_init(unsigned int duration_seconds) {
    DEBUG_PRINT("Initializing global NVMe cache. Duration: %u seconds.", duration_seconds);
    memset(&g_nvme_cache, 0, sizeof(nvme_global_cache_t));
    g_nvme_cache.cache_duration_seconds = (duration_seconds > 0) ? duration_seconds : DEFAULT_NVME_CACHE_AGE_SECONDS;
    // Inicializa todas as entradas como inválidas
    for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        g_nvme_cache.entries[i].is_valid = false;
    }
    g_nvme_cache.count = 0; // Redundante devido ao memset, mas explícito.
    g_cache_initialized = true;
    DEBUG_PRINT("Global NVMe cache initialized. Max entries: %d, Cache age: %u s.", MAX_CACHE_ENTRIES, g_nvme_cache.cache_duration_seconds);
}

void nvme_cache_global_cleanup(void) {
    DEBUG_PRINT("Cleaning up global NVMe cache.");
    // Com um cache estático global, não há muita limpeza de memória para fazer,
    // mas podemos invalidar todas as entradas ou resetar o estado.
    nvme_cache_global_invalidate_all();
    g_cache_initialized = false;
}

nvme_cache_item_t* nvme_cache_global_get(const char* key) {
    if (!g_cache_initialized) {
        DEBUG_PRINT("Cache not initialized. Call nvme_cache_global_init() first.");
        // Alternativamente, poderia inicializar aqui com defaults, mas é melhor que seja explícito.
        nvme_cache_global_init(DEFAULT_NVME_CACHE_AGE_SECONDS); 
    }
    if (!key) {
        DEBUG_PRINT("nvme_cache_global_get: Chave nula fornecida.");
        return NULL;
    }

    DEBUG_PRINT("Attempting to get cache for key: %s", key);
    for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        if (g_nvme_cache.entries[i].is_valid && strcmp(g_nvme_cache.entries[i].key, key) == 0) {
            // Chave encontrada, verificar expiração
            time_t current_time = time(NULL);
            if (difftime(current_time, g_nvme_cache.entries[i].timestamp) < g_nvme_cache.cache_duration_seconds) {
                DEBUG_PRINT("Cache HIT for key: %s. Age: %.0f s.", key, difftime(current_time, g_nvme_cache.entries[i].timestamp));
                return &g_nvme_cache.entries[i];
            } else {
                DEBUG_PRINT("Cache STALE for key: %s. Age: %.0f s. Max age: %u s. Invalidating.", 
                            key, difftime(current_time, g_nvme_cache.entries[i].timestamp), g_nvme_cache.cache_duration_seconds);
                g_nvme_cache.entries[i].is_valid = false; // Marca como inválida/expirada
                // Não decrementamos g_nvme_cache.count aqui, pois a entrada ainda ocupa um slot,
                // apenas não é mais válida. Será sobrescrita por um novo update.
                return NULL; // Tratar como cache miss
            }
        }
    }
    DEBUG_PRINT("Cache MISS for key: %s", key);
    return NULL;
}

void nvme_cache_global_update(const char* key, const struct smart_nvme* health_log_to_cache) {
    if (!g_cache_initialized) {
        // Considerar inicializar com defaults se chamado antes do init explícito
        DEBUG_PRINT("Cache not initialized. Updating will proceed after implicit init.");
        nvme_cache_global_init(DEFAULT_NVME_CACHE_AGE_SECONDS);
    }
    if (!key || !health_log_to_cache) {
        DEBUG_PRINT("nvme_cache_global_update: Chave nula ou dados nulos fornecidos.");
        return;
    }

    DEBUG_PRINT("Attempting to update cache for key: %s", key);
    int found_idx = -1;
    int oldest_idx = -1;
    time_t oldest_time = 0;

    // Tentar encontrar a entrada existente ou a primeira inválida/vaga
    for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        if (g_nvme_cache.entries[i].is_valid && strcmp(g_nvme_cache.entries[i].key, key) == 0) {
            found_idx = i;
            break;
        }
        if (!g_nvme_cache.entries[i].is_valid) {
            if (found_idx == -1 || !g_nvme_cache.entries[found_idx].is_valid ) { // Preferir uma entrada totalmente nova/inválida
                 found_idx = i; // Encontrou um slot inválido/vazio
            }
        }
        // Para política de substituição LRU (ou mais antiga se não houver vagas)
        if (i == 0 || g_nvme_cache.entries[i].timestamp < oldest_time) {
            oldest_time = g_nvme_cache.entries[i].timestamp;
            oldest_idx = i;
        }
    }

    if (found_idx != -1 && !g_nvme_cache.entries[found_idx].is_valid) {
        // Encontrou um slot novo/inválido, e não existia uma entrada válida com essa chave.
        // Se a entrada encontrada já era inválida, não precisa incrementar o count.
        // Se era uma entrada nova (além do count atual e dentro de MAX_CACHE_ENTRIES), incrementa.
        if (g_nvme_cache.count < MAX_CACHE_ENTRIES && found_idx >= g_nvme_cache.count) {
             // Este cenário é um pouco confuso com a lógica atual de `found_idx` para "primeiro slot inválido".
             // Simplificação: se vamos usar um slot, e ele era `is_valid == false` e era um slot que não estava "contado", incrementamos.
             // Mas a forma mais simples é se `found_idx` é um slot novo e não apenas uma chave existente. 
             // Vamos refinar: se achamos a chave, usamos `found_idx`. Senão, achamos um slot para novo.
        }
    }

    int target_idx = -1;
    // 1. Tentar atualizar entrada existente
    for(int i=0; i < MAX_CACHE_ENTRIES; ++i) {
        if(g_nvme_cache.entries[i].is_valid && strcmp(g_nvme_cache.entries[i].key, key) == 0) {
            target_idx = i;
            DEBUG_PRINT("Updating existing cache entry at index %d for key: %s", target_idx, key);
            break;
        }
    }

    // 2. Se não encontrou, tentar encontrar um slot inválido
    if (target_idx == -1) {
        for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
            if (!g_nvme_cache.entries[i].is_valid) {
                target_idx = i;
                DEBUG_PRINT("Using new/invalid cache slot at index %d for key: %s", target_idx, key);
                if (g_nvme_cache.count < MAX_CACHE_ENTRIES) { // Apenas incrementa se estivermos realmente adicionando a um slot "novo" para o contador
                    // Esta lógica de count pode ser removida se MAX_CACHE_ENTRIES for o único limite.
                    // O count era mais para a lista encadeada original. Para array, a validade do slot é mais importante.
                }
                break;
            }
        }
    }

    // 3. Se cache cheio e nenhuma entrada existente para a chave, usar política de substituição (mais antigo)
    if (target_idx == -1) {
        if (MAX_CACHE_ENTRIES > 0) {
            target_idx = (oldest_idx != -1) ? oldest_idx : 0; // Fallback para 0 se algo der errado com oldest_idx
            DEBUG_PRINT("Cache full. Evicting entry at index %d (key: %s) for new key: %s", 
                        target_idx, g_nvme_cache.entries[target_idx].key, key);
        } else {
            DEBUG_PRINT("Cache size is 0. Cannot update.");
            return; // Cache tem tamanho 0, não pode armazenar nada.
        }
    }
    
    // Atualizar a entrada no target_idx
    // strncpy_s(g_nvme_cache.entries[target_idx].key, NVME_CACHE_KEY_MAX_LEN, key, _TRUNCATE);
    snprintf(g_nvme_cache.entries[target_idx].key, NVME_CACHE_KEY_MAX_LEN, "%s", key);
    memcpy(&g_nvme_cache.entries[target_idx].health_log, health_log_to_cache, sizeof(struct smart_nvme));
    g_nvme_cache.entries[target_idx].timestamp = time(NULL);
    g_nvme_cache.entries[target_idx].is_valid = true;

    DEBUG_PRINT("Cache updated for key: %s at index %d", key, target_idx);
}

void nvme_cache_global_invalidate(const char* key) {
    if (!g_cache_initialized || !key) return;
    DEBUG_PRINT("Invalidating cache for key: %s", key);
    for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        if (g_nvme_cache.entries[i].is_valid && strcmp(g_nvme_cache.entries[i].key, key) == 0) {
            g_nvme_cache.entries[i].is_valid = false;
            DEBUG_PRINT("Cache entry for key %s at index %d invalidated.", key, i);
            return; // Assume chaves são únicas
        }
    }
    DEBUG_PRINT("Key %s not found in cache for invalidation.", key);
}

void nvme_cache_global_invalidate_all(void) {
    if (!g_cache_initialized) return;
    DEBUG_PRINT("Invalidating all global NVMe cache entries.");
    for (int i = 0; i < MAX_CACHE_ENTRIES; ++i) {
        g_nvme_cache.entries[i].is_valid = false;
    }
}

// Remover funções antigas que usavam nvme_hybrid_context_t diretamente para o cache.
// As funções nvme_cache_init, nvme_cache_get, nvme_cache_update, nvme_cache_invalidate 
// e nvme_cache_generate_signature que estavam aqui antes e operavam no contexto foram removidas 
// e substituídas pelas versões _global_.
// A função nvme_cache_generate_signature pode ser mantida se for útil para criar a chave externamente.

// Função para gerar uma assinatura de dispositivo (chave de cache)
// Pode ser chamada externamente para criar a chave antes de usar as funções de cache.
void nvme_cache_generate_signature(const char* model, const char* serial, char* out_key, size_t key_buffer_size) {
    if (!out_key || key_buffer_size == 0) return;
    
    // Usar o serial como chave primária se disponível, pois é mais provável ser único.
    // Model pode ser usado como fallback ou parte de uma chave mais complexa se necessário.
    if (serial && strlen(serial) > 0) {
        snprintf(out_key, key_buffer_size, "NVME_HEALTH_SERIAL_%s", serial);
    } else if (model && strlen(model) > 0) {
        snprintf(out_key, key_buffer_size, "NVME_HEALTH_MODEL_%s", model);
    } else {
        snprintf(out_key, key_buffer_size, "NVME_HEALTH_UNKNOWN_DEVICE");
    }
    // Garantir terminação nula em caso de truncamento por snprintf
    out_key[key_buffer_size - 1] = '\0';
    DEBUG_PRINT("Generated cache key: '%s'", out_key);
} 