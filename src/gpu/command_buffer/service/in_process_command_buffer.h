// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_IN_PROCESS_COMMAND_BUFFER_H_
#define GPU_COMMAND_BUFFER_SERVICE_IN_PROCESS_COMMAND_BUFFER_H_

#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "gpu/command_buffer/common/command_buffer.h"
#include "gpu/gpu_export.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gpu_preference.h"

namespace gfx {
class GLContext;
class GLImage;
class GLSurface;
class Size;
}

namespace gpu {

namespace gles2 {
class GLES2Decoder;
}

class GpuScheduler;
class TransferBufferManagerInterface;

// This class provides a thread-safe interface to the global GPU service (for
// example GPU thread) when being run in single process mode.
// However, the behavior for accessing one context (i.e. one instance of this
// class) from different client threads is undefined.
class GPU_EXPORT InProcessCommandBuffer : public CommandBuffer {
 public:
  InProcessCommandBuffer();
  virtual ~InProcessCommandBuffer();

  // Used to override the GPU thread with explicit scheduling.
  // (By default an internal GPU thread will be spawned to handle all GL work
  // and the two functions are unused.)
  // The callback will be called from different client threads. After the
  // callback is issued, the client is expected to eventually call
  // ProcessGpuWorkOnCurrentThread(). The latter cannot be called from different
  // threads.
  // The callback needs to be set before any context is created.
  static void SetScheduleCallback(const base::Closure& callback);
  static void ProcessGpuWorkOnCurrentThread();

  static void EnableVirtualizedContext();

  bool Initialize(bool is_offscreen,
                  bool share_resources,
                  gfx::AcceleratedWidget window,
                  const gfx::Size& size,
                  const char* allowed_extensions,
                  const std::vector<int32>& attribs,
                  gfx::GpuPreference gpu_preference,
                  const base::Closure& context_lost_callback,
                  unsigned int share_group_id);
  void Destroy();
  void SignalSyncPoint(unsigned sync_point,
                       const base::Closure& callback);
  unsigned int CreateImageForGpuMemoryBuffer(
      gfx::GpuMemoryBufferHandle buffer,
      gfx::Size size);
  void RemoveImage(unsigned int image_id);

  // CommandBuffer implementation:
  virtual bool Initialize() OVERRIDE;
  virtual State GetState() OVERRIDE;
  virtual State GetLastState() OVERRIDE;
  virtual int32 GetLastToken() OVERRIDE;
  virtual void Flush(int32 put_offset) OVERRIDE;
  virtual State FlushSync(int32 put_offset, int32 last_known_get) OVERRIDE;
  virtual void SetGetBuffer(int32 shm_id) OVERRIDE;
  virtual void SetGetOffset(int32 get_offset) OVERRIDE;
  virtual gpu::Buffer CreateTransferBuffer(size_t size, int32* id) OVERRIDE;
  virtual void DestroyTransferBuffer(int32 id) OVERRIDE;
  virtual gpu::Buffer GetTransferBuffer(int32 id) OVERRIDE;
  virtual void SetToken(int32 token) OVERRIDE;
  virtual void SetParseError(gpu::error::Error error) OVERRIDE;
  virtual void SetContextLostReason(
      gpu::error::ContextLostReason reason) OVERRIDE;
  virtual uint32 InsertSyncPoint() OVERRIDE;
  virtual gpu::error::Error GetLastError() OVERRIDE;

  // The serializer interface to the GPU service (i.e. thread).
  class SchedulerClient {
   public:
     virtual ~SchedulerClient() {}
     virtual void QueueTask(const base::Closure& task) = 0;
  };

 private:
  bool InitializeOnGpuThread(bool is_offscreen,
                             gfx::AcceleratedWidget window,
                             const gfx::Size& size,
                             const char* allowed_extensions,
                             const std::vector<int32>& attribs,
                             gfx::GpuPreference gpu_preference);
  bool DestroyOnGpuThread();
  void FlushOnGpuThread(int32 put_offset);
  void CreateImageOnGpuThread(gfx::GpuMemoryBufferHandle buffer,
                              gfx::Size size,
                              unsigned int image_id);
  void RemoveImageOnGpuThread(unsigned int image_id);
  bool MakeCurrent();
  bool IsContextLost();
  base::Closure WrapCallback(const base::Closure& callback);
  State GetStateFast();
  void QueueTask(const base::Closure& task) { queue_->QueueTask(task); }

  // Callbacks:
  void OnContextLost();
  void OnResizeView(gfx::Size size, float scale_factor);
  bool GetBufferChanged(int32 transfer_buffer_id);
  void PumpCommands();

  // Members accessed on the gpu thread (possibly with the exception of
  // creation):
  bool context_lost_;
  bool share_resources_;
  scoped_ptr<TransferBufferManagerInterface> transfer_buffer_manager_;
  scoped_ptr<GpuScheduler> gpu_scheduler_;
  scoped_ptr<gles2::GLES2Decoder> decoder_;
  scoped_refptr<gfx::GLContext> context_;
  scoped_refptr<gfx::GLSurface> surface_;
  base::Closure context_lost_callback_;
  unsigned int share_group_id_;

  // Members accessed on the client thread:
  State last_state_;
  int32 last_put_offset_;

  // Accessed on both threads:
  scoped_ptr<CommandBuffer> command_buffer_;
  base::Lock command_buffer_lock_;
  base::WaitableEvent flush_event_;
  scoped_ptr<SchedulerClient> queue_;

  DISALLOW_COPY_AND_ASSIGN(InProcessCommandBuffer);
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_IN_PROCESS_COMMAND_BUFFER_H_
