#include "pal.h"
#include "smart.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <sys/param.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/nvme/IONVMeController.h>
#include <IOKit/ata/IOATAStorageAdapterDefinitions.h>
#include <IOKit/ata/IOATASmartUserClient.h>
#include <IOKit/IOCFPlugIn.h>

#include <unistd.h>
#include <sys/sysctl.h>

#define kIOPropertyNVMeSMARTCapableKey "NVMe SMART Capable"

#define kIONVMeSMARTUserClientTypeID CFUUIDGetConstantUUIDWithBytes(NULL, \
    0xAA, 0x0F, 0xA6, 0xF9, 0xC2, 0xD6, 0x45, 0x7F, 0xB1, 0x0B, \
    0x59, 0xA1, 0x32, 0x53, 0x29, 0x2F)

#define kIONVMeSMARTInterfaceID CFUUIDGetConstantUUIDWithBytes(NULL, \
    0xcc, 0xd1, 0xdb, 0x19, 0xfd, 0x9a, 0x4d, 0xaf, 0xbf, 0x95, \
    0x12, 0x45, 0x4b, 0x23, 0xa, 0xb6)

typedef struct {
    uint8_t  critical_warning;
    uint16_t temperature;
    uint8_t  available_spare;
    uint8_t  available_spare_threshold;
    uint8_t  percentage_used;
    uint8_t  reserved1[26];
    uint8_t  data_units_read[16];
    uint8_t  data_units_written[16];
    uint8_t  host_read_commands[16];
    uint8_t  host_write_commands[16];
    uint8_t  controller_busy_time[16];
    uint8_t  power_cycles[16];
    uint8_t  power_on_hours[16];
    uint8_t  unsafe_shutdowns[16];
    uint8_t  media_errors[16];
    uint8_t  num_error_log_entries[16];
    uint32_t warning_composite_temperature_time;
    uint32_t critical_composite_temperature_time;
    uint16_t temp_sensor_1_temp;
    uint16_t temp_sensor_2_temp;
    uint8_t  reserved2[356];
} UserNVMeSMARTData;

typedef struct {
    uint16_t  vid;
    uint16_t  ssvid;
    char      sn[20];
    char      mn[40];
    char      fr[8];
    uint8_t   reserved[4020];
} UserNVMeIdentifyController;

typedef struct UserIONVMeSMARTInterface {
    IUNKNOWN_C_GUTS;
    IOReturn (*SMARTReadData)(void *interface, UserNVMeSMARTData *data);
    IOReturn (*GetIdentifyData)(void *interface, UserNVMeIdentifyController *data, unsigned int nsid);
    IOReturn (*GetLogPage)(void *interface, void *buffer, uint8_t logPage, uint32_t nsid);
} UserIONVMeSMARTInterface;

typedef struct {
    io_object_t               device_service;
    IOCFPlugInInterface      **plugin;
    UserIONVMeSMARTInterface **interface;
    uint8_t                   ioretries;
    uint32_t                  nsid;
    bool                      initialized;
    char                      bsd_path_cache[MAXPATHLEN];
} nvme_macos_state_t;

#define MAX_NVME_CACHE_SIZE 8
static nvme_macos_state_t *nvme_state_cache[MAX_NVME_CACHE_SIZE] = {NULL};
static int nvme_cache_next_slot = 0;

#define NVME_LOG_ERROR   0
#define NVME_LOG_WARNING 1
#define NVME_LOG_INFO    2
#define NVME_LOG_DEBUG   3
static int nvme_log_level = NVME_LOG_WARNING;

static void nvme_log(int level, const char *fmt, ...) {
    if (level > nvme_log_level) return;
    va_list args;
    va_start(args, fmt);
    const char *prefix;
    FILE *output = stderr;
    switch (level) {
        case NVME_LOG_ERROR:   prefix = "pal_macos_nvme ERROR: "; break;
        case NVME_LOG_WARNING: prefix = "pal_macos_nvme Warning: "; break;
        case NVME_LOG_INFO:    prefix = "pal_macos_nvme Info: "; break;
        case NVME_LOG_DEBUG:   prefix = "pal_macos_nvme Debug: "; break;
        default:               prefix = "pal_macos_nvme: "; break;
    }
    fprintf(output, "%s", prefix);
    vfprintf(output, fmt, args);
    fprintf(output, "\n");
    va_end(args);
}

static bool is_apple_silicon(void) {
    char cpu_brand[256];
    size_t size = sizeof(cpu_brand);
    if (sysctlbyname("machdep.cpu.brand_string", cpu_brand, &size, NULL, 0) == 0) {
        return (strstr(cpu_brand, "Apple") != NULL);
    }
    return false;
}

static int get_macos_version_major(void) {
    char osversion[256];
    size_t size = sizeof(osversion);
    if (sysctlbyname("kern.osproductversion", osversion, &size, NULL, 0) == 0) {
        int major = 0;
        sscanf(osversion, "%d", &major);
        return major;
    }
    return 11;
}

