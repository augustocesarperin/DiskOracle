#ifndef PAL_H
#define PAL_H

#include <stdint.h>
#include <stdbool.h>
#include "info.h" // Depende das estruturas de dados corretas
#include "surface.h" // Adicionado para que scan_state_t seja conhecido

typedef void (*pal_scan_callback)(const scan_state_t* state, void* user_data);

// Define um status comum para as funções da PAL
typedef int pal_status_t;

// === Constantes de Status ===
#define PAL_STATUS_SUCCESS 0
#define PAL_STATUS_ERROR 1
#define PAL_STATUS_INVALID_PARAMETER 2
#define PAL_STATUS_IO_ERROR 3
#define PAL_STATUS_NO_MEMORY 4
#define PAL_STATUS_DEVICE_NOT_FOUND 6
#define PAL_STATUS_ACCESS_DENIED 7
#define PAL_STATUS_UNSUPPORTED 8
#define PAL_STATUS_BUFFER_TOO_SMALL 10
#define PAL_STATUS_NO_DRIVES_FOUND 11
#define PAL_STATUS_SMART_NOT_SUPPORTED 12
#define PAL_STATUS_WRONG_DRIVE_TYPE 16      
#define PAL_STATUS_ERROR_DATA_UNDERFLOW 17 
#define PAL_STATUS_DEVICE_ERROR 18        


// Inicialização e limpeza
pal_status_t pal_initialize(void);
void pal_cleanup(void);

// Listagem e Informações de Drive
pal_status_t pal_list_drives(DriveInfo* drives, int max_drives, int* drive_count);
pal_status_t pal_get_basic_drive_info(const char* device_path, BasicDriveInfo* drive_info);
int64_t pal_get_device_size(const char *device_path);

// S.M.A.R.T.
struct smart_data; 
pal_status_t pal_get_smart_data(const char *device_path, struct smart_data *out);

// Scan Superfície
pal_status_t pal_do_surface_scan(void *handle, unsigned long long start_lba, unsigned long long lba_count, pal_scan_callback callback, void *user_data);
void* pal_open_device(const char *device_path);
pal_status_t pal_close_device(void *handle);

// Funções de utilidade para a TUI
void pal_clear_screen(void);
void pal_wait_for_keypress(void);
int pal_get_char_input(void);
bool pal_get_string_input(char* buffer, size_t buffer_size);
pal_status_t pal_get_terminal_size(int* width, int* height);

// Funções de manipulação de sistema de arquivos
pal_status_t pal_create_directory(const char *path);
pal_status_t pal_get_current_directory(char* buffer, size_t size);

// Tradução de Erros
const char* pal_get_error_string(pal_status_t status);

#endif // PAL_H
