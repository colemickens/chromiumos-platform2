// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tpm - class for performing encryption/decryption in the TPM.  For cryptohome,
// the TPM may be used as a way to strenghten the security of the wrapped vault
// keys stored on disk.  When the TPM is enabled, there is a system-wide
// cryptohome RSA key that is used during the encryption/decryption of these
// keys.
// TODO(wad) make more functions virtual for use in mock_tpm.h.

#include <base/file_util.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/synchronization/lock.h>
#include <base/values.h>
#include <chromeos/utility.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>

#include "platform.h"
#include "secure_blob.h"
#include "tpm_status.pb.h"

#ifndef CRYPTOHOME_TPM_H_
#define CRYPTOHOME_TPM_H_

namespace cryptohome {

class Crypto;
class TpmInit;

extern const TSS_UUID kCryptohomeWellKnownUuid;

class Tpm {
 public:
  enum TpmRetryAction {
    RetryNone,
    RetryCommFailure,
    RetryDefendLock,
    Fatal
  };

  struct TpmStatusInfo {
    bool Enabled;
    bool BeingOwned;
    bool Owned;
    TSS_RESULT LastTpmError;
    bool CanConnect;
    bool CanLoadSrk;
    bool CanLoadSrkPublicKey;
    bool HasCryptohomeKey;
    bool CanEncrypt;
    bool CanDecrypt;
    bool ThisInstanceHasContext;
    bool ThisInstanceHasKeyHandle;
  };

  static Tpm* GetSingleton();

  virtual ~Tpm();

  // Initializes the Tpm instance
  //
  // Parameters
  //   crypto - The Crypto instance to use (for generating an RSA key, entropy,
  //            etc.)
  //   open_key - Whether or not to open (load) the cryptohome TPM key
  virtual bool Init(Crypto* crypto, Platform* platform, bool open_key);

  // Read a status bit from the TPM
  //
  // Parameters
  //   status_file - The special file to read the status bit from
  virtual bool GetStatusBit(const char* status_file);

  // Tries to connect to the TPM
  virtual bool Connect(TpmRetryAction* retry_action);

  // Returns true if this instance is connected to the TPM
  virtual bool IsConnected();

  // Disconnects from the TPM
  virtual void Disconnect();

  // Encrypts a data blob using the TPM cryptohome RSA key
  //
  // Parameters
  //   data - The data to encrypt (must be small enough to fit into a single RSA
  //          decrypt--Tpm does not do blocking)
  //   password - The password to use as the UP in the method described above
  //   password_rounds - The number of hash rounds to use creating a key from
  //                     the password
  //   salt - The salt used in converting the password to the UP
  //   data_out (OUT) - The encrypted data
  virtual bool Encrypt(const chromeos::Blob& data,
                       const chromeos::Blob& password,
                       unsigned int password_rounds,
                       const chromeos::Blob& salt,
                       SecureBlob* data_out,
                       TpmRetryAction* retry_action);

  // Decrypts a data blob using the TPM cryptohome RSA key
  //
  // Parameters
  //   data - The encrypted data
  //   password - The password to use as the UP in the method described above
  //   password_rounds - The number of hash rounds to use creating a key from
  //                     the password
  //   salt - The salt used in converting the password to the UP
  //   data_out (OUT) - The decrypted data
  virtual bool Decrypt(const chromeos::Blob& data,
                       const chromeos::Blob& password,
                       unsigned int password_rounds,
                       const chromeos::Blob& salt,
                       SecureBlob* data_out,
                       TpmRetryAction* retry_action);

  // Returns the maximum number of RSA keys that the TPM can hold simultaneously
  unsigned int GetMaxRsaKeyCount();

  // Retrieves the TPM-wrapped cryptohome RSA key
  bool GetKey(SecureBlob* blob, TpmRetryAction* retry_action);
  // Retrieves the Public key component of the cryptohome RSA key
  bool GetPublicKey(SecureBlob* blob, TpmRetryAction* retry_action);
  // Loads the cryptohome RSA key from the specified TPM-wrapped key
  bool LoadKey(const SecureBlob& blob, TpmRetryAction* retry_action);

