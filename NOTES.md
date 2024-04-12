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
              └ CSaveData
```
