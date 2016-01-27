/**
 * @file sys/file.c
 *
 * @copyright 2015 Bill Zissimopoulos
 */

#include <sys/driver.h>

NTSTATUS FspFileNodeCreate(PDEVICE_OBJECT DeviceObject,
    ULONG ExtraSize, FSP_FILE_NODE **PFileNode);
VOID FspFileNodeDelete(FSP_FILE_NODE *FileNode);
VOID FspFileNodeAcquireSharedF(FSP_FILE_NODE *FileNode, ULONG Flags);
BOOLEAN FspFileNodeTryAcquireSharedF(FSP_FILE_NODE *FileNode, ULONG Flags);
VOID FspFileNodeAcquireExclusiveF(FSP_FILE_NODE *FileNode, ULONG Flags);
BOOLEAN FspFileNodeTryAcquireExclusiveF(FSP_FILE_NODE *FileNode, ULONG Flags);
VOID FspFileNodeReleaseF(FSP_FILE_NODE *FileNode, ULONG Flags);
FSP_FILE_NODE *FspFileNodeOpen(FSP_FILE_NODE *FileNode, PFILE_OBJECT FileObject,
    UINT32 GrantedAccess, UINT32 ShareAccess, BOOLEAN DeleteOnClose, NTSTATUS *PResult);
VOID FspFileNodeClose(FSP_FILE_NODE *FileNode, PFILE_OBJECT FileObject,
    PBOOLEAN PDeletePending);
VOID FspFileNodeGetFileInfo(FSP_FILE_NODE *FileNode, FSP_FSCTL_FILE_INFO *FileInfo);
BOOLEAN FspFileNodeTryGetFileInfo(FSP_FILE_NODE *FileNode, FSP_FSCTL_FILE_INFO *FileInfo);
VOID FspFileNodeSetFileInfo(FSP_FILE_NODE *FileNode, const FSP_FSCTL_FILE_INFO *FileInfo);
NTSTATUS FspFileDescCreate(FSP_FILE_DESC **PFileDesc);
VOID FspFileDescDelete(FSP_FILE_DESC *FileDesc);
VOID FspFileObjectSetSizes(PFILE_OBJECT FileObject,
    UINT64 AllocationSize, UINT64 FileSize);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, FspFileNodeCreate)
#pragma alloc_text(PAGE, FspFileNodeDelete)
#pragma alloc_text(PAGE, FspFileNodeAcquireSharedF)
#pragma alloc_text(PAGE, FspFileNodeTryAcquireSharedF)
#pragma alloc_text(PAGE, FspFileNodeAcquireExclusiveF)
#pragma alloc_text(PAGE, FspFileNodeTryAcquireExclusiveF)
#pragma alloc_text(PAGE, FspFileNodeReleaseF)
#pragma alloc_text(PAGE, FspFileNodeOpen)
#pragma alloc_text(PAGE, FspFileNodeClose)
#pragma alloc_text(PAGE, FspFileNodeGetFileInfo)
#pragma alloc_text(PAGE, FspFileNodeTryGetFileInfo)
#pragma alloc_text(PAGE, FspFileNodeSetFileInfo)
#pragma alloc_text(PAGE, FspFileDescCreate)
#pragma alloc_text(PAGE, FspFileDescDelete)
#pragma alloc_text(PAGE, FspFileObjectSetSizes)
#endif

NTSTATUS FspFileNodeCreate(PDEVICE_OBJECT DeviceObject,
    ULONG ExtraSize, FSP_FILE_NODE **PFileNode)
{
    PAGED_CODE();

    *PFileNode = 0;

    FSP_FILE_NODE_NONPAGED *NonPaged = FspAllocNonPaged(sizeof *NonPaged);
    if (0 == NonPaged)
        return STATUS_INSUFFICIENT_RESOURCES;

    FSP_FILE_NODE *FileNode = FspAlloc(sizeof *FileNode + ExtraSize);
    if (0 == FileNode)
    {
        FspFree(NonPaged);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NonPaged, sizeof *NonPaged);
    ExInitializeResourceLite(&NonPaged->Resource);
    ExInitializeResourceLite(&NonPaged->PagingIoResource);
    ExInitializeFastMutex(&NonPaged->HeaderFastMutex);
    KeInitializeSpinLock(&NonPaged->InfoSpinLock);

    RtlZeroMemory(FileNode, sizeof *FileNode + ExtraSize);
    FileNode->Header.NodeTypeCode = FspFileNodeFileKind;
    FileNode->Header.NodeByteSize = sizeof *FileNode;
    FileNode->Header.IsFastIoPossible = FastIoIsQuestionable;
    FileNode->Header.Resource = &NonPaged->Resource;
    FileNode->Header.PagingIoResource = &NonPaged->PagingIoResource;
    FileNode->Header.ValidDataLength.QuadPart = MAXLONGLONG;
        /* disable ValidDataLength functionality */
    FsRtlSetupAdvancedHeader(&FileNode->Header, &NonPaged->HeaderFastMutex);
    FileNode->NonPaged = NonPaged;
    FileNode->RefCount = 1;
    FileNode->FsvolDeviceObject = DeviceObject;
    FspDeviceReference(FileNode->FsvolDeviceObject);
    RtlInitEmptyUnicodeString(&FileNode->FileName, FileNode->FileNameBuf, (USHORT)ExtraSize);

    *PFileNode = FileNode;

    return STATUS_SUCCESS;
}

