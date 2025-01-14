// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_PROTOCOL_PAIRING_REGISTRY_DELEGATE_WIN_H_
#define REMOTING_PROTOCOL_PAIRING_REGISTRY_DELEGATE_WIN_H_

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "remoting/base/scoped_reg_key_win.h"
#include "remoting/protocol/pairing_registry.h"

namespace base {
class ListValue;
}  // namespace base

namespace remoting {

// Stores client pairing information in Windows registry. Two separate registry
// keys are used:
//  - |privileged| - contains the shared secrets of all pairings. This key must
//                   be protected by a strong ACL denying access to unprivileged
//                   code.
//  - |unprivileged| - contains the rest of pairing state.
//
// Creator of this object is responsible for passing the registry key handles
// with appropriate access. |privileged| may be NULL if read-only access is
// sufficient. Shared secrets will not be returned in such a case.
class PairingRegistryDelegateWin
    : public protocol::PairingRegistry::Delegate {
 public:
  PairingRegistryDelegateWin();
  virtual ~PairingRegistryDelegateWin();

  // Passes the root keys to be used to access the pairing registry store.
  // |privileged| is optional and may be NULL. The caller retains ownership of
  // the passed handles.
  bool SetRootKeys(HKEY privileged, HKEY unprivileged);

  // PairingRegistry::Delegate interface
  virtual scoped_ptr<base::ListValue> LoadAll() OVERRIDE;
  virtual bool DeleteAll() OVERRIDE;
  virtual protocol::PairingRegistry::Pairing Load(
      const std::string& client_id) OVERRIDE;
  virtual bool Save(const protocol::PairingRegistry::Pairing& pairing) OVERRIDE;
  virtual bool Delete(const std::string& client_id) OVERRIDE;

 private:
  ScopedRegKey privileged_;
  ScopedRegKey unprivileged_;

  DISALLOW_COPY_AND_ASSIGN(PairingRegistryDelegateWin);
};

}  // namespace remoting

#endif  // REMOTING_PROTOCOL_PAIRING_REGISTRY_DELEGATE_WIN_H_
