#pragma warning (disable : 4127)

#include "scream.h"
#include "savedata.h"
#include "savedata.tmh"

//=============================================================================
// Defines
//=============================================================================
#define MULTICAST_TARGET    "239.255.77.77"
#define MULTICAST_PORT      4010
#define PCM_PAYLOAD_SIZE    1152                        // PCM payload size (divisible by 2, 3 and 4 bytes per sample * 2 channels)
#define HEADER_SIZE         5                           // m_bSamplingFreqMarker, m_bBitsPerSampleMarker, m_bChannels, m_wChannelMask
#define CHUNK_SIZE          (PCM_PAYLOAD_SIZE + HEADER_SIZE)      // Add two bytes so we can send a small header with bytes/sample and sampling freq markers
#define NUM_CHUNKS          800                         // How many payloads in ring buffer
#define BUFFER_SIZE         CHUNK_SIZE * NUM_CHUNKS     // Ring buffer size

//=============================================================================
// Statics
//=============================================================================

// Client-level callback table
const WSK_CLIENT_DISPATCH WskClientDispatch = {
    MAKE_WSK_VERSION(1, 0), // This sample uses WSK version 1.0
    0, // Reserved
    NULL // WskClientEvent callback is not required in WSK version 1.0
};

//=============================================================================
// Helper Functions
//=============================================================================
// IRP completion routine used for synchronously waiting for completion
NTSTATUS SocketRequestCompletionRoutine(__in PDEVICE_OBJECT Reserved, __in PIRP Irp, __in PVOID Context) {
    FuncEntry(TRACE_SAVEDATA);

    PKEVENT compEvent = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(Reserved);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(compEvent, 2, FALSE);

    FuncExitNoReturn(TRACE_SAVEDATA);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

#pragma code_seg("PAGE")
//=============================================================================
// CSaveData
//=============================================================================

//=============================================================================
CSaveData::CSaveData() : m_socket(NULL), m_pBuffer(NULL), m_ulOffset(0), m_ulSendOffset(0), m_fWriteDisabled(FALSE) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();
    
    if (!g_UseIVSHMEM) {
        WSK_CLIENT_NPI wskClientNpi;

        // allocate work item for this stream
        m_pWorkItem = (PSAVEWORKER_PARAM)ExAllocatePoolWithTag(NonPagedPool, sizeof(SAVEWORKER_PARAM), SCREAM_POOLTAG);
        if (m_pWorkItem) {
            m_pWorkItem->WorkItem = IoAllocateWorkItem(GetDeviceObject());
            KeInitializeEvent(&(m_pWorkItem->EventDone), NotificationEvent, TRUE);
        }

        // get us an IRP
        m_irp = IoAllocateIrp(1, FALSE);

        // initialize io completion synchronization event
        KeInitializeEvent(&m_syncEvent, SynchronizationEvent, FALSE);

        // Register with WSK.
        wskClientNpi.ClientContext = NULL;
        wskClientNpi.Dispatch = &WskClientDispatch;
        WskRegister(&wskClientNpi, &m_wskRegistration);
    }

    FuncExitNoReturn(TRACE_SAVEDATA);
} // CSaveData

//=============================================================================
CSaveData::~CSaveData() {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();

    if (!g_UseIVSHMEM) {
        // frees the work item
        if (m_pWorkItem->WorkItem != NULL) {
            IoFreeWorkItem(m_pWorkItem->WorkItem);
            m_pWorkItem->WorkItem = NULL;
        }

        // close socket
        if (m_socket) {
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        }

        // Deregister with WSK. This call will wait until all the references to
        // the WSK provider NPI are released and all the sockets are closed. Note
        // that if the worker thread has not started yet, then when it eventually
        // starts, its WskCaptureProviderNPI call will fail and the work queue
        // will be flushed and cleaned up properly.
        WskDeregister(&m_wskRegistration);

        // free irp
        IoFreeIrp(m_irp);

        if (m_pBuffer) {
            ExFreePoolWithTag(m_pBuffer, SCREAM_POOLTAG);
            IoFreeMdl(m_pMdl);
        }
    }

    FuncExitNoReturn(TRACE_SAVEDATA);
} // CSaveData

//=============================================================================
void CSaveData::DestroyWorkItems(void) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();
        
    if (m_pWorkItem) {
        ExFreePoolWithTag(m_pWorkItem, SCREAM_POOLTAG);
        m_pWorkItem = NULL;
    }

    FuncExitNoReturn(TRACE_SAVEDATA);
} // DestroyWorkItems

//=============================================================================
void CSaveData::Disable(BOOL fDisable) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();

    m_fWriteDisabled = fDisable;

    FuncExitNoReturn(TRACE_SAVEDATA);
} // Disable

