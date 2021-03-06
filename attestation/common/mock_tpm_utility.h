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

#ifndef ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_
#define ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_

#include "attestation/common/tpm_utility.h"

#include <stdint.h>

#include <string>

#include <gmock/gmock.h>

namespace attestation {

class MockTpmUtility : public TpmUtility {
 public:
  MockTpmUtility();
  ~MockTpmUtility() override;
  // By default this class will fake seal/unbind/sign operations by passing the
  // input through Transform(<method>). E.g. The expected output of a fake Sign
  // operation on "foo" can be computed by calling
  // MockTpmUtility::Transform("Sign", "foo").
  static std::string Transform(const std::string& method,
                               const std::string& input);

  MOCK_METHOD0(Initialize, bool());
  MOCK_METHOD0(GetVersion, TpmVersion());
  MOCK_METHOD0(IsTpmReady, bool());
  MOCK_METHOD6(ActivateIdentity,
               bool(const std::string&,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    std::string*));
  MOCK_METHOD6(ActivateIdentityForTpm2,
               bool(KeyType key_type,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    std::string*));
  MOCK_METHOD9(CreateCertifiedKey,
               bool(KeyType,
                    KeyUsage,
                    const std::string&,
                    const std::string&,
                    std::string*,
                    std::string*,
                    std::string*,
                    std::string*,
                    std::string*));
  MOCK_METHOD2(SealToPCR0, bool(const std::string&, std::string*));
  MOCK_METHOD2(Unseal, bool(const std::string&, std::string*));
  MOCK_METHOD2(GetEndorsementPublicKey, bool(KeyType, std::string*));
  MOCK_METHOD2(GetEndorsementPublicKeyModulus, bool(KeyType, std::string*));
  MOCK_METHOD2(GetEndorsementCertificate,
               bool(KeyType, std::string*));
  MOCK_METHOD3(Unbind,
               bool(const std::string&, const std::string&, std::string*));
  MOCK_METHOD3(Sign,
               bool(const std::string&, const std::string&, std::string*));
  MOCK_METHOD4(CreateRestrictedKey,
               bool(KeyType,
                    KeyUsage,
                    std::string*,
                    std::string*));
  MOCK_METHOD5(QuotePCR,
               bool(uint32_t,
                    const std::string&,
                    std::string*,
                    std::string*,
                    std::string*));
  MOCK_METHOD5(CertifyNV,
               bool(uint32_t,
                    int,
                    const std::string&,
                    std::string*,
                    std::string*));
  MOCK_CONST_METHOD2(IsQuoteForPCR, bool(const std::string&, uint32_t));
  MOCK_CONST_METHOD2(ReadPCR, bool(uint32_t, std::string*));
  MOCK_METHOD2(GetRSAPublicKeyFromTpmPublicKey,
               bool(const std::string&, std::string*));
  MOCK_METHOD0(RemoveOwnerDependency, bool());
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_MOCK_TPM_UTILITY_H_