static bool is_nvme_smart_capable_prop(io_object_t device_service) {
    if (device_service == IO_OBJECT_NULL) return false;
    bool capable = false;
    CFTypeRef smartCapableProp = IORegistryEntryCreateCFProperty(device_service, CFSTR(kIOPropertyNVMeSMARTCapableKey), kCFAllocatorDefault, 0);
    if (smartCapableProp) {
        if (CFGetTypeID(smartCapableProp) == CFBooleanGetTypeID()) {
            capable = CFBooleanGetValue((CFBooleanRef)smartCapableProp);
        } else {
            capable = true;
        }
        CFRelease(smartCapableProp);
    }
    if (!capable && is_apple_silicon() && get_macos_version_major() >= 11) {
        io_iterator_t iter;
        if (IORegistryEntryGetChildIterator(device_service, kIOServicePlane, &iter) == KERN_SUCCESS) {
            io_registry_entry_t child;
            while ((child = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
                if (IOObjectConformsTo(child, "IONVMeBlockStorageDevice")) {
                    capable = true;
                    IOObjectRelease(child);
                    break;
                }
                IOObjectRelease(child);
            }
            IOObjectRelease(iter);
        }
    }
    return capable;
}

static io_object_t find_nvme_service_for_bsd(const char *bsd_name) {
    io_object_t media_service = IO_OBJECT_NULL;
    io_object_t controller_service = IO_OBJECT_NULL;

    CFMutableDictionaryRef matching_dict = IOBSDNameMatching(kIOMasterPortDefault, 0, bsd_name);
    if (!matching_dict) {
        nvme_log(NVME_LOG_ERROR, "Failed to create matching dictionary for BSD name %s", bsd_name);
        return IO_OBJECT_NULL;
    }

    media_service = IOServiceGetMatchingService(kIOMasterPortDefault, matching_dict);
    if (media_service == IO_OBJECT_NULL) {
        nvme_log(NVME_LOG_DEBUG, "No IOMedia service found for BSD name %s", bsd_name);
        return IO_OBJECT_NULL;
    }

    io_iterator_t iter;
    kern_return_t kr = IORegistryEntryGetParentIterator(media_service, kIOServicePlane, &iter);
    if (kr != KERN_SUCCESS) {
        nvme_log(NVME_LOG_WARNING, "Could not get parent iterator for %s", bsd_name);
        IOObjectRelease(media_service);
        return IO_OBJECT_NULL;
    }

    io_registry_entry_t parent;
    while ((parent = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(parent, kIONVMeControllerClassName)) {
            if (is_nvme_smart_capable_prop(parent)) {
                 controller_service = parent;
                 break;
            }
        }
        io_registry_entry_t grand_parent;
        if (IORegistryEntryGetParentEntry(parent, kIOServicePlane, &grand_parent) == KERN_SUCCESS && grand_parent != IO_OBJECT_NULL) {
             if (IOObjectConformsTo(grand_parent, kIONVMeControllerClassName) && is_nvme_smart_capable_prop(grand_parent)) {
                 controller_service = grand_parent;
                 IOObjectRelease(parent);
                 break;
             }
             IOObjectRelease(grand_parent);
        }
        IOObjectRelease(parent);
    }
    IOObjectRelease(iter);
    IOObjectRelease(media_service);

    if (controller_service == IO_OBJECT_NULL) {
         nvme_log(NVME_LOG_DEBUG, "No NVMe controller with SMART capability found for %s", bsd_name);
    }
    return controller_service;
}

static nvme_macos_state_t *nvme_state_cache_find(const char *bsd_path) {
    for (int i = 0; i < MAX_NVME_CACHE_SIZE; i++) {
        if (nvme_state_cache[i] && nvme_state_cache[i]->initialized &&
            strcmp(nvme_state_cache[i]->bsd_path_cache, bsd_path) == 0) {
            nvme_log(NVME_LOG_DEBUG, "NVMe state found in cache for %s", bsd_path);
            return nvme_state_cache[i];
        }
    }
    return NULL;
}

static void nvme_macos_cleanup(nvme_macos_state_t *state);

static void nvme_state_cache_add(nvme_macos_state_t *state) {
    if (!state || !state->initialized) return;

    if (nvme_state_cache[nvme_cache_next_slot]) {
        nvme_log(NVME_LOG_DEBUG, "Cache slot %d occupied, cleaning up old state for %s",
                 nvme_cache_next_slot, nvme_state_cache[nvme_cache_next_slot]->bsd_path_cache);
        nvme_macos_cleanup(nvme_state_cache[nvme_cache_next_slot]);
        nvme_state_cache[nvme_cache_next_slot] = NULL;
    }
    nvme_state_cache[nvme_cache_next_slot] = state;
    nvme_log(NVME_LOG_DEBUG, "NVMe state for %s added to cache slot %d", state->bsd_path_cache, nvme_cache_next_slot);
    nvme_cache_next_slot = (nvme_cache_next_slot + 1) % MAX_NVME_CACHE_SIZE;
}

static void nvme_macos_cleanup(nvme_macos_state_t *state) {
    if (!state) return;
    nvme_log(NVME_LOG_DEBUG, "Cleaning up NVMe state for %s", state->bsd_path_cache);

    for (int i = 0; i < MAX_NVME_CACHE_SIZE; i++) {
        if (nvme_state_cache[i] == state) {
            nvme_state_cache[i] = NULL;
            nvme_log(NVME_LOG_DEBUG, "Removed NVMe state for %s from cache slot %d", state->bsd_path_cache, i);
            break;
        }
    }

    if (state->interface) {
        (*state->interface)->Release(state->interface);
        state->interface = NULL;
    }
    if (state->plugin) {
        IODestroyPlugInInterface(state->plugin);
        state->plugin = NULL;
    }
    if (state->device_service) {
        IOObjectRelease(state->device_service);
        state->device_service = IO_OBJECT_NULL;
    }
    state->initialized = false;
    free(state);
}

static int nvme_macos_init(const char *device_path_full, nvme_macos_state_t **state_out) {
    if (!device_path_full || !state_out) {
        nvme_log(NVME_LOG_ERROR, "Invalid parameters for nvme_macos_init");
        return EINVAL;
    }
    const char *bsd_name_only = strrchr(device_path_full, '/');
    if (bsd_name_only) bsd_name_only++; else bsd_name_only = device_path_full;

    nvme_macos_state_t *cached_state = nvme_state_cache_find(device_path_full);
    if (cached_state) {
        *state_out = cached_state;
        return 0;
    }

    io_object_t nvme_ctrl_service = find_nvme_service_for_bsd(bsd_name_only);
    if (nvme_ctrl_service == IO_OBJECT_NULL) {
        nvme_log(NVME_LOG_DEBUG, "No NVMe controller service found for %s", device_path_full);
        return ENODEV;
    }

    nvme_macos_state_t *state = (nvme_macos_state_t *)calloc(1, sizeof(nvme_macos_state_t));
    if (!state) {
        IOObjectRelease(nvme_ctrl_service);
        nvme_log(NVME_LOG_ERROR, "Failed to allocate memory for NVMe state");
        return ENOMEM;
    }

    state->device_service = nvme_ctrl_service;
    state->ioretries = 3;
    state->nsid = 0xFFFFFFFF;
    strncpy(state->bsd_path_cache, device_path_full, MAXPATHLEN -1);
    state->bsd_path_cache[MAXPATHLEN-1] = '\0';

    SInt32 score = 0;
    IOReturn kr = IOCreatePlugInInterfaceForService(
        state->device_service,
        kIONVMeSMARTUserClientTypeID,
        kIOCFPlugInInterfaceID,
        &state->plugin,
        &score
    );

    if (kr != kIOReturnSuccess || !state->plugin) {
        nvme_log(NVME_LOG_ERROR, "Failed to create plugin interface for %s (kr=0x%x)", device_path_full, kr);
        nvme_macos_cleanup(state);
        return EIO;
    }

    HRESULT hr = (*state->plugin)->QueryInterface(
        state->plugin,
        CFUUIDGetUUIDBytes(kIONVMeSMARTInterfaceID),
        (void **)&state->interface
    );

    if (FAILED(hr) || !state->interface) {
        nvme_log(NVME_LOG_ERROR, "Failed to query NVMe SMART interface for %s (hr=0x%lx)", device_path_full, (unsigned long)hr);
        nvme_macos_cleanup(state);
        return ENOTSUP;
    }

    state->initialized = true;
    *state_out = state;
    nvme_state_cache_add(state);
    nvme_log(NVME_LOG_INFO, "NVMe interface initialized successfully for %s", device_path_full);
    return 0;
}

static int nvme_macos_do_smart_read(nvme_macos_state_t *state, UserNVMeSMARTData *data_out) {
    if (!state || !state->initialized || !state->interface || !data_out) {
        nvme_log(NVME_LOG_ERROR, "Invalid state for NVMe SMART read");
        return EINVAL;
    }
    memset(data_out, 0, sizeof(UserNVMeSMARTData));
    IOReturn kr = kIOReturnError;
    for (uint8_t i = 0; i < state->ioretries; i++) {
        kr = (*state->interface)->SMARTReadData(state->interface, data_out);
        if (kr == kIOReturnSuccess) break;
        nvme_log(NVME_LOG_WARNING, "SMARTReadData attempt %d failed for %s (kr=0x%x)", i + 1, state->bsd_path_cache, kr);
        if (i < state->ioretries -1) usleep(10000);
    }
    if (kr != kIOReturnSuccess) {
        nvme_log(NVME_LOG_ERROR, "All SMARTReadData attempts failed for %s (last kr=0x%x)", state->bsd_path_cache, kr);
        return EIO;
    }
    nvme_log(NVME_LOG_DEBUG, "SMARTReadData successful for %s", state->bsd_path_cache);
    return 0;
}

static int nvme_macos_do_identify_controller(nvme_macos_state_t *state, UserNVMeIdentifyController *data_out) {
     if (!state || !state->initialized || !state->interface || !data_out) {
        nvme_log(NVME_LOG_ERROR, "Invalid state for NVMe IdentifyController");
        return EINVAL;
    }
    if (!state->interface->GetIdentifyData) {
        nvme_log(NVME_LOG_WARNING, "GetIdentifyData not available in interface for %s", state->bsd_path_cache);
        return ENOTSUP;
    }
    memset(data_out, 0, sizeof(UserNVMeIdentifyController));
    IOReturn kr = kIOReturnError;
     for (uint8_t i = 0; i < state->ioretries; i++) {
        kr = (*state->interface)->GetIdentifyData(state->interface, data_out, state->nsid);
        if (kr == kIOReturnSuccess) break;
        nvme_log(NVME_LOG_WARNING, "GetIdentifyData attempt %d failed for %s (kr=0x%x)", i + 1, state->bsd_path_cache, kr);
        if (i < state->ioretries -1) usleep(10000);
    }
    if (kr != kIOReturnSuccess) {
        nvme_log(NVME_LOG_ERROR, "All GetIdentifyData attempts failed for %s (last kr=0x%x)", state->bsd_path_cache, kr);
        return EIO;
    }
    nvme_log(NVME_LOG_DEBUG, "GetIdentifyData successful for %s", state->bsd_path_cache);
    return 0;
}

static io_service_t get_iomedia_object(const char *bsd_name) {
    io_iterator_t media_iterator = MACH_PORT_NULL;
    io_service_t media_service = MACH_PORT_NULL;
    io_service_t result_service = MACH_PORT_NULL;

    CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOMediaClass);
    if (!matching_dict) {
        fprintf(stderr, "pal_macos: Could not create matching dictionary for IOMedia.\n");
        return MACH_PORT_NULL;
    }
    CFStringRef bsd_cfstring = CFStringCreateWithCString(kCFAllocatorDefault, bsd_name, kCFStringEncodingUTF8);
    if (!bsd_cfstring) {
         fprintf(stderr, "pal_macos: Could not create CFString for bsd_name.\n");
         CFRelease(matching_dict);
         return MACH_PORT_NULL;
    }
    CFDictionarySetValue(matching_dict, CFSTR(kIOMediaBSDNameKey), bsd_cfstring);
    CFRelease(bsd_cfstring);

    kern_return_t kr = IOServiceGetMatchingServices(kIOMasterPortDefault, matching_dict, &media_iterator);
    if (kr != KERN_SUCCESS || media_iterator == MACH_PORT_NULL) {
        fprintf(stderr, "pal_macos: No IOMedia objects found for %s. kr=0x%x\n", bsd_name, kr);
        if (media_iterator != MACH_PORT_NULL) IOObjectRelease(media_iterator);
        return MACH_PORT_NULL;
    }

    while ((media_service = IOIteratorNext(media_iterator)) != MACH_PORT_NULL) {
        result_service = media_service;
        break;
    }
    IOObjectRelease(media_iterator);
    return result_service;
}

