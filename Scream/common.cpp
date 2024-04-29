/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    common.cpp

Abstract:
    Implementation of the AdapterCommon class. 
--*/

// ReSharper disable CppClangTidyReadabilityDeleteNullPointer
#pragma warning (disable : 4127)

#include "scream.h"
#include "common.h"
#include "hw.h"
#include "netsink.h"
#include "ivshmemsavedata.h"
#include "common.tmh"

#include <ntstrsafe.h>
#include <limits.h>

//-----------------------------------------------------------------------------
// Externals
//-----------------------------------------------------------------------------
// TODO: get rid of those!
__declspec(deprecated("Move to instance member variable"))
PSAVEWORKER_PARAM CNetSink::m_pWorkItem = NULL;
__declspec(deprecated("Move to instance member variable"))
PDEVICE_OBJECT    CNetSink::m_pDeviceObject = NULL;

// TODO: get rid of those!
__declspec(deprecated("Move to instance member variable"))
PIVSHMEM_SAVEWORKER_PARAM CIVSHMEMSaveData::m_pWorkItem = NULL;
__declspec(deprecated("Move to instance member variable"))
PDEVICE_OBJECT            CIVSHMEMSaveData::m_pDeviceObject = NULL;

//=============================================================================
// Classes
//=============================================================================

///////////////////////////////////////////////////////////////////////////////
// CAdapterCommon
//   
class CAdapterCommon : public IAdapterCommon, public IAdapterPowerManagement, public CUnknown {
private:
    PPORTWAVECYCLIC         m_pPortWave;    // Port interface
    PSERVICEGROUP           m_pServiceGroupWave;
    PDEVICE_OBJECT          m_pDeviceObject;
    UINT32                  m_SlotIndex;
    DEVICE_POWER_STATE      m_PowerState;
    PCVirtualAudioDevice    m_pHW;          // Virtual MSVAD HW object
    ADAPTER_COMMON_SETTINGS m_Settings;

    //
    // Holds information about occupied device slots
    // 
    static LONG    m_Slots[8]; // 256 usable bits
    static LONG    SetSlot(UINT32 SlotIndex);
    static BOOLEAN TestSlot(UINT32 SlotIndex);
    static LONG    ClearSlot(UINT32 SlotIndex);

    _IRQL_requires_max_(PASSIVE_LEVEL)
    void QueryAdapterRegistrySettings();

public:
    //=====================================================================
    // Default CUnknown
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CAdapterCommon);
    ~CAdapterCommon() override;

    //=====================================================================
    // Default IAdapterPowerManagement
    IMP_IAdapterPowerManagement;

    //=====================================================================
    // IAdapterCommon methods                                               
    STDMETHODIMP_(NTSTATUS)      Init(IN PDEVICE_OBJECT DeviceObject) override;
    STDMETHODIMP_(PDEVICE_OBJECT)GetDeviceObject(void) override;
    STDMETHODIMP_(PUNKNOWN *)    WavePortDriverDest(void) override;
    STDMETHODIMP_(void)          SetWaveServiceGroup(IN PSERVICEGROUP ServiceGroup) override;

    STDMETHODIMP_(BOOL)bDevSpecificRead() override;
    STDMETHODIMP_(void)bDevSpecificWrite(IN BOOL bDevSpecific) override;

    STDMETHODIMP_(INT) iDevSpecificRead() override;
    STDMETHODIMP_(void)iDevSpecificWrite(IN INT iDevSpecific) override;

    STDMETHODIMP_(UINT)uiDevSpecificRead() override;
    STDMETHODIMP_(void)uiDevSpecificWrite(IN UINT uiDevSpecific) override;

    STDMETHODIMP_(BOOL) MixerMuteRead(IN ULONG Index) override;
    STDMETHODIMP_(void) MixerMuteWrite(IN ULONG Index, IN BOOL Value) override;
    STDMETHODIMP_(ULONG)MixerMuxRead(void) override;
    STDMETHODIMP_(void) MixerMuxWrite(IN ULONG Index) override;
    STDMETHODIMP_(void) MixerReset(void) override;
    STDMETHODIMP_(LONG) MixerVolumeRead(IN ULONG Index, IN LONG Channel) override;
    STDMETHODIMP_(void) MixerVolumeWrite(IN ULONG Index, IN LONG Channel, IN LONG Value) override;

    STDMETHODIMP_(UINT32)                        GetDeviceIndex(void) override;
    STDMETHODIMP_(CONST ADAPTER_COMMON_SETTINGS*)GetAdapterSettings(void) CONST override;

    //=====================================================================
    // friends
    friend NTSTATUS NewAdapterCommon(OUT PADAPTERCOMMON * OutAdapterCommon, IN PRESOURCELIST ResourceList);
};

