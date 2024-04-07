/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    Common.h

Abstract:
    CAdapterCommon class declaration.
--*/

#ifndef _MSVAD_COMMON_H_
#define _MSVAD_COMMON_H_

//=============================================================================
// Defines
//=============================================================================
// {5134DDBB-EFCB-49C8-9814-3070D7741A5F}
DEFINE_GUID(IID_IAdapterCommon, 
0x5134ddbb, 0xefcb, 0x49c8, 0x98, 0x14, 0x30, 0x70, 0xd7, 0x74, 0x1a, 0x5f);


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
    STDMETHOD_(VOID,            SetDeviceIndex)     (THIS_ IN UINT32 DeviceIndex) PURE;
    STDMETHOD_(UINT32,          GetDeviceIndex)     (THIS_) PURE;
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