static char* get_iokit_property_as_cstring(io_service_t service, CFStringRef propertyName) {
    CFTypeRef cf_property = IORegistryEntryCreateCFProperty(service, propertyName, kCFAllocatorDefault, kNilOptions);
    if (!cf_property) return NULL;

    char* c_string = NULL;
    if (CFGetTypeID(cf_property) == CFStringGetTypeID()) {
        CFStringRef cf_string_ref = (CFStringRef)cf_property;
        CFIndex length = CFStringGetLength(cf_string_ref);
        CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
        c_string = (char *)malloc(maxSize);
        if (c_string) {
            if (!CFStringGetCString(cf_string_ref, c_string, maxSize, kCFStringEncodingUTF8)) {
                free(c_string);
                c_string = NULL;
            }
        }
    }
    CFRelease(cf_property);
    return c_string;
}

static io_service_t find_ancestor_conforming_to(io_service_t start_service, const char *class_name) {
    io_iterator_t ancestor_iter;
    io_service_t found_service = MACH_PORT_NULL;
    kern_return_t kr = IORegistryEntryGetAncestorIterator(start_service, kIOServicePlane, &ancestor_iter);
    if (kr == KERN_SUCCESS && ancestor_iter != MACH_PORT_NULL) {
        io_registry_entry_t ancestor;
        while((ancestor = IOIteratorNext(ancestor_iter))) {
            if(IOObjectConformsTo(ancestor, class_name)) {
                found_service = ancestor;
                break;
            }
            IOObjectRelease(ancestor);
        }
        IOObjectRelease(ancestor_iter);
    }
    return found_service;
}

