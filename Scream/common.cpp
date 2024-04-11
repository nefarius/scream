/*++
Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:
    common.cpp

Abstract:
    Implementation of the AdapterCommon class. 
--*/

#pragma warning (disable : 4127)

#include "scream.h"
#include "common.h"
#include "hw.h"
#include "savedata.h"
#include "ivshmemsavedata.h"
#include "common.tmh"

#include <ntstrsafe.h>

//-----------------------------------------------------------------------------
// Externals
//-----------------------------------------------------------------------------
PSAVEWORKER_PARAM CSaveData::m_pWorkItem = NULL;
PDEVICE_OBJECT    CSaveData::m_pDeviceObject = NULL;

PIVSHMEM_SAVEWORKER_PARAM    CIVSHMEMSaveData::m_pWorkItem = NULL;
PDEVICE_OBJECT               CIVSHMEMSaveData::m_pDeviceObject = NULL;

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
    static LONG m_Slots[8]; // 256 usable bits
    static LONG SetSlot(UINT32 SlotIndex);
    static BOOLEAN TestSlot(UINT32 SlotIndex);
    static LONG ClearSlot(UINT32 SlotIndex);

    _IRQL_requires_max_(PASSIVE_LEVEL)
    NTSTATUS QueryAdapterRegistrySettings();

public:
    //=====================================================================
    // Default CUnknown
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CAdapterCommon);
    ~CAdapterCommon();

    //=====================================================================
    // Default IAdapterPowerManagement
    IMP_IAdapterPowerManagement;

    //=====================================================================
    // IAdapterCommon methods                                               
    STDMETHODIMP_(NTSTATUS)         Init(IN PDEVICE_OBJECT DeviceObject);
    STDMETHODIMP_(PDEVICE_OBJECT)   GetDeviceObject(void);
    STDMETHODIMP_(PUNKNOWN *)       WavePortDriverDest(void);
    STDMETHODIMP_(void)             SetWaveServiceGroup(IN PSERVICEGROUP ServiceGroup);
    
    STDMETHODIMP_(BOOL)     bDevSpecificRead();
    STDMETHODIMP_(void)     bDevSpecificWrite(IN BOOL bDevSpecific);
    
    STDMETHODIMP_(INT)      iDevSpecificRead();
    STDMETHODIMP_(void)     iDevSpecificWrite(IN INT iDevSpecific);
    
    STDMETHODIMP_(UINT)     uiDevSpecificRead();
    STDMETHODIMP_(void)     uiDevSpecificWrite(IN UINT uiDevSpecific);

    STDMETHODIMP_(BOOL)     MixerMuteRead(IN ULONG Index);
    STDMETHODIMP_(void)     MixerMuteWrite(IN ULONG Index, IN BOOL Value);
    STDMETHODIMP_(ULONG)    MixerMuxRead(void);
    STDMETHODIMP_(void)     MixerMuxWrite(IN  ULONG Index);
    STDMETHODIMP_(void)     MixerReset(void);
    STDMETHODIMP_(LONG)     MixerVolumeRead(IN ULONG Index, IN LONG Channel);
    STDMETHODIMP_(void)     MixerVolumeWrite(IN ULONG Index, IN LONG Channel, IN LONG Value);

    STDMETHODIMP_(UINT32)                   GetDeviceIndex(void);
    STDMETHODIMP_(ADAPTER_COMMON_SETTINGS)  GetAdapterSettings(void);

    //=====================================================================
    // friends
    friend NTSTATUS NewAdapterCommon(OUT PADAPTERCOMMON* OutAdapterCommon, IN PRESOURCELIST ResourceList);
};

LONG CAdapterCommon::m_Slots[8] = {0};

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

//=============================================================================
#pragma code_seg("PAGE")
NTSTATUS NewAdapterCommon( 
    OUT PUNKNOWN *              Unknown,
    IN  REFCLSID,
    IN  PUNKNOWN                UnknownOuter OPTIONAL,
    IN  POOL_TYPE               PoolType 
)
/*++
Routine Description:
  Creates a new CAdapterCommon

Arguments:
  Unknown - 
  UnknownOuter -
  PoolType

Return Value:
  NT status code.
--*/
{
    PAGED_CODE();

    ASSERT(Unknown);

    STD_CREATE_BODY_(CAdapterCommon, Unknown, UnknownOuter, PoolType, PADAPTERCOMMON);
} // NewAdapterCommon

