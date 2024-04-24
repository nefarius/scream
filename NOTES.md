# Notes

## Architecture

```text
--- functions and callbacks ---
DriverEntry
└ PcInitializeAdapterDriver
  └ AddDevice
    └ PcAddAdapterDevice
      └ StartDevice
--- object creations and nesting ---
        └ CAdapterCommon
          └ CMiniportWaveCyclic
            └ CMiniportWaveCyclicStream
              └ CNetSink
```

```c
NTSTATUS CMiniportWaveCyclicStream::Init( 
    IN PCMiniportWaveCyclic         Miniport_,
    IN ULONG                        Pin_,
    IN BOOLEAN                      Capture_,
    IN PKSDATAFORMAT                DataFormat_
)
{
    ...

    // get access to adapter settings
    const ADAPTER_COMMON_SETTINGS* pSettings = Miniport_->m_AdapterCommon->GetAdapterSettings();
```