static kern_return_t get_ata_smart_interface(io_service_t disk_service_param, IOATASMARTInterface ***smart_interface_out) {
    IOCFPlugInInterface **plug_in_interface_val = NULL;
    SInt32 score_val = 0;
    kern_return_t kr_val;
    
    io_service_t smart_user_client_service = IO_OBJECT_NULL;
    
    kr_val = IOCreatePlugInInterfaceForService(disk_service_param, kIOATASMARTUserClientType, kIOCFPlugInInterfaceID, &plug_in_interface_val, &score_val);
    
    if (kr_val != KERN_SUCCESS || !plug_in_interface_val) {
        fprintf(stderr, "pal_macos_ata: Unable to create plugin for ATA SMART (service: %u). kr=0x%x\n", disk_service_param, kr_val);
        if (smart_user_client_service != IO_OBJECT_NULL && smart_user_client_service != disk_service_param) IOObjectRelease(smart_user_client_service);
        return kr_val;
    }
    if (smart_user_client_service != IO_OBJECT_NULL && smart_user_client_service != disk_service_param) IOObjectRelease(smart_user_client_service);

    kr_val = (*plug_in_interface_val)->QueryInterface(plug_in_interface_val, CFUUIDGetUUIDBytes(kIOATASMARTInterfaceID), (LPVOID)smart_interface_out);
    IODestroyPlugInInterface(plug_in_interface_val);

    if (kr_val != KERN_SUCCESS || !(*smart_interface_out)) {
        fprintf(stderr, "pal_macos_ata: QueryInterface for ATA SMART failed. kr=0x%x\n", kr_val);
        return kr_val;
    }
    return KERN_SUCCESS;
}