//=============================================================================
CAdapterCommon::~CAdapterCommon(void)
/*++
Routine Description:
  Destructor for CAdapterCommon.

Arguments:

Return Value:
  void
--*/
{
    PAGED_CODE();

    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        delete m_pHW;
    }

    if (g_UseIVSHMEM) {
        CIVSHMEMSaveData::DestroyWorkItems();
    }
    else {
        CSaveData::DestroyWorkItems();
    }

    if (m_pPortWave) {
        m_pPortWave->Release();
    }

    if (m_pServiceGroupWave) {
        m_pServiceGroupWave->Release();
    }

    ClearSlot(m_SlotIndex);

    FuncExitNoReturn(TRACE_COMMON);
} // ~CAdapterCommon  

//=============================================================================
STDMETHODIMP_(PDEVICE_OBJECT) CAdapterCommon::GetDeviceObject(void)
/*++
Routine Description:
  Returns the deviceobject

Arguments:

Return Value:
  PDEVICE_OBJECT
--*/
{
    PAGED_CODE();

    return m_pDeviceObject;
} // GetDeviceObject

//=============================================================================
NTSTATUS CAdapterCommon::Init(IN PDEVICE_OBJECT DeviceObject)
/*++
Routine Description:
    Initialize adapter common object.

Arguments:
    DeviceObject - pointer to the device object

Return Value:
  NT status code.
--*/
{
    FuncEntry(TRACE_COMMON);

    PAGED_CODE();

    ASSERT(DeviceObject);

    NTSTATUS ntStatus = STATUS_SUCCESS;

    UINT32 slotIndex;

    //
    // Get next free slot
    // 
    for (slotIndex = 1; slotIndex <= MAX_DEVICES; slotIndex++) {
        if (!TestSlot(slotIndex)) {
            SetSlot(slotIndex);
            ntStatus = STATUS_SUCCESS;

            TraceVerbose(
                TRACE_COMMON,
                "Claimed device slot: %d",
                slotIndex
            );

            break;
        }
    }

    //
    // We've reached the maximum allowed without any success
    // 
    if (slotIndex > MAX_DEVICES) {
        ntStatus = STATUS_NO_MORE_ENTRIES;
    }

    if (!NT_SUCCESS(ntStatus)) {
        goto exit;
    }
    
    m_pDeviceObject = DeviceObject;
    m_PowerState = PowerDeviceD0;
    RtlZeroMemory(&m_Settings, sizeof(m_Settings));

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
        CSaveData::SetDeviceObject(DeviceObject); //device object is needed by CSaveData
    }

    exit:
    FuncExit(TRACE_COMMON, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
} // Init

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::MixerReset(void)
/*++
Routine Description:
  Reset mixer registers from registry.

Arguments:

Return Value:
  void
--*/
{
    FuncEntry(TRACE_COMMON);

    PAGED_CODE();
    
    if (m_pHW) {
        m_pHW->MixerReset();
    }

    FuncExitNoReturn(TRACE_COMMON);
} // MixerReset

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
NTSTATUS CAdapterCommon::QueryAdapterRegistrySettings() {
    FuncEntry(TRACE_COMMON);

    NTSTATUS ntStatus;

    DECLARE_UNICODE_STRING_SIZE(keyPath, 128);

    ntStatus = RtlUnicodeStringPrintf(
        &keyPath,
        L"\\Registry\\Machine\\SOFTWARE\\Nefarius Software Solutions e.U.\\Scream Audio Streaming Driver\\Device\\%04d",
        m_SlotIndex
    );
    if (!NT_SUCCESS(ntStatus)) {
        TraceError(
            TRACE_COMMON, 
            "RtlUnicodeStringPrintf failed with status %!STATUS!",
            ntStatus
        );
        EventWriteFailedWithNTStatus(NULL, __FUNCTION__, L"RtlUnicodeStringPrintf", ntStatus);
        goto exit;
    }

    UNICODE_STRING sourceIPv4;
    DWORD sourcePort = 0;
    UNICODE_STRING destinationIPv4;
    DWORD destinationPort = 0;
    DWORD useMulticast = 1;

    RtlZeroMemory(&destinationIPv4, sizeof(UNICODE_STRING));
    RtlZeroMemory(&sourceIPv4, sizeof(UNICODE_STRING));

    DWORD useIVSHMEM = 0;
    DWORD DSCP = 0;
    DWORD TTL = 0;
    DWORD silenceThreshold = 0;

    RTL_QUERY_REGISTRY_TABLE paramTable[] = {
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"SourceIPv4", &sourceIPv4, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"SourcePort", &sourcePort, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"DestinationIPv4", &destinationIPv4, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"DestinationPort", &destinationPort, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"UseMulticast", &useMulticast, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"UseIVSHMEM", &useIVSHMEM, REG_NONE, NULL, 0 },
        { NULL, RTL_QUERY_REGISTRY_DIRECT, L"DSCP", &DSCP, REG_NONE, NULL, 0 },
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

    m_Settings.SourceAddress.sin_family = AF_INET;
    m_Settings.DestinationAddress.sin_family = AF_INET;    
    
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
        (void)RtlIpv4StringToAddressA(
            DEFAULTS_SRC_IPV4,
            TRUE,
            &terminator,
            &m_Settings.SourceAddress.sin_addr
        );
    }

    // TODO: implement me

