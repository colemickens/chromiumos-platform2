// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/nvram_boot_lockbox.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/file_utils.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_utility.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

namespace cryptohome {

NVRamBootLockbox::NVRamBootLockbox(
  TPMNVSpaceUtilityInterface* tpm_nvspace_utility)
    : boot_lockbox_filepath_(base::FilePath(kNVRamBootLockboxFilePath)),
      tpm_nvspace_utility_(tpm_nvspace_utility) {}

NVRamBootLockbox::NVRamBootLockbox(
    TPMNVSpaceUtilityInterface* tpm_nvspace_utility,
    const base::FilePath& bootlockbox_file_path)
      : boot_lockbox_filepath_(bootlockbox_file_path),
        tpm_nvspace_utility_(tpm_nvspace_utility) {}

NVRamBootLockbox::~NVRamBootLockbox() {}

bool NVRamBootLockbox::Store(const std::string& key,
                             const std::string& digest) {
  if (nvspace_state_ == NVSpaceState::kNVSpaceWriteLocked) {
    LOG(WARNING) << "NV space is already locked.";
    return false;
  }

  // A temporaray key value map for wirtting.
  KeyValueMap updated_key_value_map = key_value_store_;
  updated_key_value_map[key] = digest;

  if (!FlushAndUpdate(updated_key_value_map)) {
    LOG(ERROR) << "Store Failed: Cannot flush to file.";
    return false;
  }
  return true;
}

bool NVRamBootLockbox::Read(const std::string& key, std::string* digest) {
  KeyValueMap::const_iterator it = key_value_store_.find(key);
  if (it == key_value_store_.end()) {
    return false;
  }
  *digest = it->second;
  return true;
}

bool NVRamBootLockbox::Finalize() {
  if (tpm_nvspace_utility_->LockNVSpace()) {
    nvspace_state_ = NVSpaceState::kNVSpaceWriteLocked;
    return true;
  }
  return false;
}

bool NVRamBootLockbox::DefineSpace() {
  if (tpm_nvspace_utility_->DefineNVSpace()) {
    nvspace_state_ = NVSpaceState::kNVSpaceUninitialized;
    return true;
  }
  return false;
}

bool NVRamBootLockbox::Load() {
  if (!tpm_nvspace_utility_->ReadNVSpace(&root_digest_, &nvspace_state_)) {
    LOG(ERROR) << "Failed to read NVRAM space.";
    return false;
  }

  brillo::Blob data;
  if (!Platform().ReadFile(boot_lockbox_filepath_, &data)) {
    LOG(ERROR) << "Failed to read boot lockbox file.";
    return false;
  }

  brillo::SecureBlob digest_blob = CryptoLib::Sha256(data);
  std::string digest(digest_blob.begin(), digest_blob.end());

  if (digest != root_digest_) {
    LOG(ERROR) << "The nvram boot lockbox file verification failed.";
    return false;
  }

  SerializedKeyValueMap message;
  if (!message.ParseFromArray(data.data(), data.size())) {
    LOG(ERROR) << "Failed to parse boot lockbox file.";
    return false;
  }

  if (!message.has_version() || message.version() != kVersion) {
    LOG(ERROR) << "Unsupported version " << message.version();
    return false;
  }

  KeyValueMap tmp(message.keyvals().begin(), message.keyvals().end());
  key_value_store_.swap(tmp);
  return true;
}

bool NVRamBootLockbox::FlushAndUpdate(const KeyValueMap& keyvals) {
  SerializedKeyValueMap message;
  message.set_version(kVersion);

  auto mutable_map = message.mutable_keyvals();
  KeyValueMap::const_iterator it;
  for (it = keyvals.begin(); it != keyvals.end(); ++it) {
    (*mutable_map)[it->first] = it->second;
  }

  brillo::Blob content(message.ByteSize());
  message.SerializeWithCachedSizesToArray(content.data());

  brillo::SecureBlob digest_blob = CryptoLib::Sha256(content);
  std::string digest(digest_blob.begin(), digest_blob.end());

  // It is hard to make this atomic. In the case the file digest
  // and NVRAM space content are inconsistent, the file is deleted and NVRAM
  // space is updated on write.
  if (!brillo::WriteBlobToFileAtomic(boot_lockbox_filepath_,
                                     content, 0600)) {
    LOG(ERROR) << "Failed to write to boot lockbox file";
    return false;
  }
  // Update tpm_nvram.
  if (!tpm_nvspace_utility_->WriteNVSpace(digest)) {
    LOG(ERROR) << "Failed to write boot lockbox NVRAM space";
    return false;
  }
  nvspace_state_ = NVSpaceState::kNVSpaceNormal;

  brillo::SyncFileOrDirectory(boot_lockbox_filepath_,
                              false /* is directory */, true /* data sync */);

  key_value_store_ = keyvals;
  root_digest_ = digest;
  return true;
}

NVSpaceState NVRamBootLockbox::GetState() {
  return nvspace_state_;
}

}  // namespace cryptohome