int pal_get_smart_data(const char *device_path, struct smart_data *out) {
    if (!device_path || !out) {
        fprintf(stderr, "pal_get_smart_data (macOS): Invalid parameters.\n");
        return 1;
    }
    memset(out, 0, sizeof(struct smart_data));

    const char *bsd_name_ptr = strrchr(device_path, '/');
    if (bsd_name_ptr) bsd_name_ptr++; else bsd_name_ptr = device_path;

    int result = 1;

    nvme_macos_state_t *nvme_state = NULL;
    if (nvme_macos_init(device_path, &nvme_state) == 0 && nvme_state && nvme_state->initialized) {
        out->is_nvme = 1;
        UserNVMeSMARTData nvme_raw_data;
        if (nvme_macos_do_smart_read(nvme_state, &nvme_raw_data) == 0) {
            result = 0;
            out->nvme.critical_warning = nvme_raw_data.critical_warning;
            uint16_t temp_kelvin = nvme_raw_data.temperature;
            out->nvme.temperature[0] = temp_kelvin & 0xFF;
            out->nvme.temperature[1] = (temp_kelvin >> 8) & 0xFF;

            out->nvme.avail_spare = nvme_raw_data.available_spare;
            out->nvme.spare_thresh = nvme_raw_data.available_spare_threshold;
            out->nvme.percent_used = nvme_raw_data.percentage_used;
            memcpy(out->nvme.data_units_read, nvme_raw_data.data_units_read, sizeof(out->nvme.data_units_read));
            memcpy(out->nvme.data_units_written, nvme_raw_data.data_units_written, sizeof(out->nvme.data_units_written));
            memcpy(out->nvme.host_read_commands, nvme_raw_data.host_read_commands, sizeof(out->nvme.host_read_commands));
            memcpy(out->nvme.host_write_commands, nvme_raw_data.host_write_commands, sizeof(out->nvme.host_write_commands));
            memcpy(out->nvme.controller_busy_time, nvme_raw_data.controller_busy_time, sizeof(out->nvme.controller_busy_time));
            memcpy(out->nvme.power_cycles, nvme_raw_data.power_cycles, sizeof(out->nvme.power_cycles));
            memcpy(out->nvme.power_on_hours, nvme_raw_data.power_on_hours, sizeof(out->nvme.power_on_hours));
            memcpy(out->nvme.unsafe_shutdowns, nvme_raw_data.unsafe_shutdowns, sizeof(out->nvme.unsafe_shutdowns));
            memcpy(out->nvme.media_errors, nvme_raw_data.media_errors, sizeof(out->nvme.media_errors));
            memcpy(out->nvme.num_err_log_entries, nvme_raw_data.num_error_log_entries, sizeof(out->nvme.num_error_log_entries));
        } else {
            nvme_log(NVME_LOG_WARNING, "NVMe SMART read failed for %s after successful init.", device_path);
        }
        if (result == 0) return 0;
    } else {
         nvme_log(NVME_LOG_DEBUG, "NVMe init failed or not an NVMe device (%s), trying ATA path.", device_path);
    }

    io_service_t media_service = get_iomedia_object(bsd_name_ptr);
    if (media_service == MACH_PORT_NULL) {
        fprintf(stderr, "pal_get_smart_data (macOS): Could not get IOMedia for %s for ATA path\n", bsd_name_ptr);
        return 1;
    }
    
    io_service_t ata_service_candidate = find_ancestor_conforming_to(media_service, kIOBlockStorageDeviceClass);
    if (ata_service_candidate == MACH_PORT_NULL) {
        nvme_log(NVME_LOG_DEBUG,"No IOBlockStorageDevice ancestor for %s, using IOMedia itself for ATA SMART.", bsd_name_ptr);
        ata_service_candidate = media_service;
        IOObjectRetain(ata_service_candidate);
    }

    IOATASMARTInterface **smart_interface_ptr = NULL;
    kern_return_t kr = get_ata_smart_interface(ata_service_candidate, &smart_interface_ptr);

    if (kr == KERN_SUCCESS && smart_interface_ptr != NULL && (*smart_interface_ptr) != NULL) {
        ATASMARTData ata_s_data;
        ATASMARTThresholds ata_s_thresholds;
        memset(&ata_s_data, 0, sizeof(ATASMARTData));
        memset(&ata_s_thresholds, 0, sizeof(ATASMARTThresholds));

        kr = (**smart_interface_ptr)->SMARTReadData(*smart_interface_ptr, &ata_s_data);
        if (kr == KERN_SUCCESS) {
            result = 0;
            out->is_nvme = 0;
            out->attr_count = 0;
            for (int i = 0; i < kATASMARTNumAttributes && out->attr_count < MAX_SMART_ATTRIBUTES; ++i) {
                if (ata_s_data.attributes[i].attributeId == 0 || ata_s_data.attributes[i].attributeId == 0xFF) continue;
                out->attrs[out->attr_count].id = ata_s_data.attributes[i].attributeId;
                out->attrs[out->attr_count].flags = 0;
                if (ata_s_data.attributes[i].flags & kATASMARTAttributeFlagPrefailure) out->attrs[out->attr_count].flags |= 0x01;
                if (ata_s_data.attributes[i].flags & kATASMARTAttributeFlagOnline) out->attrs[out->attr_count].flags |= 0x02;
                out->attrs[out->attr_count].value = ata_s_data.attributes[i].currentValue;
                out->attrs[out->attr_count].worst = ata_s_data.attributes[i].worstValue;
                memcpy(out->attrs[out->attr_count].raw, ata_s_data.attributes[i].rawValue, 6);
                out->attrs[out->attr_count].threshold = 0;
                out->attr_count++;
            }

            kr = (**smart_interface_ptr)->SMARTReadThresholds(*smart_interface_ptr, &ata_s_thresholds);
            if (kr == KERN_SUCCESS) {
                for (int i = 0; i < out->attr_count; ++i) {
                    for (int j = 0; j < kATASMARTNumAttributes; ++j) {
                        if (ata_s_thresholds.attributes[j].attributeId == out->attrs[i].id) {
                            out->attrs[i].threshold = ata_s_thresholds.attributes[j].thresholdValue;
                            break;
                        }
                    }
                }
            } else {
                fprintf(stderr, "pal_macos_ata: SMARTReadThresholds failed (kr=0x%x)\n", kr);
            }
        } else {
            fprintf(stderr, "pal_macos_ata: SMARTReadData failed (kr=0x%x)\n", kr);
            result = 1;
        }
        (**smart_interface_ptr)->Release(*smart_interface_ptr);
    } else {
        fprintf(stderr, "pal_macos_ata: Could not get ATA SMART interface for %s (kr=0x%x)\n", bsd_name_ptr, kr);
        result = 1;
    }
    IOObjectRelease(ata_service_candidate);
    IOObjectRelease(media_service);
    
    return result;
}