exit:
    if (sourceIPv4.Buffer)
        ExFreePool(sourceIPv4.Buffer);

    if (destinationIPv4.Buffer)
        ExFreePool(destinationIPv4.Buffer);

    FuncExit(TRACE_COMMON, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
}

//=============================================================================
STDMETHODIMP CAdapterCommon::NonDelegatingQueryInterface( 
    REFIID                      Interface,
    PVOID *                     Object 
)
/*++
Routine Description:
  QueryInterface routine for AdapterCommon

Arguments:
  Interface - 
  Object -

Return Value:
  NT status code.
--*/
{
    PAGED_CODE();

    ASSERT(Object);

    if (IsEqualGUIDAligned(Interface, IID_IUnknown)) {
        *Object = PVOID(PUNKNOWN(PADAPTERCOMMON(this)));
    } else if (IsEqualGUIDAligned(Interface, IID_IAdapterCommon)) {
        *Object = PVOID(PADAPTERCOMMON(this));
    } else if (IsEqualGUIDAligned(Interface, IID_IAdapterPowerManagement)) {
        *Object = PVOID(PADAPTERPOWERMANAGEMENT(this));
    } else {
        *Object = NULL;
    }

    if (*Object) {
        PUNKNOWN(*Object)->AddRef();
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_PARAMETER;
} // NonDelegatingQueryInterface

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::SetWaveServiceGroup(
    IN PSERVICEGROUP ServiceGroup
)
/*++
Routine Description:

Arguments:

Return Value:
  NT status code.
--*/
{
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
} // SetWaveServiceGroup

//=============================================================================
STDMETHODIMP_(PUNKNOWN *) CAdapterCommon::WavePortDriverDest(void)
/*++
Routine Description:
  Returns the wave port.

Arguments:

Return Value:
  PUNKNOWN : pointer to waveport
--*/
{
    PAGED_CODE();

    return (PUNKNOWN *)&m_pPortWave;
} // WavePortDriverDest
#pragma code_seg()

//=============================================================================
STDMETHODIMP_(BOOL) CAdapterCommon::bDevSpecificRead()
/*++
Routine Description:
  Fetch Device Specific information.

Arguments:
  N/A

Return Value:
  BOOL - Device Specific info
--*/
{
    if (m_pHW) {
        return m_pHW->bGetDevSpecific();
    }

    return FALSE;
} // bDevSpecificRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::bDevSpecificWrite(
    IN  BOOL                    bDevSpecific
)
/*++
Routine Description:
  Store the new value in the Device Specific location.

Arguments:
  bDevSpecific - Value to store

Return Value:
  N/A.
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->bSetDevSpecific(bDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // DevSpecificWrite

//=============================================================================
STDMETHODIMP_(INT) CAdapterCommon::iDevSpecificRead()
/*++
Routine Description:
  Fetch Device Specific information.

Arguments:
  N/A

Return Value:
  INT - Device Specific info
--*/
{
    if (m_pHW) {
        return m_pHW->iGetDevSpecific();
    }

    return 0;
} // iDevSpecificRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::iDevSpecificWrite(
    IN  INT                    iDevSpecific
)
/*++
Routine Description:
  Store the new value in the Device Specific location.

Arguments:
  iDevSpecific - Value to store

Return Value:
  N/A.
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->iSetDevSpecific(iDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // iDevSpecificWrite

//=============================================================================
STDMETHODIMP_(UINT) CAdapterCommon::uiDevSpecificRead()
/*++
Routine Description:
  Fetch Device Specific information.

Arguments:
  N/A

Return Value:
  UINT - Device Specific info
--*/
{
    if (m_pHW) {
        return m_pHW->uiGetDevSpecific();
    }

    return 0;
} // uiDevSpecificRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::uiDevSpecificWrite(
    IN  UINT                    uiDevSpecific
)
/*++
Routine Description:
  Store the new value in the Device Specific location.

Arguments:
  uiDevSpecific - Value to store

Return Value:
  N/A.
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->uiSetDevSpecific(uiDevSpecific);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // uiDevSpecificWrite

//=============================================================================
STDMETHODIMP_(BOOL) CAdapterCommon::MixerMuteRead(
    IN  ULONG                   Index
)
/*++
Routine Description:
  Store the new value in mixer register array.

Arguments:
  Index - node id

Return Value:
  BOOL - mixer mute setting for this node
--*/
{
    if (m_pHW) {
        return m_pHW->GetMixerMute(Index);
    }

    return 0;
} // MixerMuteRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::MixerMuteWrite(
    IN  ULONG                   Index,
    IN  BOOL                    Value
)
/*++
Routine Description:
  Store the new value in mixer register array.

Arguments:
  Index - node id
  Value - new mute settings

Return Value:
  NT status code.
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerMute(Index, Value);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // MixerMuteWrite

//=============================================================================
STDMETHODIMP_(ULONG) CAdapterCommon::MixerMuxRead() 
/*++
Routine Description:
  Return the mux selection

Arguments:
  Index - node id
  Value - new mute settings

Return Value:
  NT status code.
--*/
{
    if (m_pHW) {
        return m_pHW->GetMixerMux();
    }

    return 0;
} // MixerMuxRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::MixerMuxWrite(
    IN  ULONG                   Index
)
/*++
Routine Description:
  Store the new mux selection

Arguments:
  Index - node id
  Value - new mute settings

Return Value:
  NT status code.
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerMux(Index);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // MixerMuxWrite

//=============================================================================
STDMETHODIMP_(LONG) CAdapterCommon::MixerVolumeRead( 
    IN  ULONG                   Index,
    IN  LONG                    Channel
)
/*++
Routine Description:
  Return the value in mixer register array.

Arguments:
  Index - node id
  Channel = which channel

Return Value:
    Byte - mixer volume settings for this line
--*/
{
    if (m_pHW) {
        return m_pHW->GetMixerVolume(Index, Channel);
    }

    return 0;
} // MixerVolumeRead

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::MixerVolumeWrite( 
    IN  ULONG                   Index,
    IN  LONG                    Channel,
    IN  LONG                    Value
)
/*++
Routine Description:
  Store the new value in mixer register array.

Arguments:
  Index - node id
  Channel - which channel
  Value - new volume level

Return Value:
  void
--*/
{
    FuncEntry(TRACE_COMMON);

    if (m_pHW) {
        m_pHW->SetMixerVolume(Index, Channel, Value);
    }

    FuncExitNoReturn(TRACE_COMMON);
} // MixerVolumeWrite