LONG CAdapterCommon::m_Slots[8] = { 0 };

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

#pragma code_seg("PAGE")
NTSTATUS NewAdapterCommon(
    OUT PUNKNOWN * Unknown,
    IN REFCLSID,
    IN PUNKNOWN  UnknownOuter OPTIONAL,
    IN POOL_TYPE PoolType
) {
    PAGED_CODE();

    ASSERT(Unknown);

    STD_CREATE_BODY_(CAdapterCommon, Unknown, UnknownOuter, PoolType, PADAPTERCOMMON);
}

CAdapterCommon::~CAdapterCommon(void) {
    PAGED_CODE();

    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        delete m_pHW;
    }

    if (g_UseIVSHMEM) {
        CIVSHMEMSaveData::DestroyWorkItems();
    }
    else {
        CNetSink::DestroyWorkItems();
    }

    if (m_pPortWave) {
        m_pPortWave->Release();
    }

    if (m_pServiceGroupWave) {
        m_pServiceGroupWave->Release();
    }

    ClearSlot(m_SlotIndex);

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(PDEVICE_OBJECT)CAdapterCommon::GetDeviceObject(void) {
    PAGED_CODE();

    return m_pDeviceObject;
}

NTSTATUS CAdapterCommon::Init(IN PDEVICE_OBJECT DeviceObject) {
    FuncEntry(TRACE_COMMON);

    PAGED_CODE();

    ASSERT(DeviceObject);

    NTSTATUS ntStatus = STATUS_SUCCESS;

    //
    // Get next free slot
    // 
    for (m_SlotIndex = 1; m_SlotIndex <= MAX_DEVICES; m_SlotIndex++) {
        if (!TestSlot(m_SlotIndex)) {
            SetSlot(m_SlotIndex);
            ntStatus = STATUS_SUCCESS;

            TraceVerbose(
                TRACE_COMMON,
                "Claimed device slot: %d",
                m_SlotIndex
            );

            break;
        }
    }

    //
    // We've reached the maximum allowed without any success
    // 
    if (m_SlotIndex > MAX_DEVICES) {
        ntStatus = STATUS_NO_MORE_ENTRIES;
    }

    if (!NT_SUCCESS(ntStatus)) {
        goto exit;
    }

    m_pDeviceObject = DeviceObject;
    m_PowerState = PowerDeviceD0;

    //
    // (Re-)load settings for this adapter instance
    // 
    QueryAdapterRegistrySettings();

    // Initialize HW.
    m_pHW = new(NonPagedPool, SCREAM_POOLTAG) CVirtualAudioDevice;
    if (!m_pHW) {
        TraceError(TRACE_COMMON, "Failed to allocate memory for CVirtualAudioDevice");
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }
    else {
        m_pHW->MixerReset();
    }

    if (g_UseIVSHMEM) {
        CIVSHMEMSaveData::SetDeviceObject(DeviceObject); //device object is needed by CIVSHMEMSaveData
    }
    else {
        CNetSink::SetDeviceObject(DeviceObject); //device object is needed by CSaveData
    }

exit:
    FuncExit(TRACE_COMMON, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
}

STDMETHODIMP_(void)CAdapterCommon::MixerReset(void) {
    FuncEntry(TRACE_COMMON);

    PAGED_CODE();

    if (m_pHW) {
        m_pHW->MixerReset();
    }

    FuncExitNoReturn(TRACE_COMMON);
}

LONG CAdapterCommon::SetSlot(UINT32 SlotIndex) {
    const UINT32 bits = sizeof(m_Slots);

    return InterlockedOr(&m_Slots[SlotIndex / bits], 1 << (SlotIndex % bits));
}
BOOLEAN CAdapterCommon::TestSlot(UINT32 SlotIndex) {
    const UINT32 bits = sizeof(m_Slots);

    return (BOOLEAN)(m_Slots[SlotIndex / bits] & (1 << (SlotIndex % bits)));
}

LONG CAdapterCommon::ClearSlot(UINT32 SlotIndex) {
    const UINT32 bits = sizeof(m_Slots);

    return InterlockedAnd(&m_Slots[SlotIndex / bits], ~(1 << (SlotIndex % bits)));
}

//
// Query the registry settings for this adapter instance, if any
//
_IRQL_requires_max_(PASSIVE_LEVEL)
void CAdapterCommon::QueryAdapterRegistrySettings() {
    FuncEntry(TRACE_COMMON);

    NTSTATUS ntStatus;

    DECLARE_UNICODE_STRING_SIZE(keyPath, (sizeof(SETTING_REG_PATH_FMT) / sizeof(WCHAR)));

    UNICODE_STRING sourceIPv4 = {};
    DWORD          sourcePort = DEFAULTS_SRC_PORT;
    UNICODE_STRING destinationIPv4 = {};
    DWORD          destinationPort = DEFAULTS_DST_PORT;
    DWORD          useMulticast = 1;

    DWORD useIVSHMEM = 0;
    DWORD TTL = 0; // default: do not apply
    DWORD silenceThreshold = 0;

    // this might not exist, which is fine
    // Expands to e.g.: HKEY_LOCAL_MACHINE\SOFTWARE\Nefarius Software Solutions e.U.\Scream Audio Streaming Driver\Device\0001
    ntStatus = RtlUnicodeStringPrintf(&keyPath, SETTING_REG_PATH_FMT, m_SlotIndex);

    if (NT_SUCCESS(ntStatus)) {
        TraceVerbose(TRACE_COMMON, "Checking registry path: %wZ", &keyPath);

        RTL_QUERY_REGISTRY_TABLE paramTable[] = {
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"SourceIPv4", &sourceIPv4, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"SourcePort", &sourcePort, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"DestinationIPv4", &destinationIPv4, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"DestinationPort", &destinationPort, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"UseMulticast", &useMulticast, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"UseIVSHMEM", &useIVSHMEM, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"TTL", &TTL, REG_NONE, NULL, 0 },
            { NULL, RTL_QUERY_REGISTRY_DIRECT, L"SilenceThreshold", &silenceThreshold, REG_NONE, NULL, 0 },
            // end of list indicator
            { NULL, 0, NULL, NULL, 0, NULL, 0 }
        };

        ntStatus = RtlQueryRegistryValues(
            RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
            keyPath.Buffer,
            &paramTable[0],
            NULL,
            NULL
        );

        if (!NT_SUCCESS(ntStatus)) {
            TraceWarning(
                TRACE_COMMON,
                "RtlQueryRegistryValues failed with status %!STATUS!",
                ntStatus
            );
            EventWriteQueryRegistrySettingsFailed(NULL, keyPath.Buffer);
            // continue using defaults
        }
    }

    RtlZeroMemory(&m_Settings, sizeof(m_Settings));

    m_Settings.SourceAddress.sin_family = AF_INET;
    m_Settings.SourceAddress.sin_port = RtlUshortByteSwap(sourcePort);
    m_Settings.DestinationAddress.sin_family = AF_INET;
    m_Settings.DestinationAddress.sin_port = RtlUshortByteSwap(destinationPort);

    //
    // Source address value found
    // 
    if (sourceIPv4.Length > 0 && sourceIPv4.Buffer) {
        // this value is _not_ guaranteed to be NULL-terminated so we need to do a bit of converting
        ANSI_STRING narrowSrc = { 0, (USHORT)RtlUnicodeStringToAnsiSize(&sourceIPv4), NULL };
        // conversion makes room for NULL terminator
        if (!NT_SUCCESS(ntStatus = RtlUnicodeStringToAnsiString(&narrowSrc, &sourceIPv4, TRUE))) {
            TraceError(
                TRACE_COMMON,
                "RtlUnicodeStringToAnsiString failed with status %!STATUS!",
                ntStatus
            );
            EventWriteFailedWithNTStatus(NULL, __FUNCTION__, L"RtlUnicodeStringToAnsiString", ntStatus);
        }
        else {
            TraceInformation(TRACE_COMMON, "Got source IPv4: %Z", &narrowSrc);
            PCSTR terminator = NULL;
            // converts and validates string to valid IP address
            if (!NT_SUCCESS(ntStatus = RtlIpv4StringToAddressA(
                narrowSrc.Buffer,
                TRUE,
                &terminator,
                &m_Settings.SourceAddress.sin_addr
            ))) {
                TraceError(
                    TRACE_COMMON,
                    "RtlIpv4StringToAddressA failed with status %!STATUS!",
                    ntStatus
                );
                EventWriteFailedWithNTStatus(NULL, __FUNCTION__, L"RtlIpv4StringToAddressA", ntStatus);
            }

            if (narrowSrc.Buffer)
                ExFreePool(narrowSrc.Buffer);
        }
    }
    // use defaults
    else {
        PCSTR terminator = NULL;
        TraceInformation(TRACE_COMMON, "Using default source IPv4: %s", DEFAULTS_SRC_IPV4);
        (void)RtlIpv4StringToAddressA(
            DEFAULTS_SRC_IPV4,
            TRUE,
            &terminator,
            &m_Settings.SourceAddress.sin_addr
        );
    }

    //
    // Destination address value found
    // 
    if (destinationIPv4.Length > 0 && destinationIPv4.Buffer) {
        // this value is _not_ guaranteed to be NULL-terminated so we need to do a bit of converting
        ANSI_STRING narrowDst = { 0, (USHORT)RtlUnicodeStringToAnsiSize(&destinationIPv4), NULL };
        // conversion makes room for NULL terminator
        if (!NT_SUCCESS(ntStatus = RtlUnicodeStringToAnsiString(&narrowDst, &destinationIPv4, TRUE))) {
            TraceError(
                TRACE_COMMON,
                "RtlUnicodeStringToAnsiString failed with status %!STATUS!",
                ntStatus
            );
            EventWriteFailedWithNTStatus(NULL, __FUNCTION__, L"RtlUnicodeStringToAnsiString", ntStatus);
        }
        else {
            TraceInformation(TRACE_COMMON, "Got destination IPv4: %Z", &narrowDst);
            PCSTR terminator = NULL;
            // converts and validates string to valid IP address
            if (!NT_SUCCESS(ntStatus = RtlIpv4StringToAddressA(
                narrowDst.Buffer,
                TRUE,
                &terminator,
                &m_Settings.DestinationAddress.sin_addr
            ))) {
                TraceError(
                    TRACE_COMMON,
                    "RtlIpv4StringToAddressA failed with status %!STATUS!",
                    ntStatus
                );
                EventWriteFailedWithNTStatus(NULL, __FUNCTION__, L"RtlIpv4StringToAddressA", ntStatus);
            }

            if (narrowDst.Buffer)
                ExFreePool(narrowDst.Buffer);
        }
    }
    // use defaults
    else {
        PCSTR terminator = NULL;
        TraceInformation(TRACE_COMMON, "Using default destination IPv4: %s", DEFAULTS_DST_IPV4);
        (void)RtlIpv4StringToAddressA(
            DEFAULTS_DST_IPV4,
            TRUE,
            &terminator,
            &m_Settings.DestinationAddress.sin_addr
        );
        useMulticast = TRUE;
    }

    m_Settings.UseMulticast = (BOOLEAN)useMulticast;
    m_Settings.UseIVSHMEM = useIVSHMEM < _UI8_MAX ? (UINT8)useIVSHMEM : 0;
    m_Settings.TTL = TTL;
    m_Settings.SilenceThreshold = silenceThreshold;

    if (sourceIPv4.Buffer)
        ExFreePool(sourceIPv4.Buffer);

    if (destinationIPv4.Buffer)
        ExFreePool(destinationIPv4.Buffer);

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP CAdapterCommon::NonDelegatingQueryInterface(
    REFIID  Interface,
    PVOID * Object
) {
    PAGED_CODE();

    ASSERT(Object);

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(PADAPTERCOMMON(this)));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IAdapterCommon)) {
        *Object = PVOID(PADAPTERCOMMON(this));
    }
    else if (IsEqualGUIDAligned(Interface, IID_IAdapterPowerManagement)) {
        *Object = PVOID(PADAPTERPOWERMANAGEMENT(this));
    }
    else {
        *Object = NULL;
    }

    if (*Object) {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
}

