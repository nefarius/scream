#ifndef _MSVAD_H_
#define _MSVAD_H_

#include <portcls.h>
#pragma warning(disable:4595)
#include <stdunk.h>
#include <ksdebug.h>
#include "kshelper.h"
#include "Trace.h"
#include <screamETW.h>

//=============================================================================
// Defines
//=============================================================================

// Name Guid
DEFINE_GUIDSTRUCT("03F51379-5C7E-453D-847D-E4DA9D2F39C2", NAME_SCREAM);
#define SCREAM_NAME DEFINE_GUIDNAMED(NAME_SCREAM)

// Version number. Revision numbers are specified for each sample.
#define SCREAM_VERSION               1

// Revision number.
#define SCREAM_REVISION              0

// Product Id
DEFINE_GUIDSTRUCT("8CA27A4B-23F2-4BDE-92B3-A13E563C8506", PID_SCREAM);
#define SCREAM_PRODUCT DEFINE_GUIDNAMED(PID_SCREAM)

// Pool tag used for Scream driver allocations
#define SCREAM_POOLTAG               'ercS'

// Debug module name
#define STR_MODULENAME              "Scream: "

// Debug utility macros
#define D_FUNC                      4
#define D_BLAB                      DEBUGLVL_BLAB
#define D_VERBOSE                   DEBUGLVL_VERBOSE        
#define D_TERSE                     DEBUGLVL_TERSE          
#define D_ERROR                     DEBUGLVL_ERROR          
#define DPF                         _DbgPrintF

// Channel orientation
#define CHAN_LEFT                   0
#define CHAN_RIGHT                  1
#define CHAN_MASTER                 (-1)

// Dma Settings.
#define DMA_BUFFER_SIZE             0x16000

#define KSPROPERTY_TYPE_ALL         KSPROPERTY_TYPE_BASICSUPPORT | KSPROPERTY_TYPE_GET | KSPROPERTY_TYPE_SET

// Pin properties.
#define MAX_OUTPUT_STREAMS          0       // Number of capture streams.
#define MAX_INPUT_STREAMS           1       // Number of render streams.
#define MAX_TOTAL_STREAMS           MAX_OUTPUT_STREAMS + MAX_INPUT_STREAMS

// PCM Info
#define MIN_CHANNELS                1        // Min Channels.
#define MAX_CHANNELS_PCM            8        // Max Channels.
#define MIN_BITS_PER_SAMPLE_PCM     16       // Min Bits Per Sample
#define MAX_BITS_PER_SAMPLE_PCM     32       // Max Bits Per Sample
#define MIN_SAMPLE_RATE             44100    // Min Sample Rate
#define MAX_SAMPLE_RATE             192000   // Max Sample Rate

#define DEV_SPECIFIC_VT_BOOL        9
#define DEV_SPECIFIC_VT_I4          10
#define DEV_SPECIFIC_VT_UI4         11

//=============================================================================
// Typedefs
//=============================================================================

// Connection table for registering topology/wave bridge connection
typedef struct _PHYSICALCONNECTIONTABLE {
    ULONG       ulTopologyIn;
    ULONG       ulTopologyOut;
    ULONG       ulWaveIn;
    ULONG       ulWaveOut;
} PHYSICALCONNECTIONTABLE, *PPHYSICALCONNECTIONTABLE;

//=============================================================================
// Enums
//=============================================================================

// Wave pins
enum {
    KSPIN_WAVE_RENDER_SINK = 0,
    KSPIN_WAVE_RENDER_SOURCE
};

// Wave Topology nodes.
enum {
    KSNODE_WAVE_DAC = 0
};

// topology pins.
enum {
    KSPIN_TOPO_WAVEOUT_SOURCE = 0,
    KSPIN_TOPO_LINEOUT_DEST
};

// topology nodes.
enum {
    KSNODE_TOPO_WAVEOUT_VOLUME = 0,
    KSNODE_TOPO_WAVEOUT_MUTE,
    KSNODE_TOPO_LINEOUT_MIX,
    KSNODE_TOPO_LINEOUT_VOLUME
};

//=============================================================================
// Externs
//=============================================================================

// Physical connection table. Defined in mintopo.cpp for each sample
extern PHYSICALCONNECTIONTABLE TopologyPhysicalConnections;

// Generic topology handler
extern NTSTATUS PropertyHandler_Topology(IN PPCPROPERTY_REQUEST PropertyRequest);

// Generic wave port handler
extern NTSTATUS PropertyHandler_Wave(IN PPCPROPERTY_REQUEST PropertyRequest);

// Default WaveFilter automation table.
// Handles the GeneralComponentId request.
extern NTSTATUS PropertyHandler_WaveFilter(IN PPCPROPERTY_REQUEST PropertyRequest);


#define MAX_DEVICES 255

//
// TODO: get rid of all global variables for settings
// 

extern PCHAR g_UnicastIPv4;
extern DWORD g_UnicastPort;
extern UINT8 g_UseIVSHMEM;
extern PCHAR g_UnicastSrcIPv4;
extern DWORD g_UnicastSrcPort;
extern DWORD g_DSCP;
extern DWORD g_TTL;
extern DWORD g_ScreamVersion;
extern DWORD g_silenceThreshold;

#endif
