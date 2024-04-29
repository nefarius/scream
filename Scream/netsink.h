#pragma once

#pragma warning(push)
#pragma warning(disable:4201) // nameless struct/union
#pragma warning(disable:4214) // bit field types other than int

// fix strange warnings from wsk.h
#pragma warning(disable:4510)
#pragma warning(disable:4512)
#pragma warning(disable:4610)

#include <ntddk.h>
#include <wsk.h>
#include "settings.h"

#pragma warning(pop)

class CNetSink;
typedef CNetSink *PCNetSink;

// Parameter to workitem.
#include <pshpack1.h>
typedef struct _SAVEWORKER_PARAM {
    PIO_WORKITEM     WorkItem;
    PCNetSink       pSaveData;
    KEVENT           EventDone;
} SAVEWORKER_PARAM;
typedef SAVEWORKER_PARAM *PSAVEWORKER_PARAM;
#include <poppack.h>


IO_WORKITEM_ROUTINE SendDataWorkerCallback;

class CNetSink {
protected:
    WSK_REGISTRATION            m_wskRegistration;
    PWSK_SOCKET                 m_socket;
    PIRP                        m_irp;
    KEVENT                      m_syncEvent;
    
    PBYTE                       m_pBuffer;
    ULONG                       m_ulOffset;
    ULONG                       m_ulSendOffset;
    PMDL                        m_pMdl;

    // TODO: convert to member variable
    __declspec(deprecated("Move to instance member variable"))
    static PDEVICE_OBJECT       m_pDeviceObject;
    __declspec(deprecated("Move to instance member variable"))
    static PSAVEWORKER_PARAM    m_pWorkItem;

    BOOL                        m_fWriteDisabled;

    BYTE                        m_bSamplingFreqMarker;
    BYTE                        m_bBitsPerSampleMarker;
    BYTE                        m_bChannels;
    WORD                        m_wChannelMask;

    CONST ADAPTER_COMMON_SETTINGS*  m_pAdapterSettings;

public:
    CNetSink();
    ~CNetSink();

    void SetAdapterSettings(CONST ADAPTER_COMMON_SETTINGS* Settings);
    NTSTATUS                    Initialize(DWORD nSamplesPerSec, WORD wBitsPerSample, WORD nChannels, DWORD dwChannelMask);
    void                        Disable(BOOL fDisable);

    __declspec(deprecated("Move to instance member function"))
    static void                 DestroyWorkItems(void);
    void                        WaitAllWorkItems(void);

    // TODO: convert to member function
    __declspec(deprecated("Move to instance member function"))
    static NTSTATUS             SetDeviceObject(IN PDEVICE_OBJECT DeviceObject);
    __declspec(deprecated("Move to instance member function"))
    static PDEVICE_OBJECT       GetDeviceObject(void);
    
    void                        WriteData(IN PBYTE pBuffer, IN ULONG ulByteCount);

private:
    // TODO: convert to member function
    __declspec(deprecated("Move to instance member function"))
    static NTSTATUS             InitializeWorkItem(IN PDEVICE_OBJECT DeviceObject);

    _IRQL_requires_max_(PASSIVE_LEVEL)
    void                        CreateSocket(void);
    _IRQL_requires_max_(PASSIVE_LEVEL)
    void                        SendData();
    friend VOID                 SendDataWorkerCallback(PDEVICE_OBJECT pDeviceObject, IN PVOID Context);
};
typedef CNetSink *PCNetSink;
