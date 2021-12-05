/**
 * @brief Contém informações básicas sobre uma unidade de disco.
 */
typedef struct {
    char path[256];         /**< O caminho do dispositivo (ex: \\.\PhysicalDrive0). */
    char model[257];        /**< O modelo do disco (40*4 + 1 para segurança). */
    char serial[21];        /**< O número de série do disco (20 + 1). */
} BasicDriveInfo; 