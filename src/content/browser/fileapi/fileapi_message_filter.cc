// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/fileapi/fileapi_message_filter.h"

#include <string>
#include <vector>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/memory/scoped_ptr.h"
#include "base/platform_file.h"
#include "base/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/fileapi/browser_file_system_helper.h"
#include "content/browser/fileapi/chrome_blob_storage_context.h"
#include "content/common/fileapi/file_system_messages.h"
#include "content/common/fileapi/webblob_messages.h"
#include "content/public/browser/user_metrics.h"
#include "ipc/ipc_platform_file.h"
#include "net/base/mime_util.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "url/gurl.h"
#include "webkit/browser/blob/blob_storage_controller.h"
#include "webkit/browser/fileapi/file_observers.h"
#include "webkit/browser/fileapi/file_permission_policy.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/isolated_context.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/blob/blob_data.h"
#include "webkit/common/blob/shareable_file_reference.h"
#include "webkit/common/fileapi/directory_entry.h"
#include "webkit/common/fileapi/file_system_types.h"
#include "webkit/common/fileapi/file_system_util.h"

using fileapi::FileSystemFileUtil;
using fileapi::FileSystemBackend;
using fileapi::FileSystemOperation;
using fileapi::FileSystemURL;
using fileapi::FileUpdateObserver;
using fileapi::UpdateObserverList;
using webkit_blob::BlobData;
using webkit_blob::BlobStorageController;

namespace content {

namespace {

void RevokeFilePermission(int child_id, const base::FilePath& path) {
  ChildProcessSecurityPolicyImpl::GetInstance()->RevokeAllPermissionsForFile(
    child_id, path);
}

}  // namespace

FileAPIMessageFilter::FileAPIMessageFilter(
    int process_id,
    net::URLRequestContextGetter* request_context_getter,
    fileapi::FileSystemContext* file_system_context,
    ChromeBlobStorageContext* blob_storage_context)
    : process_id_(process_id),
      context_(file_system_context),
      request_context_getter_(request_context_getter),
      request_context_(NULL),
      blob_storage_context_(blob_storage_context) {
  DCHECK(context_);
  DCHECK(request_context_getter_.get());
  DCHECK(blob_storage_context);
}

FileAPIMessageFilter::FileAPIMessageFilter(
    int process_id,
    net::URLRequestContext* request_context,
    fileapi::FileSystemContext* file_system_context,
    ChromeBlobStorageContext* blob_storage_context)
    : process_id_(process_id),
      context_(file_system_context),
      request_context_(request_context),
      blob_storage_context_(blob_storage_context) {
  DCHECK(context_);
  DCHECK(request_context_);
  DCHECK(blob_storage_context);
}

void FileAPIMessageFilter::OnChannelConnected(int32 peer_pid) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  BrowserMessageFilter::OnChannelConnected(peer_pid);

  if (request_context_getter_.get()) {
    DCHECK(!request_context_);
    request_context_ = request_context_getter_->GetURLRequestContext();
    request_context_getter_ = NULL;
    DCHECK(request_context_);
  }

  operation_runner_ = context_->CreateFileSystemOperationRunner();
}

void FileAPIMessageFilter::OnChannelClosing() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  BrowserMessageFilter::OnChannelClosing();

  // Unregister all the blob URLs that are previously registered in this
  // process.
  for (base::hash_set<std::string>::const_iterator iter = blob_urls_.begin();
       iter != blob_urls_.end(); ++iter) {
    blob_storage_context_->controller()->RemoveBlob(GURL(*iter));
  }

  in_transit_snapshot_files_.clear();

  // Close all files that are previously OpenFile()'ed in this process.
  if (!on_close_callbacks_.IsEmpty()) {
    DLOG(INFO)
        << "File API: Renderer process shut down before NotifyCloseFile"
        << " for " << on_close_callbacks_.size() << " files opened in PPAPI";
  }

  for (OnCloseCallbackMap::iterator itr(&on_close_callbacks_);
       !itr.IsAtEnd(); itr.Advance()) {
    const base::Closure* callback = itr.GetCurrentValue();
    DCHECK(callback);
    if (!callback->is_null())
      callback->Run();
  }

  on_close_callbacks_.Clear();
  operation_runner_.reset();
  operations_.clear();
}

