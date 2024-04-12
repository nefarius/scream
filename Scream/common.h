/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    Common.h

Abstract:
    CAdapterCommon class declaration.
--*/

#ifndef _MSVAD_COMMON_H_
#define _MSVAD_COMMON_H_

#include <wsk.h>

//=============================================================================
// Defines
//=============================================================================
// {5134DDBB-EFCB-49C8-9814-3070D7741A5F}
DEFINE_GUID(IID_IAdapterCommon, 
0x5134ddbb, 0xefcb, 0x49c8, 0x98, 0x14, 0x30, 0x70, 0xd7, 0x74, 0x1a, 0x5f);

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
} ADAPTER_COMMON_SETTINGS, *PADAPTER_COMMON_SETTINGS;


//=============================================================================
// Interfaces
//=============================================================================

///////////////////////////////////////////////////////////////////////////////
// IAdapterCommon
//
DECLARE_INTERFACE_(IAdapterCommon, IUnknown) {
    STDMETHOD_(NTSTATUS,        Init)                (THIS_ IN PDEVICE_OBJECT DeviceObject) PURE;
    STDMETHOD_(PDEVICE_OBJECT,  GetDeviceObject)     (THIS) PURE;
    STDMETHOD_(VOID,            SetWaveServiceGroup) (THIS_ IN PSERVICEGROUP ServiceGroup) PURE;
    STDMETHOD_(PUNKNOWN *,      WavePortDriverDest)  (THIS) PURE;

    STDMETHOD_(BOOL,            bDevSpecificRead)    (THIS_) PURE;
    STDMETHOD_(VOID,            bDevSpecificWrite)   (THIS_ IN  BOOL bDevSpecific);

    STDMETHOD_(INT,             iDevSpecificRead)    (THIS_) PURE;
    STDMETHOD_(VOID,            iDevSpecificWrite)   (THIS_ IN INT iDevSpecific);

    STDMETHOD_(UINT,            uiDevSpecificRead)   (THIS_) PURE;
    STDMETHOD_(VOID,            uiDevSpecificWrite)  (THIS_ IN UINT uiDevSpecific);

    STDMETHOD_(BOOL,            MixerMuteRead)       (THIS_ IN ULONG Index) PURE;
    STDMETHOD_(VOID,            MixerMuteWrite)      (THIS_ IN ULONG Index, IN BOOL Value);
    STDMETHOD_(ULONG,           MixerMuxRead)        (THIS);
    STDMETHOD_(VOID,            MixerMuxWrite)       (THIS_ IN ULONG Index);
    STDMETHOD_(LONG,            MixerVolumeRead)     (THIS_ IN ULONG Index, IN LONG Channel) PURE;
    STDMETHOD_(VOID,            MixerVolumeWrite)    (THIS_ IN ULONG Index, IN LONG Channel, IN LONG Value) PURE;
    STDMETHOD_(VOID,            MixerReset)          (THIS) PURE;

    //
    // Additional helpers to support multiple devices/adapters
    // 
    STDMETHOD_(UINT32,                  GetDeviceIndex)     (THIS_) PURE;
    STDMETHOD_(ADAPTER_COMMON_SETTINGS, GetAdapterSettings) (THIS_) PURE;
};
typedef IAdapterCommon *PADAPTERCOMMON;

//=============================================================================
// Function Prototypes
//=============================================================================
NTSTATUS NewAdapterCommon( 
    OUT PUNKNOWN* Unknown,
    IN  REFCLSID,
    IN  PUNKNOWN  UnknownOuter OPTIONAL,
    IN  POOL_TYPE PoolType 
);

#endif  //_COMMON_H_