STDMETHODIMP_(UINT32) CAdapterCommon::GetDeviceIndex() {
    return m_SlotIndex;
}

STDMETHODIMP_(ADAPTER_COMMON_SETTINGS) CAdapterCommon::GetAdapterSettings() {
    return m_Settings;
}

//=============================================================================
STDMETHODIMP_(void) CAdapterCommon::PowerChangeState( 
    IN  POWER_STATE             NewState 
)
/*++
Routine Description:

Arguments:
  NewState - The requested, new power state for the device. 

Return Value:
  void
--*/
{
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
} // PowerStateChange

//=============================================================================
STDMETHODIMP_(NTSTATUS) CAdapterCommon::QueryDeviceCapabilities( 
    IN  PDEVICE_CAPABILITIES    PowerDeviceCaps 
)
/*++
Routine Description:
    Called at startup to get the caps for the device.  This structure provides 
    the system with the mappings between system power state and device power 
    state.  This typically will not need modification by the driver.         

Arguments:
  PowerDeviceCaps - The device's capabilities. 

Return Value:
  NT status code.
--*/
{
    UNREFERENCED_PARAMETER(PowerDeviceCaps);

    DPF_ENTER(("[CAdapterCommon::QueryDeviceCapabilities]"));

    return (STATUS_SUCCESS);
} // QueryDeviceCapabilities

//=============================================================================
STDMETHODIMP_(NTSTATUS) CAdapterCommon::QueryPowerChangeState( 
    IN  POWER_STATE             NewStateQuery 
)
/*++
Routine Description:
  Query to see if the device can change to this power state 

Arguments:
  NewStateQuery - The requested, new power state for the device

Return Value:
  NT status code.
--*/
{
    UNREFERENCED_PARAMETER(NewStateQuery);

    DPF_ENTER(("[CAdapterCommon::QueryPowerChangeState]"));

    return STATUS_SUCCESS;
} // QueryPowerChangeState