base::TaskRunner* FileAPIMessageFilter::OverrideTaskRunnerForMessage(
    const IPC::Message& message) {
  if (message.type() == FileSystemHostMsg_SyncGetPlatformPath::ID)
    return context_->default_file_task_runner();
  return NULL;
}

bool FileAPIMessageFilter::OnMessageReceived(
    const IPC::Message& message, bool* message_was_ok) {
  *message_was_ok = true;
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP_EX(FileAPIMessageFilter, message, *message_was_ok)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Open, OnOpen)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_DeleteFileSystem, OnDeleteFileSystem)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Move, OnMove)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Copy, OnCopy)
    IPC_MESSAGE_HANDLER(FileSystemMsg_Remove, OnRemove)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_ReadMetadata, OnReadMetadata)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Create, OnCreate)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Exists, OnExists)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_ReadDirectory, OnReadDirectory)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Write, OnWrite)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_Truncate, OnTruncate)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_TouchFile, OnTouchFile)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_CancelWrite, OnCancel)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_OpenFile, OnOpenFile)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_NotifyCloseFile, OnNotifyCloseFile)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_CreateSnapshotFile,
                        OnCreateSnapshotFile)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_DidReceiveSnapshotFile,
                        OnDidReceiveSnapshotFile)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_WillUpdate, OnWillUpdate)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_DidUpdate, OnDidUpdate)
    IPC_MESSAGE_HANDLER(FileSystemHostMsg_SyncGetPlatformPath,
                        OnSyncGetPlatformPath)
    IPC_MESSAGE_HANDLER(BlobHostMsg_StartBuildingBlob, OnStartBuildingBlob)
    IPC_MESSAGE_HANDLER(BlobHostMsg_AppendBlobDataItem, OnAppendBlobDataItem)
    IPC_MESSAGE_HANDLER(BlobHostMsg_SyncAppendSharedMemory,
                        OnAppendSharedMemory)
    IPC_MESSAGE_HANDLER(BlobHostMsg_FinishBuildingBlob, OnFinishBuildingBlob)
    IPC_MESSAGE_HANDLER(BlobHostMsg_CloneBlob, OnCloneBlob)
    IPC_MESSAGE_HANDLER(BlobHostMsg_RemoveBlob, OnRemoveBlob)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP_EX()
  return handled;
}

FileAPIMessageFilter::~FileAPIMessageFilter() {}

void FileAPIMessageFilter::BadMessageReceived() {
  RecordAction(UserMetricsAction("BadMessageTerminate_FAMF"));
  BrowserMessageFilter::BadMessageReceived();
}

void FileAPIMessageFilter::OnOpen(
    int request_id, const GURL& origin_url, fileapi::FileSystemType type,
    int64 requested_size, bool create) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (type == fileapi::kFileSystemTypeTemporary) {
    RecordAction(UserMetricsAction("OpenFileSystemTemporary"));
  } else if (type == fileapi::kFileSystemTypePersistent) {
    RecordAction(UserMetricsAction("OpenFileSystemPersistent"));
  }
  // TODO(kinuko): Use this mode for IPC too.
  fileapi::OpenFileSystemMode mode =
      create ? fileapi::OPEN_FILE_SYSTEM_CREATE_IF_NONEXISTENT
             : fileapi::OPEN_FILE_SYSTEM_FAIL_IF_NONEXISTENT;
  context_->OpenFileSystem(origin_url, type, mode, base::Bind(
      &FileAPIMessageFilter::DidOpenFileSystem, this, request_id));
}

void FileAPIMessageFilter::OnDeleteFileSystem(
    int request_id,
    const GURL& origin_url,
    fileapi::FileSystemType type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  context_->DeleteFileSystem(origin_url, type, base::Bind(
      &FileAPIMessageFilter::DidDeleteFileSystem, this, request_id));
}