  // Gets the TPM status information
  //
  // Parameters
  //   check_crypto - Whether to check if encrypt/decrypt works (may take
  //                  longer)
  //   status (OUT) - The TpmStatusInfo structure containing the results
  void GetStatus(bool check_crypto, Tpm::TpmStatusInfo* status);

  // Gets the TPM status information as a Value.
  //
  // Parameters
  //   init - If non-NULL, overrides some of the status values
  Value* GetStatusValue(TpmInit* init);

  // Returns the owner password if this instance was used to take ownership.
  // This will only occur when the TPM is unowned, which will be on OOBE
  //
  // Parameters
  //   owner_password (OUT) - The random owner password used
  virtual bool GetOwnerPassword(chromeos::Blob* owner_password);

  // Clears the owner password from storage
  void ClearStoredOwnerPassword();

  // Returns whether or not the TPM is enabled.  This method call returns a
  // cached result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsEnabled() const { return !is_disabled_; }

  // Returns whether or not the TPM is owned.  This method call returns a cached
  // result because querying the TPM directly will block if ownership is
  // currently being taken (such as on a separate thread).
  virtual bool IsOwned() const { return is_owned_; }

  // Returns whether or not the SRK is available
  bool IsSrkAvailable() const { return is_srk_available_; }

  // Returns whether or not the TPM is being owned
  bool IsBeingOwned() const { return is_being_owned_; }

  // Runs the TPM initialization sequence.  This may take a long time due to the
  // call to Tspi_TPM_TakeOwnership.
  bool InitializeTpm(bool* OUT_took_ownership);

  // Gets random bytes from the TPM
  //
  // Parameters
  //   length - The number of bytes to get
  //   data (OUT) - The random data from the TPM
  virtual bool GetRandomData(size_t length, chromeos::Blob* data);

  // Creates a lockable NVRAM space in the TPM
  //
  // Parameters
  //   index - The index of the space
  //   length - The number of bytes to allocate
  //   flags - any space-specific flags (currently ignored)
  // TODO(wad) Add PCR compositing via flags.
  // TODO(wad) Should this just be a factory for a TpmNvram class?
  // Returns false if the index, length, or flags are invalid or the required
  // authorization is not possible.
  virtual bool DefineLockOnceNvram(uint32_t index,
                                   size_t length,
                                   uint32_t flags);

  // Destroys a defined NVRAM space
  //
  // Parameters
  //  index - The index of the space to destroy
  // Returns false if the index is invalid or the required authorization
  // is not possible.
  virtual bool DestroyNvram(uint32_t index);

  // Writes the given blob to NVRAM
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to write (size==0 may be used for locking)
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool WriteNvram(uint32_t index, const SecureBlob& blob);

  // Reads from the NVRAM index to the given blob
  //
  // Parameters
  //  index - The index of the space to write
  //  blob - the data to read
  // Returns false if the index is invalid or the request lacks the required
  // authorization.
  virtual bool ReadNvram(uint32_t index, SecureBlob* blob);

  // Determines if the given index is defined in the TPM
  //
  // Parameters
  //  index - The index of the space
  // Returns true if it exists and false if it doesn't or there is a failure to
  // communicate with the TPM.
  virtual bool IsNvramDefined(uint32_t index);

  // Determines if the NVRAM space at the given index is bWriteDefine locked
  //
  // Parameters
  //   index - The index of the space
  // Returns true if locked and false if it is unlocked, the space does not
  // exist, or there is a TPM-related error.
  virtual bool IsNvramLocked(uint32_t index);

  // Returns the reported size of the NVRAM space indicated by its index
  //
  // Parameters
  //   index - The index of the space
  // Returns the size of the space. If undefined or an error occurs, 0 is
  // returned.
  virtual unsigned int GetNvramSize(uint32_t index);