int pal_list_drives() {
    printf("Available physical drives (macOS):\n");
    printf("%-15s | %-30s | %-25s | %-10s | %-10s | %s\n", "Device Path", "Model", "Serial", "Size (GB)", "Type", "SMART Cap");
    printf("---------------------------------------------------------------------------------------------------------------------\n");

    io_iterator_t drive_iterator;
    mach_port_t master_port;
    kern_return_t kern_result = IOMasterPort(MACH_PORT_NULL, &master_port);
    if (kern_result != KERN_SUCCESS) {
        master_port = kIOMasterPortDefault;
    }

    CFMutableDictionaryRef matching_dict = IOServiceMatching(kIOMediaClass);
    if (!matching_dict) return 1;
    CFDictionaryAddValue(matching_dict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

    kern_result = IOServiceGetMatchingServices(master_port, matching_dict, &drive_iterator);
    if (kern_result != KERN_SUCCESS) return 1;

    io_service_t drive_service;
    int found_any = 0;
    while ((drive_service = IOIteratorNext(drive_iterator))) {
        char bsd_name_str[MAXPATHLEN] = "N/A";
        char full_device_path[MAXPATHLEN] = "N/A";

        CFStringRef bsd_cf_name = (CFStringRef)IORegistryEntryCreateCFProperty(drive_service, CFSTR(kIOMediaBSDNameKey), kCFAllocatorDefault, kNilOptions);
        if (bsd_cf_name) {
            CFStringGetCString(bsd_cf_name, bsd_name_str, sizeof(bsd_name_str), kCFStringEncodingUTF8);
            snprintf(full_device_path, sizeof(full_device_path), "/dev/%s", bsd_name_str);
            CFRelease(bsd_cf_name);
        } else { 
            io_registry_entry_t parent_service;
            if (IORegistryEntryGetParentEntry(drive_service, kIOServicePlane, &parent_service) == KERN_SUCCESS && parent_service != IO_OBJECT_NULL) {
                 bsd_cf_name = (CFStringRef)IORegistryEntryCreateCFProperty(parent_service, CFSTR(kIOMediaBSDNameKey), kCFAllocatorDefault, kNilOptions);
                 if (bsd_cf_name) {
                    CFStringGetCString(bsd_cf_name, bsd_name_str, sizeof(bsd_name_str), kCFStringEncodingUTF8);
                    snprintf(full_device_path, sizeof(full_device_path), "/dev/%s", bsd_name_str);
                    CFRelease(bsd_cf_name);
                 }
                 IOObjectRelease(parent_service);
            }
        }
        
        bool is_whole_disk = false;
        CFBooleanRef whole_prop = (CFBooleanRef)IORegistryEntryCreateCFProperty(drive_service, CFSTR(kIOMediaWholeKey), kCFAllocatorDefault, 0);
        if (whole_prop && CFGetTypeID(whole_prop) == CFBooleanGetTypeID()) {
            is_whole_disk = CFBooleanGetValue(whole_prop);
        }
        if (whole_prop) CFRelease(whole_prop);

        if (!is_whole_disk || strncmp(bsd_name_str, "disk", 4) != 0 || strchr(bsd_name_str, 's') != NULL) {
            IOObjectRelease(drive_service);
            continue;
        }
        
        BasicDriveInfo info;
        bool info_ok = pal_get_basic_drive_info(full_device_path, &info);
        if (info_ok) {
            found_any = 1;
            double size_gb = (info.size_bytes > 0) ? (double)info.size_bytes / (1024.0 * 1024.0 * 1024.0) : 0.0;
            struct smart_data temp_smart_data;
            bool smart_functional = (pal_get_smart_data(full_device_path, &temp_smart_data) == 0);

            printf("%-15s | %-30s | %-25s | %-10.2f | %-10s | %s\n", 
                   full_device_path, info.model, info.serial, size_gb, info.type, smart_functional ? "Yes" : "No");
        }
        IOObjectRelease(drive_service);
    }
    IOObjectRelease(drive_iterator);
    if (!found_any) printf("No physical drives found or IOKit query failed.\n");
    return 0;
}

int64_t pal_get_device_size(const char *device_path) {
    const char *bsd_name_ptr = strrchr(device_path, '/');
    if (bsd_name_ptr) bsd_name_ptr++;
    else bsd_name_ptr = device_path;

    io_service_t media_service = get_iomedia_object(bsd_name_ptr);
    if (media_service == MACH_PORT_NULL) return -1;

    int64_t device_size = -1;
    CFNumberRef size_cfnumber = (CFNumberRef)IORegistryEntryCreateCFProperty(media_service, CFSTR(kIOMediaSizeKey), kCFAllocatorDefault, kNilOptions);
    if (size_cfnumber && CFGetTypeID(size_cfnumber) == CFNumberGetTypeID()) {
        uint64_t size_val_u64;
        if (CFNumberGetValue(size_cfnumber, kCFNumberSInt64Type, &size_val_u64)) {
            device_size = (int64_t)size_val_u64;
        }
        CFRelease(size_cfnumber);
    }
    IOObjectRelease(media_service);
    return device_size;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    if (!device_path || !info) return false;
    memset(info, 0, sizeof(BasicDriveInfo));
    strncpy(info->model, "Unknown", sizeof(info->model) - 1);
    strncpy(info->serial, "Unknown", sizeof(info->serial) - 1);
    strncpy(info->type, "Unknown", sizeof(info->type) - 1);
    strncpy(info->bus_type, "Unknown", sizeof(info->bus_type) - 1);
    info->smart_capable = false;

    const char *bsd_name_ptr = strrchr(device_path, '/');
    if (bsd_name_ptr) bsd_name_ptr++;
    else bsd_name_ptr = device_path;

    io_service_t media_service = get_iomedia_object(bsd_name_ptr);
    if (media_service == MACH_PORT_NULL) return false;

    CFBooleanRef ssd_prop = (CFBooleanRef)IORegistryEntryCreateCFProperty(media_service, CFSTR(kIOMediaSolidStateKey), kCFAllocatorDefault, kNilOptions);
    if (ssd_prop && CFGetTypeID(ssd_prop) == CFBooleanGetTypeID()) {
        if (CFBooleanGetValue(ssd_prop)) strncpy(info->type, "SSD", sizeof(info->type) - 1);
        else strncpy(info->type, "HDD", sizeof(info->type) - 1);
        CFRelease(ssd_prop);
    }

    info->size_bytes = pal_get_device_size(device_path);

    io_service_t device_to_query_props = find_ancestor_conforming_to(media_service, kIOBlockStorageDeviceClass);
    if (device_to_query_props == IO_OBJECT_NULL) {
        device_to_query_props = find_ancestor_conforming_to(media_service, kIONVMeControllerClassName);
    }
    if (device_to_query_props == IO_OBJECT_NULL) {
        device_to_query_props = media_service;
        IOObjectRetain(media_service);
    }

    char* model_str = get_iokit_property_as_cstring(device_to_query_props, CFSTR(kIOPropertyProductNameKey));
    if (model_str) { strncpy(info->model, model_str, sizeof(info->model) - 1); free(model_str); }

    char* serial_str = get_iokit_property_as_cstring(device_to_query_props, CFSTR(kIOPropertyProductSerialNumberKey));
    if (serial_str) { strncpy(info->serial, serial_str, sizeof(info->serial) - 1); free(serial_str); }

    CFTypeRef dev_char_prop = IORegistryEntryCreateCFProperty(device_to_query_props, CFSTR(kIOPropertyDeviceCharacteristicsKey), kCFAllocatorDefault, kNilOptions);
    if (dev_char_prop && CFGetTypeID(dev_char_prop) == CFDictionaryGetTypeID()) {
        CFStringRef transport_cfstr = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)dev_char_prop, CFSTR(kIOPropertyTransportTypeKey));
        if (transport_cfstr && CFGetTypeID(transport_cfstr) == CFStringGetTypeID()) {
            char temp_bus_type[64];
            if (CFStringGetCString(transport_cfstr, temp_bus_type, sizeof(temp_bus_type), kCFStringEncodingUTF8)) {
                strncpy(info->bus_type, temp_bus_type, sizeof(info->bus_type) - 1);
                if (strcmp(temp_bus_type, "NVMe") == 0 && strcmp(info->type, "SSD") == 0) {
                    strncpy(info->type, "NVMe SSD", sizeof(info->type) -1 );
                }
            }
        }
    }
    if (dev_char_prop) CFRelease(dev_char_prop);

    if (strcmp(info->bus_type, "Unknown") == 0) {
        if (IOObjectConformsTo(device_to_query_props, kIONVMeControllerClassName)) {
            strncpy(info->bus_type, "NVMe", sizeof(info->bus_type) - 1);
            if (strcmp(info->type, "SSD") == 0 || strcmp(info->type, "Unknown") == 0) strncpy(info->type, "NVMe SSD", sizeof(info->type) -1 );
        } else if (IOObjectConformsTo(device_to_query_props, kIOATAStorageDeviceClassName) ||
                   IOObjectConformsTo(device_to_query_props, "IOATABlockStorageDevice")) { 
            strncpy(info->bus_type, "ATA/SATA", sizeof(info->bus_type) - 1);
        }
    }
    
    CFBooleanRef smart_cap_prop = (CFBooleanRef)IORegistryEntryCreateCFProperty(media_service, CFSTR(kIOPropertySMARTCapableKey), kCFAllocatorDefault, kNilOptions);
    if (smart_cap_prop && CFGetTypeID(smart_cap_prop) == CFBooleanGetTypeID()){
        info->smart_capable = CFBooleanGetValue(smart_cap_prop);
        CFRelease(smart_cap_prop);
    } else if (is_nvme_smart_capable_prop(device_to_query_props)) { 
        info->smart_capable = true;
    }

    if (device_to_query_props != media_service) IOObjectRelease(device_to_query_props);
    IOObjectRelease(media_service);
    return true;
}


#else 

int pal_list_drives() {
    printf("PAL macOS: Not available (not compiling for macOS).\n");
    return 1;
}

int64_t pal_get_device_size(const char *device_path) {
    (void)device_path;
    fprintf(stderr, "pal_get_device_size: macOS PAL not compiled.\n");
    return -1;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    (void)device_path;
    if (info) {
        memset(info, 0, sizeof(BasicDriveInfo));
        strncpy(info->model, "N/A macOS", sizeof(info->model) - 1);
        strncpy(info->serial, "N/A macOS", sizeof(info->serial) - 1);
        strncpy(info->type, "N/A macOS", sizeof(info->type) - 1);
        strncpy(info->bus_type, "N/A macOS", sizeof(info->bus_type) - 1);
    }
    fprintf(stderr, "pal_get_basic_drive_info: macOS PAL not compiled.\n");
    return false;
}

int pal_get_smart_data(const char *device_path, struct smart_data *out) {
    (void)device_path;
    if (out) {
        memset(out, 0, sizeof(struct smart_data));
    }
    fprintf(stderr, "pal_get_smart_data: macOS PAL not compiled, function unavailable on this OS.\n");
    return 1;
}

#endif