STDMETHODIMP_(void)CAdapterCommon::SetWaveServiceGroup(
    IN PSERVICEGROUP ServiceGroup
) {
    FuncEntry(TRACE_COMMON);

    PAGED_CODE();

    if (m_pServiceGroupWave) {
        m_pServiceGroupWave->Release();
    }

    m_pServiceGroupWave = ServiceGroup;

    if (m_pServiceGroupWave) {
        m_pServiceGroupWave->AddRef();
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(PUNKNOWN *)CAdapterCommon::WavePortDriverDest(void) {
    PAGED_CODE();

    return (PUNKNOWN *)&m_pPortWave;
}
#pragma code_seg()

STDMETHODIMP_(BOOL)CAdapterCommon::bDevSpecificRead() {
    if (m_pHW) {
        return m_pHW->bGetDevSpecific();
    }

    return FALSE;
}

STDMETHODIMP_(void)CAdapterCommon::bDevSpecificWrite(
    IN BOOL bDevSpecific
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->bSetDevSpecific(bDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(INT)CAdapterCommon::iDevSpecificRead() {
    if (m_pHW) {
        return m_pHW->iGetDevSpecific();
    }

    return 0;
}

STDMETHODIMP_(void)CAdapterCommon::iDevSpecificWrite(
    IN INT iDevSpecific
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->iSetDevSpecific(iDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(UINT)CAdapterCommon::uiDevSpecificRead() {
    if (m_pHW) {
        return m_pHW->uiGetDevSpecific();
    }

    return 0;
}

STDMETHODIMP_(void)CAdapterCommon::uiDevSpecificWrite(
    IN UINT uiDevSpecific
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->uiSetDevSpecific(uiDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(BOOL)CAdapterCommon::MixerMuteRead(
    IN ULONG Index
) {
    if (m_pHW) {
        return m_pHW->GetMixerMute(Index);
    }

    return 0;
}

STDMETHODIMP_(void)CAdapterCommon::MixerMuteWrite(
    IN ULONG Index,
    IN BOOL  Value
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerMute(Index, Value);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(ULONG)CAdapterCommon::MixerMuxRead() {
    if (m_pHW) {
        return m_pHW->GetMixerMux();
    }

    return 0;
}

STDMETHODIMP_(void)CAdapterCommon::MixerMuxWrite(
    IN ULONG Index
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerMux(Index);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(LONG)CAdapterCommon::MixerVolumeRead(
    IN ULONG Index,
    IN LONG  Channel
) {
    if (m_pHW) {
        return m_pHW->GetMixerVolume(Index, Channel);
    }

    return 0;
}

STDMETHODIMP_(void)CAdapterCommon::MixerVolumeWrite(
    IN ULONG Index,
    IN LONG  Channel,
    IN LONG  Value
) {
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerVolume(Index, Channel, Value);
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(UINT32)CAdapterCommon::GetDeviceIndex() {
    return m_SlotIndex;
}

STDMETHODIMP_(CONST ADAPTER_COMMON_SETTINGS*)CAdapterCommon::GetAdapterSettings() CONST {
    return &m_Settings;
}

STDMETHODIMP_(void)CAdapterCommon::PowerChangeState(
    IN POWER_STATE NewState
) {
    FuncEntry(TRACE_COMMON);

    // is this actually a state change??
    if (NewState.DeviceState != m_PowerState) {
        // switch on new state
        switch (NewState.DeviceState) {
        case PowerDeviceD0:
        case PowerDeviceD1:
        case PowerDeviceD2:
        case PowerDeviceD3:
            m_PowerState = NewState.DeviceState;
            TraceVerbose(TRACE_COMMON, "Entering D%d", ULONG(m_PowerState) - ULONG(PowerDeviceD0));
            break;

        default:
            TraceWarning(TRACE_COMMON, "Unknown Device Power State");
            break;
        }
    }

    FuncExitNoReturn(TRACE_COMMON);
}

STDMETHODIMP_(NTSTATUS)CAdapterCommon::QueryDeviceCapabilities(
    IN PDEVICE_CAPABILITIES PowerDeviceCaps
) {
    UNREFERENCED_PARAMETER(PowerDeviceCaps);

    DPF_ENTER(("[CAdapterCommon::QueryDeviceCapabilities]"));

    return (STATUS_SUCCESS);
}

STDMETHODIMP_(NTSTATUS)CAdapterCommon::QueryPowerChangeState(
    IN POWER_STATE NewStateQuery
) {
    UNREFERENCED_PARAMETER(NewStateQuery);

    DPF_ENTER(("[CAdapterCommon::QueryPowerChangeState]"));

    return STATUS_SUCCESS;
}