void FileAPIMessageFilter::OnMove(
    int request_id, const GURL& src_path, const GURL& dest_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL src_url(context_->CrackURL(src_path));
  FileSystemURL dest_url(context_->CrackURL(dest_path));
  const int src_permissions =
      fileapi::kReadFilePermissions | fileapi::kWriteFilePermissions;
  if (!HasPermissionsForFile(src_url, src_permissions, &error) ||
      !HasPermissionsForFile(
          dest_url, fileapi::kCreateFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->Move(
      src_url, dest_url,
      base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
}

void FileAPIMessageFilter::OnCopy(
    int request_id, const GURL& src_path, const GURL& dest_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL src_url(context_->CrackURL(src_path));
  FileSystemURL dest_url(context_->CrackURL(dest_path));
  if (!HasPermissionsForFile(src_url, fileapi::kReadFilePermissions, &error) ||
      !HasPermissionsForFile(
          dest_url, fileapi::kCreateFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->Copy(
      src_url, dest_url,
      base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
}

void FileAPIMessageFilter::OnRemove(
    int request_id, const GURL& path, bool recursive) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kWriteFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->Remove(
      url, recursive,
      base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
}

void FileAPIMessageFilter::OnReadMetadata(
    int request_id, const GURL& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kReadFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->GetMetadata(
      url, base::Bind(&FileAPIMessageFilter::DidGetMetadata, this, request_id));
}

void FileAPIMessageFilter::OnCreate(
    int request_id, const GURL& path, bool exclusive,
    bool is_directory, bool recursive) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kCreateFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  if (is_directory) {
    operations_[request_id] = operation_runner()->CreateDirectory(
        url, exclusive, recursive,
        base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
  } else {
    operations_[request_id] = operation_runner()->CreateFile(
        url, exclusive,
        base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
  }
}

void FileAPIMessageFilter::OnExists(
    int request_id, const GURL& path, bool is_directory) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kReadFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  if (is_directory) {
    operations_[request_id] = operation_runner()->DirectoryExists(
        url,
        base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
  } else {
    operations_[request_id] = operation_runner()->FileExists(
        url,
        base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
  }
}

void FileAPIMessageFilter::OnReadDirectory(
    int request_id, const GURL& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kReadFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->ReadDirectory(
      url, base::Bind(&FileAPIMessageFilter::DidReadDirectory,
                      this, request_id));
}

void FileAPIMessageFilter::OnWrite(
    int request_id,
    const GURL& path,
    const GURL& blob_url,
    int64 offset) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (!request_context_) {
    // We can't write w/o a request context, trying to do so will crash.
    NOTREACHED();
    return;
  }

  FileSystemURL url(context_->CrackURL(path));
  base::PlatformFileError error;
  if (!HasPermissionsForFile(url, fileapi::kWriteFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->Write(
      request_context_, url, blob_url, offset,
      base::Bind(&FileAPIMessageFilter::DidWrite, this, request_id));
}

void FileAPIMessageFilter::OnTruncate(
    int request_id,
    const GURL& path,
    int64 length) {
  base::PlatformFileError error;
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, fileapi::kWriteFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->Truncate(
      url, length,
      base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
}

void FileAPIMessageFilter::OnTouchFile(
    int request_id,
    const GURL& path,
    const base::Time& last_access_time,
    const base::Time& last_modified_time) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  FileSystemURL url(context_->CrackURL(path));
  base::PlatformFileError error;
  if (!HasPermissionsForFile(url, fileapi::kCreateFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->TouchFile(
      url, last_access_time, last_modified_time,
      base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
}

void FileAPIMessageFilter::OnCancel(
    int request_id,
    int request_id_to_cancel) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  OperationsMap::iterator found = operations_.find(request_id_to_cancel);
  if (found != operations_.end()) {
    // The cancel will eventually send both the write failure and the cancel
    // success.
    operation_runner()->Cancel(
        found->second,
        base::Bind(&FileAPIMessageFilter::DidFinish, this, request_id));
  } else {
    // The write already finished; report that we failed to stop it.
    Send(new FileSystemMsg_DidFail(
        request_id, base::PLATFORM_FILE_ERROR_INVALID_OPERATION));
  }
}

void FileAPIMessageFilter::OnOpenFile(
    int request_id, const GURL& path, int file_flags) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  base::PlatformFileError error;
  const int open_permissions = base::PLATFORM_FILE_OPEN |
                               (file_flags & fileapi::kOpenFilePermissions);
  FileSystemURL url(context_->CrackURL(path));
  if (!HasPermissionsForFile(url, open_permissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  quota::QuotaLimitType quota_policy = quota::kQuotaLimitTypeUnknown;
  quota::QuotaManagerProxy* quota_manager_proxy =
      context_->quota_manager_proxy();
  CHECK(quota_manager_proxy);
  CHECK(quota_manager_proxy->quota_manager());

  if (quota_manager_proxy->quota_manager()->IsStorageUnlimited(
          url.origin(), FileSystemTypeToQuotaStorageType(url.type()))) {
    quota_policy = quota::kQuotaLimitTypeUnlimited;
  } else {
    quota_policy = quota::kQuotaLimitTypeLimited;
  }

  operations_[request_id] = operation_runner()->OpenFile(
      url, file_flags, PeerHandle(),
      base::Bind(&FileAPIMessageFilter::DidOpenFile, this, request_id,
                 quota_policy));
}

void FileAPIMessageFilter::OnNotifyCloseFile(int file_open_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // Remove |file_open_id| from the map of |on_close_callback|s.
  // It must only be called for a ID that is successfully opened and enrolled in
  // DidOpenFile.
  base::Closure* on_close_callback = on_close_callbacks_.Lookup(file_open_id);
  if (on_close_callback && !on_close_callback->is_null()) {
    on_close_callback->Run();
    on_close_callbacks_.Remove(file_open_id);
  }
}

void FileAPIMessageFilter::OnWillUpdate(const GURL& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  FileSystemURL url(context_->CrackURL(path));
  if (!url.is_valid())
    return;
  const UpdateObserverList* observers =
      context_->GetUpdateObservers(url.type());
  if (!observers)
    return;
  observers->Notify(&FileUpdateObserver::OnStartUpdate, MakeTuple(url));
}

void FileAPIMessageFilter::OnDidUpdate(const GURL& path, int64 delta) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  FileSystemURL url(context_->CrackURL(path));
  if (!url.is_valid())
    return;
  const UpdateObserverList* observers =
      context_->GetUpdateObservers(url.type());
  if (!observers)
    return;
  observers->Notify(&FileUpdateObserver::OnUpdate, MakeTuple(url, delta));
  observers->Notify(&FileUpdateObserver::OnEndUpdate, MakeTuple(url));
}

void FileAPIMessageFilter::OnSyncGetPlatformPath(
    const GURL& path, base::FilePath* platform_path) {
  SyncGetPlatformPath(context_, process_id_, path, platform_path);
}

void FileAPIMessageFilter::OnCreateSnapshotFile(
    int request_id, const GURL& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  FileSystemURL url(context_->CrackURL(path));

  // Make sure if this file can be read by the renderer as this is
  // called when the renderer is about to create a new File object
  // (for reading the file).
  base::PlatformFileError error;
  if (!HasPermissionsForFile(url, fileapi::kReadFilePermissions, &error)) {
    Send(new FileSystemMsg_DidFail(request_id, error));
    return;
  }

  operations_[request_id] = operation_runner()->CreateSnapshotFile(
      url,
      base::Bind(&FileAPIMessageFilter::DidCreateSnapshot,
                 this, request_id, url));
}

void FileAPIMessageFilter::OnDidReceiveSnapshotFile(int request_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  in_transit_snapshot_files_.erase(request_id);
}

void FileAPIMessageFilter::OnStartBuildingBlob(const GURL& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  blob_storage_context_->controller()->StartBuildingBlob(url);
  blob_urls_.insert(url.spec());
}

void FileAPIMessageFilter::OnAppendBlobDataItem(
    const GURL& url, const BlobData::Item& item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (item.type() == BlobData::Item::TYPE_FILE_FILESYSTEM) {
    base::PlatformFileError error;
    FileSystemURL filesystem_url(context_->CrackURL(item.url()));
    if (!HasPermissionsForFile(filesystem_url,
                               fileapi::kReadFilePermissions, &error)) {
      OnRemoveBlob(url);
      return;
    }
  }
  if (item.type() == BlobData::Item::TYPE_FILE &&
      !ChildProcessSecurityPolicyImpl::GetInstance()->CanReadFile(
          process_id_, item.path())) {
    OnRemoveBlob(url);
    return;
  }
  if (item.length() == 0) {
    BadMessageReceived();
    return;
  }
  blob_storage_context_->controller()->AppendBlobDataItem(url, item);
}

void FileAPIMessageFilter::OnAppendSharedMemory(
    const GURL& url, base::SharedMemoryHandle handle, size_t buffer_size) {
  DCHECK(base::SharedMemory::IsHandleValid(handle));
  if (!buffer_size) {
    BadMessageReceived();
    return;
  }
#if defined(OS_WIN)
  base::SharedMemory shared_memory(handle, true, PeerHandle());
#else
  base::SharedMemory shared_memory(handle, true);
#endif
  if (!shared_memory.Map(buffer_size)) {
    OnRemoveBlob(url);
    return;
  }

  BlobData::Item item;
  item.SetToSharedBytes(static_cast<char*>(shared_memory.memory()),
                        buffer_size);
  blob_storage_context_->controller()->AppendBlobDataItem(url, item);
}

void FileAPIMessageFilter::OnFinishBuildingBlob(
    const GURL& url, const std::string& content_type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  blob_storage_context_->controller()->FinishBuildingBlob(url, content_type);
}

void FileAPIMessageFilter::OnCloneBlob(
    const GURL& url, const GURL& src_url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  blob_storage_context_->controller()->CloneBlob(url, src_url);
  blob_urls_.insert(url.spec());
}

void FileAPIMessageFilter::OnRemoveBlob(const GURL& url) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  blob_storage_context_->controller()->RemoveBlob(url);
  blob_urls_.erase(url.spec());
}

void FileAPIMessageFilter::DidFinish(int request_id,
                                     base::PlatformFileError result) {
  if (result == base::PLATFORM_FILE_OK)
    Send(new FileSystemMsg_DidSucceed(request_id));
  else
    Send(new FileSystemMsg_DidFail(request_id, result));
  operations_.erase(request_id);
}

void FileAPIMessageFilter::DidGetMetadata(
    int request_id,
    base::PlatformFileError result,
    const base::PlatformFileInfo& info) {
  if (result == base::PLATFORM_FILE_OK)
    Send(new FileSystemMsg_DidReadMetadata(request_id, info));
  else
    Send(new FileSystemMsg_DidFail(request_id, result));
  operations_.erase(request_id);
}

void FileAPIMessageFilter::DidReadDirectory(
    int request_id,
    base::PlatformFileError result,
    const std::vector<fileapi::DirectoryEntry>& entries,
    bool has_more) {
  if (result == base::PLATFORM_FILE_OK)
    Send(new FileSystemMsg_DidReadDirectory(request_id, entries, has_more));
  else
    Send(new FileSystemMsg_DidFail(request_id, result));
  operations_.erase(request_id);
}

void FileAPIMessageFilter::DidOpenFile(int request_id,
                                       quota::QuotaLimitType quota_policy,
                                       base::PlatformFileError result,
                                       base::PlatformFile file,
                                       const base::Closure& on_close_callback,
                                       base::ProcessHandle peer_handle) {
  if (result == base::PLATFORM_FILE_OK) {
    IPC::PlatformFileForTransit file_for_transit =
        file != base::kInvalidPlatformFileValue ?
            IPC::GetFileHandleForProcess(file, peer_handle, true) :
            IPC::InvalidPlatformFileForTransit();
    int file_open_id = on_close_callbacks_.Add(
        new base::Closure(on_close_callback));

    Send(new FileSystemMsg_DidOpenFile(request_id,
                                       file_for_transit,
                                       file_open_id,
                                       quota_policy));
  } else {
    Send(new FileSystemMsg_DidFail(request_id,
                                   result));
  }
  operations_.erase(request_id);
}

void FileAPIMessageFilter::DidWrite(int request_id,
                                    base::PlatformFileError result,
                                    int64 bytes,
                                    bool complete) {
  if (result == base::PLATFORM_FILE_OK) {
    Send(new FileSystemMsg_DidWrite(request_id, bytes, complete));
    if (complete)
      operations_.erase(request_id);
  } else {
    Send(new FileSystemMsg_DidFail(request_id, result));
    operations_.erase(request_id);
  }
}

void FileAPIMessageFilter::DidOpenFileSystem(int request_id,
                                             base::PlatformFileError result,
                                             const std::string& name,
                                             const GURL& root) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  if (result == base::PLATFORM_FILE_OK) {
    DCHECK(root.is_valid());
    Send(new FileSystemMsg_DidOpenFileSystem(request_id, name, root));
  } else {
    Send(new FileSystemMsg_DidFail(request_id, result));
  }
  // For OpenFileSystem we do not create a new operation, so no unregister here.
}

void FileAPIMessageFilter::DidDeleteFileSystem(
    int request_id,
    base::PlatformFileError result) {
  if (result == base::PLATFORM_FILE_OK)
    Send(new FileSystemMsg_DidSucceed(request_id));
  else
    Send(new FileSystemMsg_DidFail(request_id, result));
  // For DeleteFileSystem we do not create a new operation,
  // so no unregister here.
}

void FileAPIMessageFilter::DidCreateSnapshot(
    int request_id,
    const fileapi::FileSystemURL& url,
    base::PlatformFileError result,
    const base::PlatformFileInfo& info,
    const base::FilePath& platform_path,
    const scoped_refptr<webkit_blob::ShareableFileReference>& snapshot_file) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  operations_.erase(request_id);

  if (result != base::PLATFORM_FILE_OK) {
    Send(new FileSystemMsg_DidFail(request_id, result));
    return;
  }

  scoped_refptr<webkit_blob::ShareableFileReference> file_ref = snapshot_file;
  if (!ChildProcessSecurityPolicyImpl::GetInstance()->CanReadFile(
          process_id_, platform_path)) {
    // Give per-file read permission to the snapshot file if it hasn't it yet.
    // In order for the renderer to be able to read the file via File object,
    // it must be granted per-file read permission for the file's platform
    // path. By now, it has already been verified that the renderer has
    // sufficient permissions to read the file, so giving per-file permission
    // here must be safe.
    ChildProcessSecurityPolicyImpl::GetInstance()->GrantReadFile(
        process_id_, platform_path);

    // Revoke all permissions for the file when the last ref of the file
    // is dropped.
    if (!file_ref.get()) {
      // Create a reference for temporary permission handling.
      file_ref = webkit_blob::ShareableFileReference::GetOrCreate(
          platform_path,
          webkit_blob::ShareableFileReference::DONT_DELETE_ON_FINAL_RELEASE,
          context_->default_file_task_runner());
    }
    file_ref->AddFinalReleaseCallback(
        base::Bind(&RevokeFilePermission, process_id_));
  }

  if (file_ref.get()) {
    // This ref is held until OnDidReceiveSnapshotFile is called.
    in_transit_snapshot_files_[request_id] = file_ref;
  }

  // Return the file info and platform_path.
  Send(new FileSystemMsg_DidCreateSnapshotFile(
               request_id, info, platform_path));
}

bool FileAPIMessageFilter::HasPermissionsForFile(
    const FileSystemURL& url, int permissions, base::PlatformFileError* error) {
  return CheckFileSystemPermissionsForProcess(context_, process_id_, url,
                                              permissions, error);
}

}  // namespace content
