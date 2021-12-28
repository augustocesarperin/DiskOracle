#ifndef NVME_EXPORT_H
#define NVME_EXPORT_H

#include "pal.h"          
#include "nvme_hybrid.h"  
#include <stdio.h>       

typedef enum {
    EXPORT_FORMAT_JSON,
    EXPORT_FORMAT_XML,
    EXPORT_FORMAT_CSV,
    EXPORT_FORMAT_HTML,
    EXPORT_FORMAT_UNKNOWN
} export_format_t;

int nvme_export_to_json(
    const char* device_path,                  
    const BasicDriveInfo* basic_info,        
    const struct smart_data* sdata,           
    const nvme_health_alerts_t* alerts,       
    const nvme_hybrid_context_t* hybrid_ctx,  // Contexto híbrido, para resultados de benchmark
    const char* output_file_path              // Caminho do arquivo de saída. Se NULL, imprime para stdout.
);



#endif // NVME_EXPORT_H 