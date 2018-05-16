// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Interface used by "mount-encrypted" to interface with the TPM.

#ifndef CRYPTOHOME_MOUNT_ENCRYPTED_TPM_H_
#define CRYPTOHOME_MOUNT_ENCRYPTED_TPM_H_

#include <stdint.h>

#include <memory>

#include <base/macros.h>

#include <brillo/secure_blob.h>

#include "cryptohome/mount_encrypted.h"

const uint32_t kLockboxSizeV1 = 0x2c;
const uint32_t kLockboxSizeV2 = 0x45;

#if USE_TPM2
const uint32_t kLockboxIndex = 0x800004;
const uint32_t kEncStatefulIndex = 0x800005;
const uint32_t kEncStatefulSize = 40;
#else
const uint32_t kLockboxIndex = 0x20000004;
const uint32_t kEncStatefulIndex = 0x20000005;
const uint32_t kEncStatefulSize = 0;
#endif

class Tpm;

class NvramSpace {
 public:
  NvramSpace(Tpm* tpm, uint32_t index);

  enum class Status {
    kUnknown,   // Not accessed yet.
    kAbsent,    // Not defined.
    kValid,     // Present and read was successful.
    kTpmError,  // Error accessing the space.
  };

  Status status() const { return status_; }
  bool is_valid() const { return status() == Status::kValid; }
  const brillo::SecureBlob& contents() const { return contents_; }

  // Retrieves the space attributes.
  result_code GetAttributes(uint32_t* attributes);

  // Attempts to read the NVRAM space.
  result_code Read(uint32_t size);

  // Writes to the NVRAM space.
  result_code Write(const brillo::SecureBlob& contents);

  // Sets the read lock on the space.
  result_code ReadLock();

  // Sets write lock on the space.
  result_code WriteLock();

  // Attempt to define the space with the given attributes and size.
  result_code Define(uint32_t attributes, uint32_t size);

 private:
  Tpm* tpm_;
  uint32_t index_;

  // Cached copy of NVRAM space attributes.
  uint32_t attributes_;

  // Cached copy of the data as read from the space.
  brillo::SecureBlob contents_;

  // Cached indicator reflecting the status of the space in the TPM.
  Status status_ = Status::kUnknown;
};

// Encapsulates high-level TPM state and the motions needed to open and close
// the TPM library.
class Tpm {
 public:
  Tpm();
  ~Tpm();

  bool available() const { return available_; }
  bool is_tpm2() const { return is_tpm2_; }

  result_code IsOwned(bool* owned);

  result_code GetRandomBytes(uint8_t* buffer, int wanted);

  // Returns the initialized lockbox NVRAM space.
  NvramSpace* GetLockboxSpace();

  // Get the initialized encrypted stateful space.
  NvramSpace* GetEncStatefulSpace();

 private:
  bool available_ = false;
  bool is_tpm2_ = false;

  bool ownership_checked_ = false;
  bool owned_ = false;

  std::unique_ptr<NvramSpace> lockbox_space_;
  std::unique_ptr<NvramSpace> encstateful_space_;

  DISALLOW_COPY_AND_ASSIGN(Tpm);
};

// The interface used by the key handling logic to access the system key. The
// system key is used to wrap the actual data encryption key.
//
// System keys must have these properties:
//  1. The system key can only be accessed in the current boot mode, i.e.
//     switching to developer mode blocks access or destroys the system key.
//  2. A fresh system key must be generated after clearing the TPM. This can be
//     achieved either by arranging a TPM clear to drop the key or by detecting
//     a TPM clear an generating a fresh key.
//  3. The key should ideally not be accessible for reading after early boot.
//  4. Because mounting the encrypted stateful file system is on the critical
//     boot path, loading the system key must be reasonably fast.
//  5. Fresh keys can be generated with reasonable cost. Costly operations such
//     as taking TPM ownership after each TPM clear to set up fresh NVRAM spaces
//     do not fly performance-wise. The file system encryption key logic has a
//     fallback path to dump its key without protection by a system key until
//     the latter becomes available, but that's a risk that should ideally be
//     avoided.
class SystemKeyLoader {
 public:
  virtual ~SystemKeyLoader() = default;

  // Create a system key loader suitable for the system.
  static std::unique_ptr<SystemKeyLoader> Create(Tpm* tpm);

  // Load the encryption key from TPM NVRAM. Returns true if successful and
  // fills in key, false if the key is not available or there is an error.
  //
  // TODO(mnissler): Remove the migration feature - Chrome OS has supported
  // encrypted stateful for years, the chance that a device that was set up with
  // a Chrome OS version that didn't support stateful encryption will be
  // switching directly to this version of the code is negligibly small.
  virtual result_code Load(brillo::SecureBlob* key, bool* migrate) = 0;

  // Generate a fresh system key but, do not store it in NVRAM yet.
  virtual brillo::SecureBlob Generate() = 0;

  // Persist a previously generated system key in NVRAM. This may not be
  // possible in case the TPM is not in a state where the NVRAM spaces can be
  // manipulated.
  virtual result_code Persist() = 0;

  // Lock the system key to prevent further manipulation.
  virtual void Lock() = 0;
};

#endif  // CRYPTOHOME_MOUNT_ENCRYPTED_TPM_H_