VOID FspFileNodeDelete(FSP_FILE_NODE *FileNode)
{
    PAGED_CODE();

    FsRtlTeardownPerStreamContexts(&FileNode->Header);

    FspDeviceDereference(FileNode->FsvolDeviceObject);

    ExDeleteResourceLite(&FileNode->NonPaged->PagingIoResource);
    ExDeleteResourceLite(&FileNode->NonPaged->Resource);
    FspFree(FileNode->NonPaged);

    FspFree(FileNode);
}

VOID FspFileNodeAcquireSharedF(FSP_FILE_NODE *FileNode, ULONG Flags)
{
    PAGED_CODE();

    if (Flags & FspFileNodeAcquireMain)
        ExAcquireResourceSharedLite(FileNode->Header.Resource, TRUE);

    if (Flags & FspFileNodeAcquirePgio)
        ExAcquireResourceSharedLite(FileNode->Header.PagingIoResource, TRUE);
}

BOOLEAN FspFileNodeTryAcquireSharedF(FSP_FILE_NODE *FileNode, ULONG Flags)
{
    PAGED_CODE();

    BOOLEAN Result = FALSE;

    if (Flags & FspFileNodeAcquireMain)
    {
        Result = ExAcquireResourceSharedLite(FileNode->Header.Resource, FALSE);
        if (!Result)
            return FALSE;
    }

    if (Flags & FspFileNodeAcquirePgio)
    {
        Result = ExAcquireResourceSharedLite(FileNode->Header.PagingIoResource, FALSE);
        if (!Result)
        {
            if (Flags & FspFileNodeAcquireMain)
                ExReleaseResourceLite(FileNode->Header.Resource);
            return FALSE;
        }
    }

    return Result;
}

VOID FspFileNodeAcquireExclusiveF(FSP_FILE_NODE *FileNode, ULONG Flags)
{
    PAGED_CODE();

    if (Flags & FspFileNodeAcquireMain)
        ExAcquireResourceExclusiveLite(FileNode->Header.Resource, TRUE);

    if (Flags & FspFileNodeAcquirePgio)
        ExAcquireResourceExclusiveLite(FileNode->Header.PagingIoResource, TRUE);
}

BOOLEAN FspFileNodeTryAcquireExclusiveF(FSP_FILE_NODE *FileNode, ULONG Flags)
{
    PAGED_CODE();

    BOOLEAN Result = FALSE;

    if (Flags & FspFileNodeAcquireMain)
    {
        Result = ExAcquireResourceExclusiveLite(FileNode->Header.Resource, FALSE);
        if (!Result)
            return FALSE;
    }

    if (Flags & FspFileNodeAcquirePgio)
    {
        Result = ExAcquireResourceExclusiveLite(FileNode->Header.PagingIoResource, FALSE);
        if (!Result)
        {
            if (Flags & FspFileNodeAcquireMain)
                ExReleaseResourceLite(FileNode->Header.Resource);
            return FALSE;
        }
    }

    return Result;
}

VOID FspFileNodeReleaseF(FSP_FILE_NODE *FileNode, ULONG Flags)
{
    PAGED_CODE();

    if (Flags & FspFileNodeAcquirePgio)
        ExReleaseResourceLite(FileNode->Header.PagingIoResource);

    if (Flags & FspFileNodeAcquireMain)
        ExReleaseResourceLite(FileNode->Header.Resource);
}

