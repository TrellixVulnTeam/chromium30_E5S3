// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_PEPPER_PPB_TCP_SERVER_SOCKET_PRIVATE_IMPL_H_
#define CONTENT_RENDERER_PEPPER_PPB_TCP_SERVER_SOCKET_PRIVATE_IMPL_H_

#include "base/compiler_specific.h"
#include "ppapi/shared_impl/private/ppb_tcp_server_socket_shared.h"

namespace content {
class PepperHelperImpl;

class PPB_TCPServerSocket_Private_Impl
    : public ::ppapi::PPB_TCPServerSocket_Shared {
 public:
  static PP_Resource CreateResource(PP_Instance instance);

  virtual void OnAcceptCompleted(
      bool succeeded,
      uint32 accepted_socket_id,
      const PP_NetAddress_Private& local_addr,
      const PP_NetAddress_Private& remote_addr) OVERRIDE;

  virtual void SendListen(const PP_NetAddress_Private& addr,
                          int32_t backlog) OVERRIDE;
  virtual void SendAccept() OVERRIDE;
  virtual void SendStopListening() OVERRIDE;

 private:
  PPB_TCPServerSocket_Private_Impl(PP_Instance instance);
  virtual ~PPB_TCPServerSocket_Private_Impl();

  PepperHelperImpl* GetHelper();

  DISALLOW_COPY_AND_ASSIGN(PPB_TCPServerSocket_Private_Impl);
};

}  // namespace content

#endif  // CONTENT_RENDERER_PEPPER_PPB_TCP_SERVER_SOCKET_PRIVATE_IMPL_H_