//=============================================================================
NTSTATUS CSaveData::SetDeviceObject(IN PDEVICE_OBJECT DeviceObject) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();

    ASSERT(DeviceObject);
    
    m_pDeviceObject = DeviceObject;

    FuncExitNoReturn(TRACE_SAVEDATA);

    return STATUS_SUCCESS;
}

//=============================================================================
PDEVICE_OBJECT CSaveData::GetDeviceObject(void) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();

    FuncExit(TRACE_SAVEDATA, "devObj=0x%p", m_pDeviceObject);

    return m_pDeviceObject;
}

#pragma code_seg("PAGE")
//=============================================================================
NTSTATUS CSaveData::Initialize(DWORD nSamplesPerSec, WORD wBitsPerSample, WORD nChannels, DWORD dwChannelMask) {
    FuncEntryArguments(
        TRACE_SAVEDATA, 
        "nSamplesPerSec=%lu,wBitsPerSample=%d,nChannels=%d,dwChannelMask=%lu",
        nSamplesPerSec, wBitsPerSample, nChannels, dwChannelMask
    );

    PAGED_CODE();

    NTSTATUS          ntStatus = STATUS_SUCCESS;
    
    // Only multiples of 44100 and 48000 are supported
    m_bSamplingFreqMarker  = (BYTE)((nSamplesPerSec % 44100) ? (0 + (nSamplesPerSec / 48000)) : (128 + (nSamplesPerSec / 44100)));
    m_bBitsPerSampleMarker = (BYTE)(wBitsPerSample);
    m_bChannels = (BYTE)nChannels;
    m_wChannelMask = (WORD)dwChannelMask;

    // Allocate memory for data buffer.
    if (NT_SUCCESS(ntStatus)) {
        m_pBuffer = (PBYTE) ExAllocatePoolWithTag(NonPagedPool, BUFFER_SIZE, SCREAM_POOLTAG);
        if (!m_pBuffer) {
            TraceError(TRACE_SAVEDATA, "Could not allocate memory for sending data");
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Allocate MDL for the data buffer
    if (NT_SUCCESS(ntStatus)) {
        m_pMdl = IoAllocateMdl(m_pBuffer, BUFFER_SIZE, FALSE, FALSE, NULL);
        if (m_pMdl == NULL) {
            TraceError(TRACE_SAVEDATA, "Failed to allocate MDL");
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            MmBuildMdlForNonPagedPool(m_pMdl);
        }
    }

    FuncExit(TRACE_SAVEDATA, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
} // Initialize

//=============================================================================
VOID SendDataWorkerCallback(PDEVICE_OBJECT pDeviceObject, IN PVOID Context) {
    FuncEntry(TRACE_SAVEDATA);

    UNREFERENCED_PARAMETER(pDeviceObject);

    PAGED_CODE();

    ASSERT(Context);

    const PSAVEWORKER_PARAM pParam = (PSAVEWORKER_PARAM) Context;

    ASSERT(pParam->pSaveData);

    if (pParam->WorkItem) {
        const PCSaveData pSaveData = pParam->pSaveData;
        pSaveData->SendData();
    }

    KeSetEvent(&(pParam->EventDone), 0, FALSE);

    FuncExitNoReturn(TRACE_SAVEDATA);
} // SendDataWorkerCallback

// Prototype for the control socket IoCompletion routine
static
NTSTATUS
ControlSocketComplete(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp,
	PVOID Context
);

// Function to set socket options
NTSTATUS SetSockOpt(PWSK_SOCKET Socket, ULONG level, ULONG option_name, ULONG option_value) {
    FuncEntry(TRACE_SAVEDATA);

    PWSK_PROVIDER_BASIC_DISPATCH Dispatch;
    PIRP Irp;
    //ULONG		SocketOptionState;
    NTSTATUS ntStatus;

    Dispatch = (PWSK_PROVIDER_BASIC_DISPATCH)(Socket->Dispatch);

    // Allocate an IRP
    Irp = IoAllocateIrp(1, FALSE);

    // Check result
    if (!Irp) {
        // Return error
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Set the completion routine for the IRP
    IoSetCompletionRoutine(
        Irp,
        ControlSocketComplete,
        Socket,  // Use the socket object for the context
        TRUE,
        TRUE,
        TRUE
    );

    // Initiate the control operation on the socket
    ntStatus = Dispatch->WskControlSocket(
        Socket,
        WskSetOption,
        option_name,
        level,
        sizeof(ULONG),
        &option_value,
        0,
        NULL,
        NULL,
        Irp
    );

    FuncExit(TRACE_SAVEDATA, "ntStatus=%!STATUS!", ntStatus);

    return ntStatus;
}

// Control socket IoCompletion routine
static
NTSTATUS
ControlSocketComplete(
	PDEVICE_OBJECT DeviceObject,
	PIRP Irp,
	PVOID Context
)
{
    FuncEntry(TRACE_SAVEDATA);

	UNREFERENCED_PARAMETER(DeviceObject);
	UNREFERENCED_PARAMETER(Context);

	// Free the IRP
	IoFreeIrp(Irp);

    FuncExitNoReturn(TRACE_SAVEDATA);

	// Always return STATUS_MORE_PROCESSING_REQUIRED to
	// terminate the completion processing of the IRP.
	return STATUS_MORE_PROCESSING_REQUIRED;
}

#pragma code_seg()

//=============================================================================
_IRQL_requires_max_(PASSIVE_LEVEL)
void CSaveData::CreateSocket(void) {
    FuncEntry(TRACE_SAVEDATA);

    NTSTATUS status;
    WSK_PROVIDER_NPI pronpi;
    LPCTSTR terminator;
    SOCKADDR_IN locaddr4 = { AF_INET, RtlUshortByteSwap((USHORT)g_UnicastSrcPort), 0, 0 };
    SOCKADDR_IN sockaddr = { AF_INET, RtlUshortByteSwap((USHORT)g_UnicastPort), 0, 0 };
    
    // capture WSK provider
    status = WskCaptureProviderNPI(&m_wskRegistration, WSK_INFINITE_WAIT, &pronpi);
    if (!NT_SUCCESS(status)) {
        TraceError(TRACE_SAVEDATA, "WskCaptureProviderNPI failed with status %!STATUS!", status);
        FuncExitNoReturn(TRACE_SAVEDATA);
        return;
    }

    RtlIpv4StringToAddress(g_UnicastSrcIPv4, true, &terminator, &(locaddr4.sin_addr));
    RtlIpv4StringToAddress(g_UnicastIPv4, true, &terminator, &(sockaddr.sin_addr));
    RtlCopyMemory(&m_sServerAddr, &sockaddr, sizeof(SOCKADDR_IN));

    // create socket
    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
    pronpi.Dispatch->WskSocket(
        pronpi.Client,
        m_sServerAddr.ss_family,
        SOCK_DGRAM,
        IPPROTO_UDP,
        WSK_FLAG_DATAGRAM_SOCKET,
        NULL, // socket context
        NULL, // dispatch
        NULL, // Process
        NULL, // Thread
        NULL, // SecurityDescriptor
        m_irp);
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);

    DPF(D_TERSE, ("WskSocket: %x", m_irp->IoStatus.Status));

    if (!NT_SUCCESS(m_irp->IoStatus.Status)) {
        TraceError(TRACE_SAVEDATA, "Socket creation failed with status %!STATUS!", m_irp->IoStatus.Status);

        if (m_socket) {
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        }

        // release the provider again, as we are finished with it
        WskReleaseProviderNPI(&m_wskRegistration);

        FuncExitNoReturn(TRACE_SAVEDATA);
        return;
    }

    // save the socket
    m_socket = (PWSK_SOCKET)m_irp->IoStatus.Information;

    // release the provider again, as we are finished with it
    WskReleaseProviderNPI(&m_wskRegistration);

    // bind the socket
    IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
    IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);

    status = SetSockOpt(m_socket, SOL_SOCKET, SO_REUSEADDR, 1);

    if (g_TTL) {
        // should check for unicasst and set IP_TTL
        status = SetSockOpt(m_socket, IPPROTO_IP, IP_MULTICAST_TTL, g_TTL);
    }

// if (g_DSCP) status = SetSockOpt(m_socket, IPPROTO_IP, IP_TOS, (g_DSCP << 2) & 0xff);  // no support in kernel - raw socket and IP_HDRINCL?

    status = ((PWSK_PROVIDER_DATAGRAM_DISPATCH)(m_socket->Dispatch))->WskBind(m_socket, (PSOCKADDR)(&locaddr4), 0, m_irp);
    KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);

    DPF(D_TERSE, ("WskBind: %x", m_irp->IoStatus.Status));

    if (!NT_SUCCESS(m_irp->IoStatus.Status)) {
        TraceError(TRACE_SAVEDATA, "Socket bind failed with status %!STATUS!", m_irp->IoStatus.Status);
        if (m_socket) {
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_BASIC_DISPATCH)m_socket->Dispatch)->WskCloseSocket(m_socket, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
        }

        FuncExitNoReturn(TRACE_SAVEDATA);
        return;
    }

    FuncExitNoReturn(TRACE_SAVEDATA);
}

