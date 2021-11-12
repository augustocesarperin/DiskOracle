#ifndef NVME_ORCHESTRATOR_H
#define NVME_ORCHESTRATOR_H

#include "pal.h"          // For pal_status_t and BasicDriveInfo (though not directly used by orchestrator input)
#include "smart.h"        // For struct smart_data
#include "nvme_hybrid.h"  // For nvme_hybrid_context_t

/**
 * @brief Main orchestrator function for retrieving SMART data from NVMe drives,
 *        potentially using caching, benchmarking, and multiple access methods.
 *
 * This function serves as the primary entry point for NVMe SMART data operations
 * when called from the main application logic (e.g., main.c). It coordinates
 * the use of different NVMe access methods, caching, and benchmarking based on
 * the provided hybrid_ctx.
 *
 * @param device_path Path to the target NVMe drive.
 * @param out_smart_data Pointer to a struct smart_data structure where the parsed
 *                       SMART data will be stored.
 * @param hybrid_ctx Pointer to an nvme_hybrid_context_t structure. This context
 *                   provides configuration for the orchestrator (e.g., cache settings,
 *                   benchmark mode, preferred access methods) and is also used to
 *                   return operational details and extended results (e.g., cache hit status,
 *                   benchmark timings, detailed alerts). The caller is responsible for
 *                   initializing the configuration aspects of this context. The orchestrator
 *                   will populate the results aspects.
 * @return pal_status_t PAL_STATUS_SUCCESS on success, or an appropriate error code
 *         on failure.
 */
pal_status_t nvme_orchestrator_get_smart_data(
    const char *device_path,
    struct smart_data *out_smart_data,
    nvme_hybrid_context_t *hybrid_ctx
);

typedef enum {
    ORCH_NVME_ACCESS_METHOD_SCSI_MINIPORT, // Attempted IOCTL_SCSI_MINIPORT with various ControlCodes
    ORCH_NVME_ACCESS_METHOD_SAT_PASSTHROUGH, // If drive is behind a SAT layer (relevant for some USB enclosures)
    ORCH_NVME_ACCESS_METHOD_PAL_GET_SMART, // Orchestrator used pal_get_smart_data
    ORCH_NVME_ACCESS_METHOD_UNKNOWN        // Could not determine or not applicable
} nvme_access_method_e;

#endif // NVME_ORCHESTRATOR_H 