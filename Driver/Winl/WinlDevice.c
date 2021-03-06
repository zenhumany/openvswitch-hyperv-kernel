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

#include "WinlDevice.h"
#include "WinlFlow.h"
#include "WinlPacket.h"
#include "WinlDatapath.h"
#include "WinlOFPort.h"
#include "Buffer.h"
#include "Message.h"
#include "Winetlink.h"
#include "Error.h"
#include "List.h"
#include "BufferControl.h"
#include "Switch.h"
#include "OFDatapath.h"

#include "MsgVerification.h"

typedef struct _WINL_DEVICE_EXTENSION
{
    // Data structure magic #
    ULONG magicNumber;
    // This is used to indicate the state of the device
    ULONG deviceState;
}WINL_DEVICE_EXTENSION, *PWINL_DEVICE_EXTENSION;

#define WINL_OVS_DEVICE_TYPE    0xB360

static PDEVICE_OBJECT g_pOvsDeviceObject;

// {CEF35472-E7EE-4B4E-AB69-93ED0249377C}
static const GUID g_ovsDeviceGuidName =
{ 0xcef35472, 0xe7ee, 0x4b4e, { 0xab, 0x69, 0x93, 0xed, 0x2, 0x49, 0x37, 0x7c } };

/********************************************************************************************/

//i.e. for userspace to read
OVS_ERROR WriteMsgsToDevice(OVS_NLMSGHDR* pMsgs, int countMsgs, const FILE_OBJECT* pFileObject, UINT groupId)
{
    LOCK_STATE_EX lockState = { 0 };
    OVS_BUFFER buffer = { 0 };
    OVS_ERROR error = OVS_ERROR_NOERROR;

    OVS_CHECK(countMsgs > 0);

#if OVS_VERIFY_WINL_MESSAGES
    OVS_NLMSGHDR* pMsg = pMsgs;

    for (int i = 0; i < countMsgs; ++i)
    {
        if (!VerifyMessage(pMsg, /*request*/ FALSE))
        {
            DEBUGP(LOG_ERROR, "msg not written to device for userspace to read, because it failed verification\n");
            OVS_CHECK(__UNEXPECTED__);
            return OVS_ERROR_INVAL;
        }

        pMsg = AdvanceMessage(pMsg);
    }
#endif

    if (!WriteMsgsToBuffer(pMsgs, countMsgs, &buffer))
    {
        FreeBufferData(&buffer);

        DEBUGP(LOG_ERROR, "msg not written to devuce for userspace to read, because it failed to output to buffer\n");
        return OVS_ERROR_INVAL;
    }

    BufferCtl_LockWrite(&lockState);

    error = BufferCtl_Write_Unsafe(pFileObject, &buffer, pMsgs[0].pid, groupId);
    OVS_CHECK(error == OVS_ERROR_NOERROR);

    BufferCtl_Unlock(&lockState);

    return error;
}

_Use_decl_annotations_
VOID WriteErrorToDevice(const OVS_NLMSGHDR* pOriginalMsg, UINT errorCode, const FILE_OBJECT* pFileObject, UINT groupId)
{
    OVS_MESSAGE_ERROR msg = { 0 };
    OVS_ERROR error = OVS_ERROR_NOERROR;

    msg.length = sizeof(OVS_MESSAGE_ERROR);
    msg.type = OVS_MESSAGE_TARGET_ERROR;
    msg.flags = 0; (UINT16)errorCode;
    msg.sequence = pOriginalMsg->sequence;
    msg.pid = pOriginalMsg->pid;

    msg.error = (int)errorCode;
    msg.originalMsg = *pOriginalMsg;

    error = WriteMsgsToDevice((OVS_NLMSGHDR*)&msg, 1, pFileObject, groupId);
    if (error != OVS_ERROR_NOERROR)
    {
        DEBUGP(LOG_ERROR, "could not write err msg\n");
    }
}

