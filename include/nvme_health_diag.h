#ifndef MY_UNIQUE_NVME_ALERTS_H_GUARD_98765
#define MY_UNIQUE_NVME_ALERTS_H_GUARD_98765

#include "smart.h" // For struct smart_nvme
#include "nvme_hybrid.h" // For nvme_health_alerts_t (assuming it's here or in a common header)
// If nvme_health_alerts_t is not in nvme_hybrid.h, ensure its definition is accessible.

// Forward declaration if nvme_health_alerts_t is complex and defined elsewhere, 
// but better to include its actual definition.
// struct nvme_health_alerts_s; // Example if typedef is nvme_health_alerts_t

#ifdef __cplusplus
extern "C" {
#endif

void nvme_analyze_health_alerts(
    const struct smart_nvme *smart_log_subset, // Use the subset of SMART data
    nvme_health_alerts_t *alerts              // Pointer to the output alerts structure
);

#ifdef __cplusplus
}
#endif

#endif /* MY_UNIQUE_NVME_ALERTS_H_GUARD_98765 */ 