//=============================================================================
_IRQL_requires_max_(PASSIVE_LEVEL)
void CSaveData::SendData() {
    FuncEntry(TRACE_SAVEDATA);

    WSK_BUF wskbuf;

    ULONG storeOffset;

    if (!m_socket) {
        CreateSocket();
    }

    if (m_socket) {
        while (TRUE) {
            // Read latest storeOffset. There might be new data.
            storeOffset = m_ulOffset;

            // Abort if there's nothing to send. Note: When storeOffset < sendOffset, we can always send a chunk.
            if ((storeOffset >= m_ulSendOffset) && ((storeOffset - m_ulSendOffset) < CHUNK_SIZE))
                break;

            // Send a chunk
            wskbuf.Mdl = m_pMdl;
            wskbuf.Length = CHUNK_SIZE;
            wskbuf.Offset = m_ulSendOffset;
            IoReuseIrp(m_irp, STATUS_UNSUCCESSFUL);
            IoSetCompletionRoutine(m_irp, SocketRequestCompletionRoutine, &m_syncEvent, TRUE, TRUE, TRUE);
            ((PWSK_PROVIDER_DATAGRAM_DISPATCH)(m_socket->Dispatch))->WskSendTo(m_socket, &wskbuf, 0, (PSOCKADDR)&m_sServerAddr, 0, NULL, m_irp);
            KeWaitForSingleObject(&m_syncEvent, Executive, KernelMode, FALSE, NULL);
            DPF(D_TERSE, ("WskSendTo: %x", m_irp->IoStatus.Status));

            m_ulSendOffset += CHUNK_SIZE;
            if (m_ulSendOffset >= BUFFER_SIZE) m_ulSendOffset = 0;
        }
    }

    FuncExitNoReturn(TRACE_SAVEDATA);
}