  void set_srk_auth(const SecureBlob& value) {
    srk_auth_.resize(value.size());
    memcpy(srk_auth_.data(), value.const_data(), srk_auth_.size());
  }

  void set_crypto(Crypto* value) {
    crypto_ = value;
  }

  void set_rsa_key_bits(unsigned int value) {
    rsa_key_bits_ = value;
  }

  void set_key_file(const std::string& value) {
    key_file_ = value;
  }

 protected:
  // Default constructor
  Tpm();

 private:
  bool IsTransient(TSS_RESULT result);

  TpmRetryAction HandleError(TSS_RESULT result);

  bool SaveCryptohomeKey(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                         TSS_RESULT* result);

  bool OpenAndConnectTpm(TSS_HCONTEXT* context_handle, TSS_RESULT* result);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the owner password is available and
  // authorization is successfully acquired.
  bool ConnectContextAsOwner(TSS_HCONTEXT* context_handle,
                             TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS.
  bool ConnectContextAsUser(TSS_HCONTEXT* context_handle,
                            TSS_HTPM* tpm_handle);

  bool CreateCryptohomeKey(TSS_HCONTEXT context_handle,
                           bool create_in_tpm, TSS_RESULT* result);

  bool LoadCryptohomeKey(TSS_HCONTEXT context_handle, TSS_HKEY* key_handle,
                         TSS_RESULT* result);

  bool LoadOrCreateCryptohomeKey(TSS_HCONTEXT context_handle,
                                 bool create_in_tpm,
                                 TSS_HKEY* key_handle,
                                 TSS_RESULT* result);

  bool EncryptBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                   const chromeos::Blob& data, const chromeos::Blob& password,
                   unsigned int password_rounds, const chromeos::Blob& salt,
                   SecureBlob* data_out, TSS_RESULT* result);

  bool DecryptBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                   const chromeos::Blob& data, const chromeos::Blob& password,
                   unsigned int password_rounds, const chromeos::Blob& salt,
                   SecureBlob* data_out, TSS_RESULT* result);

