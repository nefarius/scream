#pragma once

#include <ws2def.h>

//
// Format string for registry key to load adapter settings from
// 
#define SETTING_REG_PATH_FMT \
    L"\\Registry\\Machine\\SOFTWARE\\" \
    L"Nefarius Software Solutions e.U.\\" \
    L"Scream Audio Streaming Driver\\" \
    L"Device\\%04d"

//
// Default settings for each adapter
// 

#define DEFAULTS_SRC_PORT   0 // any
#define DEFAULTS_DST_PORT   4010
#define DEFAULTS_SRC_IPV4   "0.0.0.0" // any
#define DEFAULTS_DST_IPV4   "239.255.77.77"

//
// Adapter settings
// 
typedef struct
{
    SOCKADDR_IN SourceAddress;
    SOCKADDR_IN DestinationAddress;
    BOOLEAN UseMulticast;
    //
    // 0 = false, otherwise it's value is the size in MiB of the IVSHMEM we want to use
    // 
    UINT8 UseIVSHMEM;
    DWORD TTL;
    DWORD SilenceThreshold;
} ADAPTER_COMMON_SETTINGS;

