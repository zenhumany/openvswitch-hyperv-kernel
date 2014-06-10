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

#include "Sctx_Port.h"
#include "Switch.h"

OVS_PORT_LIST_ENTRY* Sctx_FindPortById_Unsafe(_In_ const OVS_GLOBAL_FORWARD_INFO* pForwardIno, _In_ NDIS_SWITCH_PORT_ID portId)
{
    const LIST_ENTRY* pPortList = &pForwardIno->portList;
    const LIST_ENTRY* pCurEntry = pPortList->Flink;
    const OVS_PORT_LIST_ENTRY* pPortEntry = NULL;

    if (IsListEmpty(pPortList))
    {
        goto Cleanup;
    }

    do {
        pPortEntry = CONTAINING_RECORD(pCurEntry, OVS_PORT_LIST_ENTRY, listEntry);

        if (pPortEntry->portId == portId)
        {
            goto Cleanup;
        }

        pCurEntry = pCurEntry->Flink;
    } while (pCurEntry != pPortList);

    pPortEntry = NULL;

Cleanup:
    return (OVS_PORT_LIST_ENTRY*)pPortEntry;
}

NDIS_STATUS Sctx_AddPort_Unsafe(_Inout_ OVS_GLOBAL_FORWARD_INFO* pForwardInfo, const NDIS_SWITCH_PORT_PARAMETERS* pCurPort, _Inout_opt_ OVS_PORT_LIST_ENTRY** ppPortEntry)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;
    OVS_PORT_LIST_ENTRY* pPortEntry = NULL;
    LIST_ENTRY* pPortList = &pForwardInfo->portList;
    char* ofPortName = NULL;

    pPortEntry = Sctx_FindPortById_Unsafe(pForwardInfo, pCurPort->PortId);
    if (pPortEntry)
    {
        return status;
    }

    pPortEntry = KAlloc(sizeof(OVS_PORT_LIST_ENTRY));

    if (pPortEntry == NULL)
    {
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    NdisZeroMemory(pPortEntry, sizeof(OVS_PORT_LIST_ENTRY));

    pPortEntry->portId = pCurPort->PortId;
    pPortEntry->portType = pCurPort->PortType;
    pPortEntry->on = (pCurPort->PortState == NdisSwitchPortStateCreated);
    pPortEntry->portFriendlyName = pCurPort->PortFriendlyName;
    pPortEntry->pPersistentPort = NULL;

    DEBUGP(LOG_INFO, "PORT: id=%d; type=%d; on=%d; friendly name=\"%s\"\n",
        pPortEntry->portId, pPortEntry->portType, pPortEntry->on, ofPortName);

    InsertHeadList(pPortList, &pPortEntry->listEntry);

    if (ppPortEntry)
    {
        *ppPortEntry = pPortEntry;
    }

Cleanup:
    if (ofPortName)
    {
        KFree(ofPortName);
    }

    return status;
}

NDIS_STATUS Sctx_DeletePort_Unsafe(_In_ const OVS_GLOBAL_FORWARD_INFO* pForwardInfo, _In_ NDIS_SWITCH_PORT_ID portId)
{
    NDIS_STATUS status = NDIS_STATUS_SUCCESS;

    OVS_PORT_LIST_ENTRY* pPortEntry = Sctx_FindPortById_Unsafe(pForwardInfo, portId);

    if (pPortEntry == NULL)
    {
        OVS_CHECK(FALSE);
        goto Cleanup;
    }

    OVS_CHECK(!pPortEntry->pPersistentPort);

    RemoveEntryList(&pPortEntry->listEntry);
    KFree(pPortEntry);

Cleanup:
    return status;
}

OVS_PORT_LIST_ENTRY* Sctx_FindPortBy_Unsafe(_In_ OVS_GLOBAL_FORWARD_INFO* pForwardInfo, const VOID* pContext, BOOLEAN(*Predicate)(int, const VOID*, _In_ const OVS_PORT_LIST_ENTRY*))
{
    LIST_ENTRY* pPortList = &pForwardInfo->portList;
    LIST_ENTRY* pCurEntry = pPortList->Flink;
    OVS_PORT_LIST_ENTRY* pPortEntry = NULL;
    int i = 0;

    if (IsListEmpty(pPortList))
    {
        goto Cleanup;
    }

    do {
        pPortEntry = CONTAINING_RECORD(pCurEntry, OVS_PORT_LIST_ENTRY, listEntry);

        if ((*Predicate)(i, pContext, pPortEntry))
        {
            return pPortEntry;
        }

        pCurEntry = pCurEntry->Flink;

        ++i;
    } while (pCurEntry != pPortList);

    pPortEntry = NULL;

Cleanup:

    return NULL;
}

VOID Sctx_Port_SetPersistentPort_Unsafe(_Inout_ OVS_PORT_LIST_ENTRY* pPortEntry)
{
    char* ovsPortName = IfCountedStringToCharArray(&pPortEntry->portFriendlyName);

    pPortEntry->pPersistentPort = PersPort_FindByName_Unsafe(ovsPortName);
    if (pPortEntry->pPersistentPort)
    {
        pPortEntry->pPersistentPort->pPortListEntry = pPortEntry;
    }

    KFree(ovsPortName);
}

VOID Sctx_Port_UnsetPersistentPort_Unsafe(_Inout_ OVS_PORT_LIST_ENTRY* pPortEntry)
{
    if (pPortEntry->pPersistentPort)
    {
        pPortEntry->pPersistentPort->pPortListEntry = NULL;
        pPortEntry->pPersistentPort = NULL;
    }
}

VOID Sctx_Port_Disable_Unsafe(_Inout_ OVS_GLOBAL_FORWARD_INFO* pForwardInfo, _Inout_ OVS_PORT_LIST_ENTRY* pPortEntry)
{
    --(pForwardInfo->countPorts);
    pPortEntry->on = FALSE;

    Sctx_Port_UnsetPersistentPort_Unsafe(pPortEntry);
}

VOID Sctx_Port_UpdateName_Unsafe(_Inout_ OVS_PORT_LIST_ENTRY* pPortEntry, _In_ const IF_COUNTED_STRING* pNewName)
{
    OVS_CHECK(pNewName);

    pPortEntry->portFriendlyName = *pNewName;

    if (pPortEntry->pPersistentPort)
    {
        if (pPortEntry->pPersistentPort->ovsPortName)
        {
            KFree(pPortEntry->pPersistentPort->ovsPortName);
            pPortEntry->pPersistentPort->ovsPortName = NULL;
        }

        pPortEntry->pPersistentPort->ovsPortName = IfCountedStringToCharArray(pNewName);
    }
}