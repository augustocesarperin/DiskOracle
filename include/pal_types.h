#ifndef PAL_TYPES_H
#define PAL_TYPES_H

#include <stdint.h> // For UCHAR (uint8_t) if not implicitly available

// Define UCHAR if it's not defined by Windows headers in this context
// For cross-platform compatibility, it's better to use uint8_t, uint16_t, etc.
// However, to match the existing Windows-specific code that will consume this,
// we might need to ensure UCHAR, USHORT, ULONG, ULONGLONG are defined or map them.
// For now, assuming UCHAR is uint8_t.
#ifndef _WINDEF_
// These are typically defined in Windows headers like windef.h
// If this file is included in a non-Windows context, these might be needed.
// However, pal_shared_nvme.h, which includes this, seems Windows-specific.
/*
typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned long ULONG;
typedef unsigned long long ULONGLONG;
*/
// Let windows.h define these types. If compilation fails elsewhere due to missing UCHAR etc.,
// it means windows.h was not included where it should have been for those specific files.
// The original conditional include for UCHAR etc. based on _WINDEF_ was a good approach.
// Forcing them here unconditionally if pal_types.h is included everywhere could also lead to redefinition warnings.
// Reverting to a more standard way of letting system headers provide these.
#endif


// Copied and renamed from NVME_HEALTH_INFO_LOG in pal_windows.c
// This structure is used by pal_shared_nvme.h for NVMe SMART data.
/*
typedef struct _nvme_smart_health_information_log_t {
    UCHAR CriticalWarning;         // Byte 0
    UCHAR Temperature[2];          // Byte 1-2 (Word 0) - Little Endian, Kelvin
    UCHAR AvailableSpare;          // Byte 3
    UCHAR AvailableSpareThreshold; // Byte 4
    UCHAR PercentageUsed;          // Byte 5
    UCHAR Reserved0[26];           // Byte 6-31
    uint8_t DataUnitsRead[16];     // Byte 32-47 - Number of 512-byte data units read by the host (in thousands)
    uint8_t DataUnitsWritten[16];  // Byte 48-63 - Number of 512-byte data units written by the host (in thousands)
    uint8_t HostReadCommands[16];  // Byte 64-79 - Number of read commands completed by the controller
    uint8_t HostWriteCommands[16]; // Byte 80-95 - Number of write commands completed by the controller
    uint8_t ControllerBusyTime[16];// Byte 96-111 - Total time in minutes the controller has been busy with I/O commands
    uint8_t PowerCycles[16];       // Byte 112-127 - Number of power cycles
    uint8_t PowerOnHours[16];      // Byte 128-143 - Number of power-on hours
    uint8_t UnsafeShutdowns[16];   // Byte 144-159 - Number of unsafe shutdowns
    uint8_t MediaAndDataIntegrityErrors[16]; // Byte 160-175 - Number of occurrences where the NVM subsystem or the host detected an unrecovered data integrity error
    uint8_t NumberOfErrorInformationLogEntries[16]; // Byte 176-191 - Number of Error Information log entries over the life of the controller
    // Bytes 192-203: Reserved
    // Bytes 204: Thermal Management Temperature 1 Transition Counter
    // Bytes 205: Thermal Management Temperature 2 Transition Counter
    // Bytes 206-207: Thermal Management Temperature 1 Total Time
    // Bytes 208-209: Thermal Management Temperature 2 Total Time
    UCHAR Reserved2[320];          // Byte 192-511 (NVMe spec says 320 bytes for Reserved here, original struct said Reserved2[320] which would be bytes 192-511)
                                   // The original struct's field names for DataUnitRead etc. used UCHAR[16], but these are 128-bit integers.
                                   // Using uint8_t[16] for these large integer fields to be explicit about byte arrays.
} nvme_smart_health_information_log_t;
*/

#endif // PAL_TYPES_H 