FSP_FILE_NODE *FspFileNodeOpen(FSP_FILE_NODE *FileNode, PFILE_OBJECT FileObject,
    UINT32 GrantedAccess, UINT32 ShareAccess, BOOLEAN DeleteOnClose, NTSTATUS *PResult)
{
    /*
     * Attempt to insert our FileNode into the volume device's generic table.
     * If an FileNode with the same UserContext already exists, then use that
     * FileNode instead.
     */

    PAGED_CODE();

    PDEVICE_OBJECT FsvolDeviceObject = FileNode->FsvolDeviceObject;
    FSP_FILE_NODE *OpenedFileNode;
    BOOLEAN Inserted;
    NTSTATUS Result;

    FspFsvolDeviceLockContextTable(FsvolDeviceObject);

    OpenedFileNode = FspFsvolDeviceInsertContext(FsvolDeviceObject,
        FileNode->UserContext, FileNode, &FileNode->ElementStorage, &Inserted);
    ASSERT(0 != OpenedFileNode);

    if (Inserted)
    {
        /*
         * The new FileNode was inserted into the Context table. Set its share access
         * and reference and open it. There should be (at least) two references to this
         * FileNode, one from our caller and one from the Context table.
         */
        ASSERT(OpenedFileNode == FileNode);

        IoSetShareAccess(GrantedAccess, ShareAccess, FileObject,
            &OpenedFileNode->ShareAccess);
    }
    else
    {
        /*
         * The new FileNode was NOT inserted into the Context table. Instead we are
         * opening a prior FileNode that we found in the table.
         */
        ASSERT(OpenedFileNode != FileNode);

        if (OpenedFileNode->Flags.DeletePending)
        {
            Result = STATUS_DELETE_PENDING;
            goto exit;
        }

        /*
         * FastFat says to do the following on Vista and above.
         *
         * Quote:
         *     Do an extra test for writeable user sections if the user did not allow
         *     write sharing - this is neccessary since a section may exist with no handles
         *     open to the file its based against.
         */
        if (!FlagOn(ShareAccess, FILE_SHARE_WRITE) &&
            FlagOn(GrantedAccess,
                FILE_EXECUTE | FILE_READ_DATA | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE) &&
            MmDoesFileHaveUserWritableReferences(&OpenedFileNode->NonPaged->SectionObjectPointers))
        {
            Result = STATUS_SHARING_VIOLATION;
            goto exit;
        }

        /* share access check */
        Result = IoCheckShareAccess(GrantedAccess, ShareAccess, FileObject,
            &OpenedFileNode->ShareAccess, TRUE);

    exit:
        if (!NT_SUCCESS(Result))
        {
            if (0 != PResult)
                *PResult = Result;

            OpenedFileNode = 0;
        }
    }

    if (0 != OpenedFileNode)
    {
        FspFileNodeReference(OpenedFileNode);
        OpenedFileNode->OpenCount++;

        if (DeleteOnClose)
            OpenedFileNode->Flags.DeleteOnClose = TRUE;
    }

    FspFsvolDeviceUnlockContextTable(FsvolDeviceObject);

    return OpenedFileNode;
}

VOID FspFileNodeClose(FSP_FILE_NODE *FileNode, PFILE_OBJECT FileObject,
    PBOOLEAN PDeletePending)
{
    /*
     * Close the FileNode. If the OpenCount becomes zero remove it
     * from the Context table.
     */

    PAGED_CODE();

    PDEVICE_OBJECT FsvolDeviceObject = FileNode->FsvolDeviceObject;
    BOOLEAN Deleted = FALSE, DeletePending;

    FspFsvolDeviceLockContextTable(FsvolDeviceObject);

    if (FileNode->Flags.DeleteOnClose)
        FileNode->Flags.DeletePending = TRUE;
    DeletePending = 0 != FileNode->Flags.DeletePending;

    IoRemoveShareAccess(FileObject, &FileNode->ShareAccess);
    if (0 == --FileNode->OpenCount)
        FspFsvolDeviceDeleteContext(FsvolDeviceObject, FileNode->UserContext, &Deleted);

    FspFsvolDeviceUnlockContextTable(FsvolDeviceObject);

    if (Deleted)
        FspFileNodeDereference(FileNode);

    if (0 != PDeletePending)
        *PDeletePending = Deleted && DeletePending;
}

static VOID FspFileNodeGetFileInfoNp(FSP_FILE_NODE_NONPAGED *FileNodeNp, FSP_FSCTL_FILE_INFO *FileInfoNp)
{
    // !PAGED_CODE();

    KIRQL Irql;

    KeAcquireSpinLock(&FileNodeNp->InfoSpinLock, &Irql);
    FileInfoNp->FileAttributes = FileNodeNp->FileAttributes;
    FileInfoNp->ReparseTag = FileNodeNp->ReparseTag;
    FileInfoNp->CreationTime = FileNodeNp->CreationTime;
    FileInfoNp->LastAccessTime = FileNodeNp->LastAccessTime;
    FileInfoNp->LastWriteTime = FileNodeNp->LastWriteTime;
    FileInfoNp->ChangeTime = FileNodeNp->ChangeTime;
    KeReleaseSpinLock(&FileNodeNp->InfoSpinLock, Irql);
}

