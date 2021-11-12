#ifndef NVME_EXPORT_H
#define NVME_EXPORT_H

#include "pal.h"          
#include "nvme_hybrid.h"  // Para nvme_health_alerts_t, nvme_hybrid_context_t (para resultados de benchmark)
#include <stdio.h>       // Para FILE*

typedef enum {
    EXPORT_FORMAT_JSON,
    EXPORT_FORMAT_XML,
    EXPORT_FORMAT_CSV,
    EXPORT_FORMAT_HTML,
    EXPORT_FORMAT_UNKNOWN
} export_format_t;

// Função principal de exportação para JSON
// Retorna PAL_STATUS_SUCCESS ou um código de erro
int nvme_export_to_json(
    const char* device_path,                  
    const BasicDriveInfo* basic_info,        
    const struct smart_data* sdata,           
    const nvme_health_alerts_t* alerts,       
    const nvme_hybrid_context_t* hybrid_ctx,  // Contexto híbrido, para resultados de benchmark e método usado
    const char* output_file_path              // Caminho do arquivo de saída. Se NULL, imprime para stdout.
);

// Outras funções de exportação (stubs por enquanto)
// int nvme_export_to_xml(...);
// int nvme_export_to_csv(...);
// int nvme_export_to_html(...);

#endif // NVME_EXPORT_H 