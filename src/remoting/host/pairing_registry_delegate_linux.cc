// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/host/pairing_registry_delegate_linux.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_string_value_serializer.h"
#include "base/location.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "remoting/host/branding.h"

namespace {

// The pairing registry path relative to the configuration directory.
const char kRegistryDirectory[] = "paired-clients";

const char kPairingFilenameFormat[] = "%s.json";
const char kPairingFilenamePattern[] = "*.json";

}  // namespace

namespace remoting {

using protocol::PairingRegistry;

PairingRegistryDelegateLinux::PairingRegistryDelegateLinux() {
}

PairingRegistryDelegateLinux::~PairingRegistryDelegateLinux() {
}

scoped_ptr<base::ListValue> PairingRegistryDelegateLinux::LoadAll() {
  scoped_ptr<base::ListValue> pairings(new base::ListValue());

  // Enumerate all pairing files in the pairing registry.
  base::FilePath registry_path = GetRegistryPath();
  base::FileEnumerator enumerator(registry_path, false,
                                  base::FileEnumerator::FILES,
                                  kPairingFilenamePattern);
  for (base::FilePath pairing_file = enumerator.Next(); !pairing_file.empty();
       pairing_file = enumerator.Next()) {
    // Read the JSON containing pairing data.
    JSONFileValueSerializer serializer(pairing_file);
    int error_code;
    std::string error_message;
    scoped_ptr<base::Value> pairing_json(
        serializer.Deserialize(&error_code, &error_message));
    if (!pairing_json) {
      LOG(WARNING) << "Failed to load '" << pairing_file.value() << "' ("
                   << error_code << ").";
      continue;
    }

    pairings->Append(pairing_json.release());
  }

  return pairings.Pass();
}

bool PairingRegistryDelegateLinux::DeleteAll() {
  // Delete all pairing files in the pairing registry.
  base::FilePath registry_path = GetRegistryPath();
  base::FileEnumerator enumerator(registry_path, false,
                                  base::FileEnumerator::FILES,
                                  kPairingFilenamePattern);

  bool success = true;
  for (base::FilePath pairing_file = enumerator.Next(); !pairing_file.empty();
       pairing_file = enumerator.Next()) {
    success = success && base::DeleteFile(pairing_file, false);
  }

  return success;
}

PairingRegistry::Pairing PairingRegistryDelegateLinux::Load(
    const std::string& client_id) {
  base::FilePath registry_path = GetRegistryPath();
  base::FilePath pairing_file = registry_path.Append(
      base::StringPrintf(kPairingFilenameFormat, client_id.c_str()));

  JSONFileValueSerializer serializer(pairing_file);
  int error_code;
  std::string error_message;
  scoped_ptr<base::Value> pairing(
      serializer.Deserialize(&error_code, &error_message));
  if (!pairing) {
    LOG(WARNING) << "Failed to load pairing information: " << error_message
                 << " (" << error_code << ").";
    return PairingRegistry::Pairing();
  }

  return PairingRegistry::Pairing::CreateFromValue(*pairing);
}

bool PairingRegistryDelegateLinux::Save(
    const PairingRegistry::Pairing& pairing) {
  base::FilePath registry_path = GetRegistryPath();
  base::PlatformFileError error;
  if (!file_util::CreateDirectoryAndGetError(registry_path, &error)) {
    LOG(ERROR) << "Could not create pairing registry directory: " << error;
    return false;
  }

  std::string pairing_json;
  JSONStringValueSerializer serializer(&pairing_json);
  if (!serializer.Serialize(*pairing.ToValue())) {
    LOG(ERROR) << "Failed to serialize pairing data for "
               << pairing.client_id();
    return false;
  }

  base::FilePath pairing_file = registry_path.Append(
      base::StringPrintf(kPairingFilenameFormat, pairing.client_id().c_str()));
  if (!base::ImportantFileWriter::WriteFileAtomically(pairing_file,
                                                      pairing_json)) {
    LOG(ERROR) << "Could not save pairing data for " << pairing.client_id();
    return false;
  }

  return true;
}

bool PairingRegistryDelegateLinux::Delete(const std::string& client_id) {
  base::FilePath registry_path = GetRegistryPath();
  base::FilePath pairing_file = registry_path.Append(
      base::StringPrintf(kPairingFilenameFormat, client_id.c_str()));

  return base::DeleteFile(pairing_file, false);
}

base::FilePath PairingRegistryDelegateLinux::GetRegistryPath() {
  if (!registry_path_for_testing_.empty()) {
    return registry_path_for_testing_;
  }

  base::FilePath config_dir = remoting::GetConfigDir();
  return config_dir.Append(kRegistryDirectory);
}

void PairingRegistryDelegateLinux::SetRegistryPathForTesting(
    const base::FilePath& registry_path) {
  registry_path_for_testing_ = registry_path;
}


scoped_ptr<PairingRegistry::Delegate> CreatePairingRegistryDelegate() {
  return scoped_ptr<PairingRegistry::Delegate>(
      new PairingRegistryDelegateLinux());
}

}  // namespace remoting