  bool GetKeyBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                  SecureBlob* data_out, TSS_RESULT* result);

  bool GetPublicKeyBlob(TSS_HCONTEXT context_handle, TSS_HKEY key_handle,
                        SecureBlob* data_out, TSS_RESULT* result);

  bool LoadKeyBlob(TSS_HCONTEXT context_handle, const SecureBlob& blob,
                   TSS_HKEY* key_handle, TSS_RESULT* result);

 private:
  // Tries to connect to the TPM
  virtual TSS_HCONTEXT ConnectContext();

  // Disconnects from the TPM
  virtual void DisconnectContext(TSS_HCONTEXT context_handle);

  // Gets a handle to the SRK
  bool LoadSrk(TSS_HCONTEXT context_handle, TSS_HKEY* srk_handle,
               TSS_RESULT* result);

  // Stores the TPM owner password to the TpmStatus object
  bool StoreOwnerPassword(const chromeos::Blob& owner_password,
                          TpmStatus* tpm_status);

  // Retrieves the TPM owner password
  bool LoadOwnerPassword(const TpmStatus& tpm_status,
                         chromeos::Blob* owner_password);

  // Loads the TpmStatus object
  bool LoadTpmStatus(TpmStatus* serialized);

  // Saves the TpmStatus object
  bool StoreTpmStatus(const TpmStatus& serialized);

  // Returns the maximum simultaneously-loaded RSA key count for the TPM
  // specified by the context handle
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  unsigned int GetMaxRsaKeyCountForContext(TSS_HCONTEXT context_handle);

  // Returns the size of the specified NVRAM space.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   index - NVRAM Space index
  // Returns -1 if the index, handle, or space is invalid.
  unsigned int GetNvramSizeForContext(TSS_HCONTEXT context_handle,
                                      TSS_HTPM tpm_handle,
                                      uint32_t index);

  // Returns if an Nvram space exists using the given context.
  bool IsNvramDefinedForContext(TSS_HCONTEXT context_handle,
                                TSS_HTPM tpm_handle,
                                uint32_t index);

  // Returns if bWriteDefine is true for a given NVRAM space using the given
  // context.
  bool IsNvramLockedForContext(TSS_HCONTEXT context_handle,
                               TSS_HTPM tpm_handle,
                               uint32_t index);

  // Returns whether or not the TPM is disabled by checking a flag in the TPM's
  // entry in /sys/class/misc
  bool IsDisabledCheckViaSysfs();

  // Returns whether or not the TPM is owned by checking a flag in the TPM's
  // entry in /sys/class/misc
  bool IsOwnedCheckViaSysfs();

  // Returns whether or not the TPM is enabled and owned using a call to
  // Tspi_TPM_GetCapability
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   enabled (OUT) - Whether the TPM is enabled
  //   owned (OUT) - Whether the TPM is owned
  void IsEnabledOwnedCheckViaContext(TSS_HCONTEXT context_handle,
                                     bool* enabled, bool* owned);

  // Attempts to create the endorsement key in the TPM
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  bool CreateEndorsementKey(TSS_HCONTEXT context_handle);

  // Delegates ownership authority
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  bool DelegateTpmOwnership(TSS_HCONTEXT context_handle, TSS_HTPM tpm_handle,
                            SecureBlob* delegation_blob);

  // Checks to see if the endorsement key is available by attempting to get its
  // public key
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  bool IsEndorsementKeyAvailable(TSS_HCONTEXT context_handle);

  // Creates a random owner password
  //
  // Parameters
  //   password (OUT) - the generated password
  void CreateOwnerPassword(SecureBlob* password);

  // Attempts to take ownership of the TPM
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   max_timeout_tries - The maximum number of attempts to make if the call
  //                       times out, which it may occasionally do
  bool TakeOwnership(TSS_HCONTEXT context_handle, int max_timeout_tries,
                     const SecureBlob& owner_password);

  // Zeros the SRK password (sets it to an empty string)
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool ZeroSrkPassword(TSS_HCONTEXT context_handle,
                       const SecureBlob& owner_password);

  // Removes usage restrictions on the SRK
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password for the TPM
  bool UnrestrictSrk(TSS_HCONTEXT context_handle,
                     const SecureBlob& owner_password);

  // Changes the owner password
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   previous_owner_password - The previous owner password for the TPM
  //   owner_password - The owner password for the TPM
  bool ChangeOwnerPassword(TSS_HCONTEXT context_handle,
                           const SecureBlob& previous_owner_password,
                           const SecureBlob& owner_password);

  // Gets a handle to the TPM from the specified context
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle);

  // Gets a handle to the TPM from the specified context with the given owner
  // password
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password to use when getting the handle
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpmWithAuth(TSS_HCONTEXT context_handle,
                      const SecureBlob& owner_password,
                      TSS_HTPM* tpm_handle);

  // Test the TPM auth by calling Tspi_TPM_GetStatus
  //
  // Parameters
  //   tpm_handle = The TPM handle
  bool TestTpmAuth(TSS_HTPM tpm_handle);

  // Member variables
  bool initialized_;
  unsigned int rsa_key_bits_;
  SecureBlob srk_auth_;
  Crypto* crypto_;
  TSS_HCONTEXT context_handle_;
  TSS_HKEY key_handle_;
  std::string key_file_;

  // If TPM ownership is taken, owner_password_ contains the password used
  SecureBlob owner_password_;

  // Used to provide thread-safe access to owner_password_, as it is set in the
  // initialization background thread.
  base::Lock password_sync_lock_;

  // Indicates if the TPM is disabled
  bool is_disabled_;

  // Indicates if the TPM is owned
  bool is_owned_;

  // Indicates if the SRK is available
  bool is_srk_available_;

  // Indicates if the TPM is being owned
  bool is_being_owned_;

  static Tpm* singleton_;
  static base::Lock singleton_lock_;

  Platform* platform_;

  DISALLOW_COPY_AND_ASSIGN(Tpm);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_H_