/*******************************/
_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpCreate(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    FILE_OBJECT* pFileObject = NULL;
    BOOLEAN ok = FALSE;
    LOCK_STATE_EX lockState = { 0 };
    NDIS_STATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(pDeviceObject);

    pFileObject = IoGetCurrentIrpStackLocation(pIrp)->FileObject;
    if (!pFileObject)
    {
        DEBUGP(LOG_ERROR, "expected the file to have a handle, it doesn't have!\n");
        status = STATUS_FILE_INVALID;

        goto Cleanup;
    }

    DEBUGP_FILE(LOG_INFO, "create file: %p\n", pFileObject);

    BufferCtl_LockWrite(&lockState);

    ok = BufferCtl_AddDeviceFile_Unsafe(pFileObject);

    BufferCtl_Unlock(&lockState);

    if (!ok)
    {
        DEBUGP(LOG_ERROR, "error adding datapath device buffer\n");
        status = STATUS_INSUFFICIENT_RESOURCES;

        goto Cleanup;
    }

Cleanup:
    pIrp->IoStatus.Status = status;
    pIrp->IoStatus.Information = (status == STATUS_SUCCESS ? FILE_OPENED : 0);
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return status;
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpCleanup(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    UNREFERENCED_PARAMETER(pDeviceObject);

    DEBUGP_FILE(LOG_INFO, "cleanup file: %p\n", IoGetCurrentIrpStackLocation(pIrp)->FileObject);

    pIrp->IoStatus.Status = STATUS_SUCCESS;
    pIrp->IoStatus.Information = 0;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpClose(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    FILE_OBJECT* pFileObject = NULL;
    BOOLEAN ok = FALSE;
    LOCK_STATE_EX lockState = { 0 };
    NDIS_STATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(pDeviceObject);

    pFileObject = IoGetCurrentIrpStackLocation(pIrp)->FileObject;
    if (!pFileObject)
    {
        DEBUGP(LOG_ERROR, "expected the file to have a handle, it doesn't have!\n");
        status = STATUS_FILE_INVALID;

        goto Cleanup;
    }

    DEBUGP_FILE(LOG_INFO, "cleanup file: %p\n", pFileObject);

    BufferCtl_LockWrite(&lockState);

    ok = BufferCtl_RemoveDeviceFile_Unsafe(pFileObject);

    BufferCtl_Unlock(&lockState);

    if (!ok)
    {
        DEBUGP(LOG_ERROR, "error adding datapath device buffer\n");
        status = STATUS_INSUFFICIENT_RESOURCES;

        goto Cleanup;
    }

Cleanup:
    pIrp->IoStatus.Status = status;
    pIrp->IoStatus.Information = 0;

    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return status;
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpControl(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    UNREFERENCED_PARAMETER(pDeviceObject);

    pIrp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    pIrp->IoStatus.Information = 0;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_INVALID_DEVICE_REQUEST;
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpRead(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG userReadBufferLen = 0, bytesRead = 0;
    LOCK_STATE_EX lockState = { 0 };
    FILE_OBJECT* pFileObject = NULL;
    VOID* pReadBuffer = NULL;
#if DBG
    BOOLEAN dbgPrintData = FALSE;
#endif

    UNREFERENCED_PARAMETER(pDeviceObject);

    pFileObject = IoGetCurrentIrpStackLocation(pIrp)->FileObject;

#if DBG
    if (dbgPrintData)
    {
        DbgPrintDeviceFiles();
        DbgPrintUCastBuffers();
        DbgPrintMCastBuffers();
        DbgPrintQueuedBuffers();
    }
#endif

    // Note that length is in the same location for both read and write
    userReadBufferLen = IoGetCurrentIrpStackLocation(pIrp)->Parameters.Read.Length;

    BufferCtl_LockWrite(&lockState);

    //when using direct io, sys buffer is NULL for read and for write
    OVS_CHECK(pDeviceObject->Flags & DO_DIRECT_IO);
    OVS_CHECK(pIrp->AssociatedIrp.SystemBuffer == NULL);

    pReadBuffer = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
    if (pReadBuffer)
    {
        status = BufferCtl_Read_Unsafe(pFileObject, pReadBuffer, userReadBufferLen, &bytesRead);
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        DEBUGP(LOG_ERROR, __FUNCTION__ " MmGetSystemAddressForMdlSafe failed: read buffer is NULL!\n");
    }

    BufferCtl_Unlock(&lockState);

    pIrp->IoStatus.Status = status;
    pIrp->IoStatus.Information = (status == STATUS_SUCCESS ? bytesRead : 0);
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return status;
}

static BYTE* _WriteNlAttributeToBuffer(VOID* pOutBuf, UINT16 type, VOID* payload, ULONG payloadSize)
{
    OVS_NL_ATTRIBUTE nlAttribute = { 0 };
    BYTE* p = pOutBuf;

    nlAttribute.type = type;
    nlAttribute.length = (UINT16)(sizeof(OVS_NL_ATTRIBUTE) + payloadSize);

    RtlCopyMemory(p, &nlAttribute, sizeof(OVS_NL_ATTRIBUTE));
    p = (BYTE*)p + sizeof(OVS_NL_ATTRIBUTE);

    if (payloadSize)
    {
        RtlZeroMemory(p, OVS_SIZE_ALIGNED_4(payloadSize));
        RtlCopyMemory(p, payload, payloadSize);
        p = (BYTE*)p + OVS_SIZE_ALIGNED_4(payloadSize);
    }

    return p;
}

static BYTE* _WriteCtrlMsgToBuffer(VOID* pOutBuf, OVS_MESSAGE_CONTROL* pCtrlMsg)
{
    BYTE* p = (BYTE*)pOutBuf;
    RtlCopyMemory(p, pCtrlMsg, OVS_CTRL_MESSAGE_HEADER_SIZE);
    p = (BYTE*)p + OVS_CTRL_MESSAGE_HEADER_SIZE;

    return p;
}

BOOLEAN _WriteMsgControlToBuffer(_In_ OVS_MESSAGE_CONTROL* pCtrlMsg, OVS_BUFFER* pBuffer, int family)
{
    UINT bufSize = 0;
    VOID* pos = NULL;

    bufSize = OVS_CTRL_MESSAGE_HEADER_SIZE + 7 * sizeof(OVS_NL_ATTRIBUTE) + OVS_SIZE_ALIGNED_4(sizeof(OVS_NL_ATTRIBUTE) + sizeof(UINT16));

    if (!AllocateBuffer(pBuffer, bufSize))
    {
        return FALSE;
    }

    pCtrlMsg->length = bufSize;

    pos = _WriteCtrlMsgToBuffer(pBuffer->p, pCtrlMsg);

    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 1, &family, sizeof(UINT16));
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);
    pos = _WriteNlAttributeToBuffer(pos, 0, NULL, 0);

    pBuffer->size = bufSize;

    return TRUE;
}

//Write Policy Values to the device
BOOLEAN _Parse_Control(VOID* buffer, _Inout_ OVS_MESSAGE_CONTROL* pCtrlMessage, const FILE_OBJECT* pFileObject, UINT groupId)
{
    OVS_MESSAGE_CONTROL* pBufferedMsg = buffer;
    OVS_BUFFER ovs_buffer = { 0 };
    LOCK_STATE_EX lockState = { 0 };
    OVS_ERROR error = OVS_ERROR_NOERROR;

    pCtrlMessage->command = pBufferedMsg->command;
    pCtrlMessage->version = pBufferedMsg->version;
    pCtrlMessage->reserved = pBufferedMsg->reserved;

    if (pCtrlMessage->flags == OVS_MESSAGE_FLAG_REQUEST && pCtrlMessage->command == 3)
    {
        OVS_NL_ATTRIBUTE* pNlAttribute = (OVS_NL_ATTRIBUTE*)((BYTE*)buffer + OVS_CTRL_MESSAGE_HEADER_SIZE);
        OVS_MESSAGE_TARGET_TYPE target = OVS_MESSAGE_TARGET_INVALID;

        VOID* nlAttrData = OVS_NLA_DATA(pNlAttribute);

        if (pNlAttribute->type == 2)
        {
            if (!strncmp(nlAttrData, "ovs_datapath", 12))
            {
                target = OVS_MESSAGE_TARGET_DATAPATH;
            }
            else if (!strncmp(nlAttrData, "ovs_packet", 10))
            {
                target = OVS_MESSAGE_TARGET_PACKET;
            }
            else if (!strncmp(nlAttrData, "ovs_vport", 9))
            {
                target = OVS_MESSAGE_TARGET_PORT;
            }
            else if (!strncmp(nlAttrData, "ovs_flow", 8))
            {
                target = OVS_MESSAGE_TARGET_FLOW;
            }

            if (target != OVS_MESSAGE_TARGET_INVALID)
            {
                pCtrlMessage->flags = 0;
                pCtrlMessage->command = 1;
                pCtrlMessage->version = 2;
            }
            else
            {
                target = OVS_MESSAGE_TARGET_DATAPATH;

                pCtrlMessage->flags = 3;
                pCtrlMessage->command = 1;
                pCtrlMessage->version = 2;
            }

            _WriteMsgControlToBuffer(pCtrlMessage, &ovs_buffer, target);

            BufferCtl_LockWrite(&lockState);
            error = BufferCtl_Write_Unsafe(pFileObject, &ovs_buffer, pCtrlMessage->pid, groupId);
            OVS_CHECK(error == OVS_ERROR_NOERROR);
            BufferCtl_Unlock(&lockState);

            return TRUE;
        }
    }

    return FALSE;
}

static OVS_ERROR _WinlIrpWrite_Datapath(OVS_MESSAGE* pMsg, FILE_OBJECT* pFileObject)
{
    OVS_ERROR error = OVS_ERROR_NOERROR;
    OVS_DATAPATH* pDatapath = NULL;

    CHECK_B_E(pMsg, OVS_ERROR_INVAL);

    pDatapath = GetDefaultDatapath_Ref(__FUNCTION__);
    CHECK_B_E(pDatapath, OVS_ERROR_NODEV);

    switch (pMsg->command)
    {
    case OVS_MESSAGE_COMMAND_NEW:
        //NOTE: we always have one "default" datapath, so when the command NEW arrives,
        //we must set stuff to the existing datapath.
        error = WinlDatapath_New(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_SET:
        error = WinlDatapath_Set(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_GET:
        error = WinlDatapath_Get(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DELETE:
        error = WinlDatapath_Delete(&pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DUMP:
        error = WinlDatapath_Dump(pDatapath, pMsg, pFileObject);
        break;

    default:
        error = OVS_ERROR_INVAL;
        break;
    }

Cleanup:
    OVS_REFCOUNT_DEREFERENCE(pDatapath);

    return error;
}

static OVS_ERROR _WinlIrpWrite_Flow(OVS_MESSAGE* pMsg, FILE_OBJECT* pFileObject)
{
    OVS_ERROR error = OVS_ERROR_NOERROR;
    OVS_DATAPATH* pDatapath = NULL;
    OVS_FLOW_TABLE* pFlowTable = NULL;

    CHECK_B_E(pMsg, OVS_ERROR_INVAL);

    pDatapath = GetDefaultDatapath_Ref(__FUNCTION__);
    CHECK_B_E(pDatapath, OVS_ERROR_NODEV);

    pFlowTable = Datapath_ReferenceFlowTable(pDatapath);
    CHECK_B_E(pFlowTable, OVS_ERROR_INVAL);

    switch (pMsg->command)
    {
    case OVS_MESSAGE_COMMAND_NEW:
        error = WinlFlow_New(pFlowTable, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_SET:
        error = WinlFlow_Set(pFlowTable, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_GET:
        error = WinlFlow_Get(pFlowTable, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DELETE:
        error = WinlFlow_Delete(pDatapath, pFlowTable, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DUMP:
        error = WinlFlow_Dump(pFlowTable, pMsg, pFileObject);
        break;

    default:
        error = OVS_ERROR_INVAL;
        break;
    }

Cleanup:
    OVS_REFCOUNT_DEREFERENCE(pFlowTable);
    OVS_REFCOUNT_DEREFERENCE(pDatapath);

    return error;
}

static OVS_ERROR _WinlIrpWrite_OFPort(OVS_MESSAGE* pMsg, FILE_OBJECT* pFileObject, OVS_SWITCH_INFO* pSwitchInfo)
{
    OVS_ERROR error = OVS_ERROR_NOERROR;
    OVS_DATAPATH* pDatapath = NULL;
    
    CHECK_B_E(pMsg, OVS_ERROR_INVAL);
    CHECK_B_E(pSwitchInfo, OVS_ERROR_INVAL);

    pDatapath = GetDefaultDatapath_Ref(__FUNCTION__);
    CHECK_B_E(pDatapath, OVS_ERROR_NODEV);

    switch (pMsg->command)
    {
    case OVS_MESSAGE_COMMAND_NEW:
        error = WinlOFPort_New(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_GET:
        error = WinlOFPort_Get(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_SET:
        error = WinlOFPort_Set(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DELETE:
        error = WinlOFPort_Delete(pDatapath, pMsg, pFileObject);
        break;

    case OVS_MESSAGE_COMMAND_DUMP:
        error = WinlOFPort_Dump(pSwitchInfo, pDatapath, pMsg, pFileObject);
        break;

    default:
        error = OVS_ERROR_INVAL;
        break;
    }

Cleanup:
    OVS_REFCOUNT_DEREFERENCE(pDatapath);
    return error;
}

static OVS_ERROR _WinlIrpWrite_Packet(OVS_MESSAGE* pMsg, OVS_SWITCH_INFO* pSwitchInfo)
{
    OVS_ERROR error = OVS_ERROR_NOERROR;
    OVS_DATAPATH* pDatapath = NULL;

    CHECK_B_E(pMsg, OVS_ERROR_INVAL);

    switch (pMsg->command)
    {
    case OVS_MESSAGE_COMMAND_PACKET_UPCALL_EXECUTE:
        CHECK_B_E(pMsg->pArgGroup, OVS_ERROR_INVAL);

        pDatapath = GetDefaultDatapath_Ref(__FUNCTION__);
        CHECK_B_E(pDatapath, OVS_ERROR_NODEV);

        WinlPacket_Execute(pSwitchInfo, pDatapath, pMsg->pArgGroup, NULL);

        OVS_REFCOUNT_DEREFERENCE(pDatapath);
        break;

    default:
        error = OVS_ERROR_INVAL;
        break;
    }

Cleanup:
    return error;
}

_Function_class_(DRIVER_DISPATCH)
NTSTATUS _WinlIrpWrite(PDEVICE_OBJECT pDeviceObject, PIRP pIrp)
{
    NTSTATUS status = STATUS_SUCCESS;
    ULONG length = 0;
    FILE_OBJECT* pFileObject = NULL;
    OVS_ERROR error = OVS_ERROR_NOERROR;
    UINT groupId = OVS_MULTICAST_GROUP_NONE;
    OVS_NLMSGHDR* pNlMsg = NULL;
    OVS_MESSAGE* pMsg = NULL;
    VOID* pWriteBuffer = NULL;
    OVS_SWITCH_INFO* pSwitchInfo = NULL;
#if DBG
    BOOLEAN dbgPrintData = FALSE;
#endif

    UNREFERENCED_PARAMETER(pDeviceObject);

    length = IoGetCurrentIrpStackLocation(pIrp)->Parameters.Write.Length;
    pFileObject = IoGetCurrentIrpStackLocation(pIrp)->FileObject;

    if (length > 0xFFFF)
    {
        status = NDIS_STATUS_INVALID_LENGTH;
        goto Cleanup;
    }

    //when using direct io, sys buffer is NULL for read and for write
    OVS_CHECK(pDeviceObject->Flags & DO_DIRECT_IO);
    OVS_CHECK(pIrp->AssociatedIrp.SystemBuffer == NULL);

    pWriteBuffer = MmGetSystemAddressForMdlSafe(pIrp->MdlAddress, NormalPagePriority | MdlMappingNoExecute);
    if (!pWriteBuffer)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        DEBUGP(LOG_ERROR, __FUNCTION__ " MmGetSystemAddressForMdlSafe failed: read buffer is NULL!\n");

        goto Cleanup;
    }

    //messages from here are always OVS_MESSAGE, not done, not error
    if (!ParseReceivedMessage(pWriteBuffer, (UINT16)length, &pNlMsg))
    {
        error = OVS_ERROR_INVAL;
        goto Cleanup;
    }

    OVS_CHECK(pNlMsg);

    if (NlMsgIsGeneric(pNlMsg))
    {
        pMsg = (OVS_MESSAGE*)pNlMsg;

        if (pMsg->pArgGroup)
        {
#if OVS_VERIFY_WINL_MESSAGES
            if (!VerifyMessage(pNlMsg, /*request*/ TRUE))
            {
                error = OVS_ERROR_INVAL;

                DEBUGP(LOG_ERROR, "msg read from userspace not processed, because it failed verification\n");
                OVS_CHECK(__UNEXPECTED__);

                goto Cleanup;
            }
#endif
        }

        if (pMsg->flags & OVS_MESSAGE_FLAG_DUMP)
        {
            pMsg->command = OVS_MESSAGE_COMMAND_DUMP;
        }
    }

#if DBG
    if (dbgPrintData)
    {
        DbgPrintDeviceFiles();
        DbgPrintUCastBuffers();
        DbgPrintMCastBuffers();
        DbgPrintQueuedBuffers();
    }
#endif

    pSwitchInfo = Driver_GetDefaultSwitch_Ref(__FUNCTION__);
    if (!pSwitchInfo)
    {
        DEBUGP(LOG_ERROR, __FUNCTION__ " hyper-v extension not enabled!n");
        error = OVS_ERROR_PERM;
        goto Cleanup;
    }

    switch (pNlMsg->type)
    {
    case OVS_MESSAGE_TARGET_CONTROL:
    {
        OVS_MESSAGE_CONTROL* pCtrlMsg = (OVS_MESSAGE_CONTROL*)pNlMsg;

        OVS_CHECK(pCtrlMsg);
        if (!pCtrlMsg)
        {
            error = OVS_ERROR_INVAL;
            goto Cleanup;
        }

        _Parse_Control(pWriteBuffer, pCtrlMsg, pFileObject, groupId);
        DEBUGP_FILE(LOG_INFO, __FUNCTION__ " target = OVS_NETLINK_GENERIC\n");
    }
        break;

    case OVS_MESSAGE_TARGET_RTM_GETROUTE:
        error = OVS_ERROR_PERM;
        DEBUGP(LOG_WARN, __FUNCTION__ " we don't have routing implemented!\n");
        break;

    case OVS_MESSAGE_TARGET_SET_FILE_PID:
    {
        OVS_MESSAGE_SET_FILE_PID* pSetPidMsg = (OVS_MESSAGE_SET_FILE_PID*)pNlMsg;

        DEBUGP_FILE(LOG_INFO, "setting pid=%u for file=%p\n", pSetPidMsg->pid, pFileObject);
        BufferCtl_SetPidForFile(pSetPidMsg->pid, pFileObject);
    }
        break;

    case OVS_MESSAGE_TARGET_MULTICAST:
    {
        OVS_MESSAGE_MULTICAST* pMulticastMsg = (OVS_MESSAGE_MULTICAST*)pNlMsg;

        McGroup_Change(pMulticastMsg, pFileObject);
    }
        break;

    case OVS_MESSAGE_TARGET_DATAPATH:
        error = _WinlIrpWrite_Datapath(pMsg, pFileObject);
        break;

    case OVS_MESSAGE_TARGET_PORT:
        groupId = OVS_VPORT_MCGROUP;
        error = _WinlIrpWrite_OFPort(pMsg, pFileObject, pSwitchInfo);
        break;

    case OVS_MESSAGE_TARGET_FLOW:
        error = _WinlIrpWrite_Flow(pMsg, pFileObject);
        break;

    case OVS_MESSAGE_TARGET_PACKET:
        error = _WinlIrpWrite_Packet(pMsg, pSwitchInfo);
            break;

    default: OVS_CHECK(0);
    }

Cleanup:
    OVS_REFCOUNT_DEREFERENCE(pSwitchInfo);

    if (pNlMsg)
    {
        if (error != OVS_ERROR_NOERROR)
        {
            WriteErrorToDevice(pNlMsg, error, pFileObject, groupId);
        }

        if (pMsg)
        {
            if (pMsg->type != OVS_MESSAGE_TARGET_CONTROL)
            {
                DestroyArgumentGroup(pMsg->pArgGroup);
            }
        }

        KFree(pNlMsg);
        pNlMsg = NULL;
        pMsg = NULL;
    }

    pIrp->IoStatus.Status = status;
    pIrp->IoStatus.Information = (status == STATUS_SUCCESS ? length : 0);

    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return status;
}

static NTSTATUS _WinlCreateOneDevice(_In_ PDRIVER_OBJECT pDriverObject, UINT type, _Out_ PDEVICE_OBJECT* ppDeviceObj,
    const WCHAR* wsDeviceName, const WCHAR* wsSymbolicName, _In_ const GUID* pGuid)
{
    UNICODE_STRING deviceName = { 0 };
    UNICODE_STRING symbolicName = { 0 };
    UNICODE_STRING sddlString = { 0 };
    NTSTATUS status = 0;
    UNREFERENCED_PARAMETER(pGuid);
    UNREFERENCED_PARAMETER(sddlString);

    NdisInitUnicodeString(&deviceName, wsDeviceName);
    NdisInitUnicodeString(&sddlString, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;WD)(A;;GA;;;NU)");
    DEBUGP_FILE(LOG_INFO, "Create device %wZ\n", &deviceName);

    // Now we can create the control device object
    status = IoCreateDevice(pDriverObject, sizeof(WINL_DEVICE_EXTENSION), &deviceName, type,
        FILE_DEVICE_SECURE_OPEN, FALSE, ppDeviceObj);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    (*ppDeviceObj)->Flags |= DO_DIRECT_IO;

    NdisInitUnicodeString(&symbolicName, wsSymbolicName);
    status = IoCreateSymbolicLink(&symbolicName, &deviceName);

    if (!NT_SUCCESS(status))
    {
        IoDeleteDevice(*ppDeviceObj);
        *ppDeviceObj = NULL;
    }

    return status;
}

NTSTATUS WinlCreateDevices(PDRIVER_OBJECT pDriverObject, NDIS_HANDLE ndishandle)
{
    NTSTATUS status = 0;

    UNREFERENCED_PARAMETER(ndishandle);

    BufferCtl_Init(ndishandle);

    status = _WinlCreateOneDevice(pDriverObject, WINL_OVS_DEVICE_TYPE, &g_pOvsDeviceObject,
        L"\\Device\\OpenVSwitchDevice", L"\\DosDevices\\OpenVSwitchDevice", &g_ovsDeviceGuidName);

    if (!NT_SUCCESS(status))
    {
        return status;
    }

    // Now, set up entry points
    pDriverObject->MajorFunction[IRP_MJ_CREATE] = _WinlIrpCreate;
    pDriverObject->MajorFunction[IRP_MJ_CLEANUP] = _WinlIrpCleanup;
    pDriverObject->MajorFunction[IRP_MJ_CLOSE] = _WinlIrpClose;
    pDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = _WinlIrpControl;
    pDriverObject->MajorFunction[IRP_MJ_READ] = _WinlIrpRead;
    pDriverObject->MajorFunction[IRP_MJ_WRITE] = _WinlIrpWrite;

    return STATUS_SUCCESS;
}

VOID WinlDeleteDevices()
{
    UNICODE_STRING symbolicName = { 0 };
    NTSTATUS status = 0;

    if (g_pOvsDeviceObject)
    {
        NdisInitUnicodeString(&symbolicName, L"\\DosDevices\\OpenVSwitchDevice");
        status = IoDeleteSymbolicLink(&symbolicName);
        if (status != STATUS_SUCCESS)
        {
            DEBUGP(LOG_ERROR, "failed deleting dos device Packet: 0x%x", status);
        }

        IoDeleteDevice(g_pOvsDeviceObject);
    }

    BufferCtl_Uninit();
}