#pragma code_seg("PAGE")
//=============================================================================
void CSaveData::WaitAllWorkItems(void) {
    FuncEntry(TRACE_SAVEDATA);

    PAGED_CODE();

    KeWaitForSingleObject(&(m_pWorkItem->EventDone), Executive, KernelMode, FALSE, NULL);

    FuncExitNoReturn(TRACE_SAVEDATA);
} // WaitAllWorkItems

#pragma code_seg()
//=============================================================================
void CSaveData::WriteData(IN PBYTE pBuffer, IN ULONG ulByteCount) {
    FuncEntryArguments(TRACE_SAVEDATA, "ulByteCount=%ul", ulByteCount);

    ASSERT(pBuffer);

    LARGE_INTEGER timeOut = { 0 };
    NTSTATUS ntStatus;
    ULONG offset;
    ULONG toWrite;
    ULONG w;

    if (m_fWriteDisabled) {
        goto exit;
    }

    // Undersized (paranoia)
    if (0 == ulByteCount) {
        goto exit;
    }

    // Oversized (paranoia)
    if (ulByteCount > (CHUNK_SIZE * NUM_CHUNKS / 2)) {
        goto exit;
    }

    // Append to ring buffer. Don't write intermediate states to m_ulOffset,
    // but update it once at the end.
    offset = m_ulOffset;
    toWrite = ulByteCount;
    while (toWrite > 0) {
        w = offset % CHUNK_SIZE;
        if (w > 0) {
            // Fill up last chunk
            w = (CHUNK_SIZE - w);
            w = (toWrite < w) ? toWrite : w;
            RtlCopyMemory(&(m_pBuffer[offset]), &(pBuffer[ulByteCount - toWrite]), w);
        }
        else {
            // Start a new chunk
            m_pBuffer[offset] = m_bSamplingFreqMarker;
            m_pBuffer[offset + 1] = m_bBitsPerSampleMarker;
            m_pBuffer[offset + 2] = m_bChannels;
            m_pBuffer[offset + 3] = (BYTE)(m_wChannelMask & 0xFF);
            m_pBuffer[offset + 4] = (BYTE)(m_wChannelMask >> 8 & 0xFF);
            offset += HEADER_SIZE;
            w = ((BUFFER_SIZE - offset) < toWrite) ? (BUFFER_SIZE - offset) : toWrite;
            w = (w > PCM_PAYLOAD_SIZE) ? PCM_PAYLOAD_SIZE : w;
            RtlCopyMemory(&(m_pBuffer[offset]), &(pBuffer[ulByteCount - toWrite]), w);
        }
        toWrite -= w;
        offset += w;
        if (offset >= BUFFER_SIZE) offset = 0;
    }
    m_ulOffset = offset;

    // If I/O worker was done, relaunch it
    ntStatus = KeWaitForSingleObject(&(m_pWorkItem->EventDone), Executive, KernelMode, FALSE, &timeOut);
    if (STATUS_SUCCESS == ntStatus) {
        m_pWorkItem->pSaveData = this;
        KeResetEvent(&(m_pWorkItem->EventDone));
        IoQueueWorkItem(m_pWorkItem->WorkItem, SendDataWorkerCallback, CriticalWorkQueue, (PVOID)m_pWorkItem);
    }

exit:
    FuncExitNoReturn(TRACE_SAVEDATA);
} // WriteData
