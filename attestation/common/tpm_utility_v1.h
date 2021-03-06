//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ATTESTATION_COMMON_TPM_UTILITY_V1_H_
#define ATTESTATION_COMMON_TPM_UTILITY_V1_H_

#include "attestation/common/tpm_utility.h"

#include <stdint.h>

#include <string>

#include <base/macros.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>

namespace attestation {

// A TpmUtility implementation for TPM v1.2 modules.
class TpmUtilityV1 : public TpmUtility {
 public:
  TpmUtilityV1() = default;
  ~TpmUtilityV1() override;

  // TpmUtility methods.
  bool Initialize() override;
  TpmVersion GetVersion() override { return TPM_1_2; }
  bool IsTpmReady() override;
  bool ActivateIdentity(const std::string& delegate_blob,
                        const std::string& delegate_secret,
                        const std::string& identity_key_blob,
                        const std::string& asym_ca_contents,
                        const std::string& sym_ca_attestation,
                        std::string* credential) override;
  bool ActivateIdentityForTpm2(KeyType key_type,
                               const std::string& identity_key_blob,
                               const std::string& encrypted_seed,
                               const std::string& credential_mac,
                               const std::string& wrapped_credential,
                               std::string* credential) override;
  bool CreateCertifiedKey(KeyType key_type,
                          KeyUsage key_usage,
                          const std::string& identity_key_blob,
                          const std::string& external_data,
                          std::string* key_blob,
                          std::string* public_key,
                          std::string* public_key_tpm_format,
                          std::string* key_info,
                          std::string* proof) override;
  bool SealToPCR0(const std::string& data, std::string* sealed_data) override;
  bool Unseal(const std::string& sealed_data, std::string* data) override;
  bool GetEndorsementPublicKey(KeyType key_type,
                               std::string* public_key) override;
  bool GetEndorsementCertificate(KeyType key_type,
                                 std::string* certificate) override;
  bool Unbind(const std::string& key_blob,
              const std::string& bound_data,
              std::string* data) override;
  bool Sign(const std::string& key_blob,
            const std::string& data_to_sign,
            std::string* signature) override;
  bool CreateRestrictedKey(KeyType key_type,
                           KeyUsage key_usage,
                           std::string* public_key,
                           std::string* private_key_blob) override;
  bool QuotePCR(uint32_t pcr_index,
                const std::string& key_blob,
                std::string* quoted_pcr_value,
                std::string* quoted_data,
                std::string* quote) override;
  bool IsQuoteForPCR(const std::string& quote,
                     uint32_t pcr_index) const override;
  bool CertifyNV(uint32_t nv_index,
                 int nv_size,
                 const std::string& key_blob,
                 std::string* quoted_data,
                 std::string* quote) override;
  bool ReadPCR(uint32_t pcr_index, std::string* pcr_value) const override;
  bool GetRSAPublicKeyFromTpmPublicKey(const std::string& tpm_public_key_object,
                                       std::string* public_key_der) override;
  bool RemoveOwnerDependency() override;
  bool GetEndorsementPublicKeyModulus(KeyType key_type,
                                      std::string* ekm) override;

 private:
  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS. Returns true on success.
  bool ConnectContext(trousers::ScopedTssContext* context_handle,
                      TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object authorized by the given |delegate_blob| and
  // |delegate_secret|. Returns true on success.
  bool ConnectContextAsDelegate(const std::string& delegate_blob,
                                const std::string& delegate_secret,
                                trousers::ScopedTssContext* context,
                                TSS_HTPM* tpm);

  // Sets up srk_handle_ if necessary. Returns true iff the SRK is ready.
  bool SetupSrk();

  // Loads the storage root key (SRK) and populates |srk_handle|. The
  // |context_handle| must be connected and valid. Returns true on success.
  bool LoadSrk(TSS_HCONTEXT context_handle, trousers::ScopedTssKey* srk_handle);

  // Loads a key in the TPM given a |key_blob| and a |parent_key_handle|. The
  // |context_handle| must be connected and valid. Returns true and populates
  // |key_handle| on success.
  bool LoadKeyFromBlob(const std::string& key_blob,
                       TSS_HCONTEXT context_handle,
                       TSS_HKEY parent_key_handle,
                       trousers::ScopedTssKey* key_handle);

  // Retrieves a |data| attribute defined by |flag| and |sub_flag| from a TSS
  // |object_handle|. The |context_handle| is only used for TSS memory
  // management.
  bool GetDataAttribute(TSS_HCONTEXT context_handle,
                        TSS_HOBJECT object_handle,
                        TSS_FLAG flag,
                        TSS_FLAG sub_flag,
                        std::string* data);

  bool is_ready_{false};
  trousers::ScopedTssContext context_handle_;
  TSS_HTPM tpm_handle_{0};
  trousers::ScopedTssKey srk_handle_{0};

  DISALLOW_COPY_AND_ASSIGN(TpmUtilityV1);
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_TPM_UTILITY_V1_H_