static BOOLEAN FspFileNodeTryGetFileInfoNp(FSP_FILE_NODE_NONPAGED *FileNodeNp,
    FSP_FSCTL_FILE_INFO *FileInfoNp)
{
    // !PAGED_CODE();

    KIRQL Irql;
    BOOLEAN Result;

    KeAcquireSpinLock(&FileNodeNp->InfoSpinLock, &Irql);
    if (0 < FileNodeNp->InfoExpirationTime && KeQueryInterruptTime() < FileNodeNp->InfoExpirationTime)
    {
        FileInfoNp->FileAttributes = FileNodeNp->FileAttributes;
        FileInfoNp->ReparseTag = FileNodeNp->ReparseTag;
        FileInfoNp->CreationTime = FileNodeNp->CreationTime;
        FileInfoNp->LastAccessTime = FileNodeNp->LastAccessTime;
        FileInfoNp->LastWriteTime = FileNodeNp->LastWriteTime;
        FileInfoNp->ChangeTime = FileNodeNp->ChangeTime;
        Result = TRUE;
    }
    else
        Result = FALSE;
    KeReleaseSpinLock(&FileNodeNp->InfoSpinLock, Irql);

    return Result;
}

static VOID FspFileNodeSetFileInfoNp(FSP_FILE_NODE_NONPAGED *FileNodeNp,
    const FSP_FSCTL_FILE_INFO *FileInfoNp, UINT64 FileInfoTimeout)
{
    // !PAGED_CODE();

    KIRQL Irql;

    KeAcquireSpinLock(&FileNodeNp->InfoSpinLock, &Irql);
    FileNodeNp->FileAttributes = FileInfoNp->FileAttributes;
    FileNodeNp->ReparseTag = FileInfoNp->ReparseTag;
    FileNodeNp->CreationTime = FileInfoNp->CreationTime;
    FileNodeNp->LastAccessTime = FileInfoNp->LastAccessTime;
    FileNodeNp->LastWriteTime = FileInfoNp->LastWriteTime;
    FileNodeNp->ChangeTime = FileInfoNp->ChangeTime;
    FileNodeNp->InfoExpirationTime = 0 != FileInfoTimeout ?
        KeQueryInterruptTime() + FileInfoTimeout : 0;
    KeReleaseSpinLock(&FileNodeNp->InfoSpinLock, Irql);
}

VOID FspFileNodeGetFileInfo(FSP_FILE_NODE *FileNode, FSP_FSCTL_FILE_INFO *FileInfo)
{
    PAGED_CODE();

    FSP_FILE_NODE_NONPAGED *FileNodeNp = FileNode->NonPaged;
    FSP_FSCTL_FILE_INFO FileInfoNp;

    FspFileNodeGetFileInfoNp(FileNodeNp, &FileInfoNp);

    *FileInfo = FileInfoNp;
}

BOOLEAN FspFileNodeTryGetFileInfo(FSP_FILE_NODE *FileNode, FSP_FSCTL_FILE_INFO *FileInfo)
{
    PAGED_CODE();

    FSP_FILE_NODE_NONPAGED *FileNodeNp = FileNode->NonPaged;
    FSP_FSCTL_FILE_INFO FileInfoNp;

    if (!FspFileNodeTryGetFileInfoNp(FileNodeNp, &FileInfoNp))
        return FALSE;

    *FileInfo = FileInfoNp;
    return TRUE;
}

VOID FspFileNodeSetFileInfo(FSP_FILE_NODE *FileNode, const FSP_FSCTL_FILE_INFO *FileInfo)
{
    PAGED_CODE();

    UINT64 FileInfoTimeout = FspFsvolDeviceExtension(FileNode->FsvolDeviceObject)->
        VolumeParams.FileInfoTimeout * 10000ULL;
    FSP_FILE_NODE_NONPAGED *FileNodeNp = FileNode->NonPaged;
    FSP_FSCTL_FILE_INFO FileInfoNp = *FileInfo;

    FspFileNodeSetFileInfoNp(FileNodeNp, &FileInfoNp, FileInfoTimeout);
}

NTSTATUS FspFileDescCreate(FSP_FILE_DESC **PFileDesc)
{
    PAGED_CODE();

    *PFileDesc = FspAlloc(sizeof(FSP_FILE_DESC));
    if (0 == *PFileDesc)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(*PFileDesc, sizeof(FSP_FILE_DESC));

    return STATUS_SUCCESS;
}

VOID FspFileDescDelete(FSP_FILE_DESC *FileDesc)
{
    PAGED_CODE();

    FspFree(FileDesc);
}

VOID FspFileObjectSetSizes(PFILE_OBJECT FileObject,
    UINT64 AllocationSize, UINT64 FileSize)
{
    PAGED_CODE();

    FSP_FILE_NODE *FileNode = FileObject->FsContext;

    ASSERT(ExIsResourceAcquiredExclusiveLite(FileNode->Header.PagingIoResource));

    FileNode->Header.AllocationSize.QuadPart = AllocationSize;
    FileNode->Header.FileSize.QuadPart = FileSize;
    FileNode->CcStatus = FspCcSetFileSizes(
        FileObject, (PCC_FILE_SIZES)&FileNode->Header.AllocationSize);
}