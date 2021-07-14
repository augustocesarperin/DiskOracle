#ifndef SMART_ATA_H
#define SMART_ATA_H

#include "smart.h"
#include "pal.h"

#ifdef _WIN32
#include <windows.h>
typedef HANDLE PAL_DEV;
#else
typedef void* PAL_DEV;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Função pra ler dados SMART de discos ATA/SATA
 * 
 * Usa ATA Pass-Through pra pegar infos de saúde e desempenho
 * do disco. Lê atributos como temperatura, erros de leitura, etc.
 * 
 * @param device Handle do dispositivo (HANDLE do Windows)
 * @param out Ponteiro pra struct que vai receber os dados
 * @return int Status: PAL_STATUS_SUCCESS se OK, código de erro se falhar
 */
int smart_read_ata(PAL_DEV device, struct smart_data* out);

#ifdef __cplusplus
}
#endif

#endif /* SMART_ATA_H */ 