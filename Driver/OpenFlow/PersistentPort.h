/*
Copyright 2014 Cloudbase Solutions Srl

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http ://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#pragma once

#include "precomp.h"
#include "OFPort.h"

#define OVS_LOCAL_PORT_NUMBER			((UINT32)0)
#define OVS_MAX_PORTS					MAXUINT16
#define OVS_INVALID_PORT_NUMBER			OVS_MAX_PORTS

typedef struct _OVS_NIC_LIST_ENTRY OVS_NIC_LIST_ENTRY;
typedef struct _OVS_PORT_LIST_ENTRY OVS_PORT_LIST_ENTRY;

typedef struct _OVS_TUNNELING_PORT_OPTIONS OVS_TUNNELING_PORT_OPTIONS;
typedef struct _OVS_SWITCH_INFO OVS_SWITCH_INFO;

typedef struct _OVS_PERSISTENT_PORT {
    //port number assigned by OVS (userspace, or computed in driver)
    UINT16				ovsPortNumber;

    //port name assigned by OVS (userspace, or computed in driver)
    char*			ovsPortName;

    //OpenFlow / ovs port type
    OVS_OFPORT_TYPE		ofPortType;
    OVS_OFPORT_STATS	stats;
    UINT32				upcallPortId;

    OVS_TUNNELING_PORT_OPTIONS*	pOptions;
    OVS_SWITCH_INFO*			pSwitchInfo;

    //NDIS / Hyper-V: NULL if disconnected from hyper-v
    OVS_NIC_LIST_ENTRY*		pNicListEntry;
    OVS_PORT_LIST_ENTRY*	pPortListEntry;
}OVS_PERSISTENT_PORT;

typedef struct _OVS_LOGICAL_PORT_ENTRY {
    LIST_ENTRY listEntry;
    OVS_PERSISTENT_PORT* pPort;
}OVS_LOGICAL_PORT_ENTRY;

typedef struct _OVS_PERSISTENT_PORTS_INFO {
    OVS_PERSISTENT_PORT* portsArray[OVS_MAX_PORTS];
    UINT16 count;
    UINT16 firstPortFree;
}OVS_PERSISTENT_PORTS_INFO;

typedef struct _OF_PI_IPV4_TUNNEL OF_PI_IPV4_TUNNEL;

//i.e. internal or external
BOOLEAN PersPort_CreateInternalPort_Unsafe(const char* name, UINT32 upcallPortId, NDIS_SWITCH_PORT_TYPE portType);

OVS_PERSISTENT_PORT* PersPort_Create_Unsafe(_In_opt_ const char* portName, _In_opt_ const UINT16* pPortNumber, OVS_OFPORT_TYPE portType);

BOOLEAN PersPort_CForEach_Unsafe(_In_ const OVS_PERSISTENT_PORTS_INFO* pPorts, VOID* pContext, BOOLEAN(*Action)(int, OVS_PERSISTENT_PORT*, VOID*));

OVS_PERSISTENT_PORT* PersPort_FindByName_Unsafe(const char* ofPortName);
OVS_PERSISTENT_PORT* PersPort_FindByNumber_Unsafe(UINT16 portNumber);

OVS_PERSISTENT_PORT* PersPort_FindById_Unsafe(NDIS_SWITCH_PORT_ID portId, BOOLEAN lookInNic);

OVS_PERSISTENT_PORT* PersPort_GetInternal_Unsafe();
BOOLEAN PersPort_Delete_Unsafe(OVS_PERSISTENT_PORT* pPersPort);

_Ret_maybenull_
OVS_PERSISTENT_PORT* PersPort_FindExternal_Unsafe();

_Ret_maybenull_
OVS_PERSISTENT_PORT* PersPort_FindInternal_Unsafe();

_Ret_maybenull_
OVS_PERSISTENT_PORT* PersPort_FindGre(const OVS_TUNNELING_PORT_OPTIONS* pTunnelInfo);
_Ret_maybenull_
OVS_PERSISTENT_PORT* PersPort_FindVxlan(_In_ const OVS_TUNNELING_PORT_OPTIONS* pTunnelInfo);
_Ret_maybenull_
OVS_PERSISTENT_PORT* PersPort_FindVxlanByDestPort(LE16 udpDestPort);

BOOLEAN PersPort_Initialize();
VOID PersPort_Uninitialize();

BOOLEAN PersPort_HaveInternal_Unsafe();