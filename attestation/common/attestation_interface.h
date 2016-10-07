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

#ifndef ATTESTATION_COMMON_ATTESTATION_INTERFACE_H_
#define ATTESTATION_COMMON_ATTESTATION_INTERFACE_H_

#include <string>

#include <base/callback_forward.h>

#include "attestation/common/interface.pb.h"

namespace attestation {

// The main attestation interface implemented by proxies and services. The
// anticipated flow looks like this:
//   [APP] -> AttestationInterface -> [IPC] -> AttestationInterface
class AttestationInterface {
 public:
  virtual ~AttestationInterface() = default;

  // Performs initialization tasks that may take a long time. This method must
  // be successfully called before calling any other method. Returns true on
  // success.
  virtual bool Initialize() = 0;

  // Processes a CreateGoogleAttestedKeyRequest and responds with a
  // CreateGoogleAttestedKeyReply.
  using CreateGoogleAttestedKeyCallback =
      base::Callback<void(const CreateGoogleAttestedKeyReply&)>;
  virtual void CreateGoogleAttestedKey(
      const CreateGoogleAttestedKeyRequest& request,
      const CreateGoogleAttestedKeyCallback& callback) = 0;

  // Processes a GetKeyInfoRequest and responds with a GetKeyInfoReply.
  using GetKeyInfoCallback = base::Callback<void(const GetKeyInfoReply&)>;
  virtual void GetKeyInfo(const GetKeyInfoRequest& request,
                          const GetKeyInfoCallback& callback) = 0;

  // Processes a GetEndorsementInfoRequest and responds with a
  // GetEndorsementInfoReply.
  using GetEndorsementInfoCallback =
      base::Callback<void(const GetEndorsementInfoReply&)>;
  virtual void GetEndorsementInfo(
      const GetEndorsementInfoRequest& request,
      const GetEndorsementInfoCallback& callback) = 0;

  // Processes a GetAttestationKeyInfoRequest and responds with a
  // GetAttestationKeyInfoReply.
  using GetAttestationKeyInfoCallback =
      base::Callback<void(const GetAttestationKeyInfoReply&)>;
  virtual void GetAttestationKeyInfo(
      const GetAttestationKeyInfoRequest& request,
      const GetAttestationKeyInfoCallback& callback) = 0;

  // Processes a ActivateAttestationKeyRequest and responds with a
  // ActivateAttestationKeyReply.
  using ActivateAttestationKeyCallback =
      base::Callback<void(const ActivateAttestationKeyReply&)>;
  virtual void ActivateAttestationKey(
      const ActivateAttestationKeyRequest& request,
      const ActivateAttestationKeyCallback& callback) = 0;

  // Processes a CreateCertifiableKeyRequest and responds with a
  // CreateCertifiableKeyReply.
  using CreateCertifiableKeyCallback =
      base::Callback<void(const CreateCertifiableKeyReply&)>;
  virtual void CreateCertifiableKey(
      const CreateCertifiableKeyRequest& request,
      const CreateCertifiableKeyCallback& callback) = 0;

  // Processes a DecryptRequest and responds with a DecryptReply.
  using DecryptCallback = base::Callback<void(const DecryptReply&)>;
  virtual void Decrypt(const DecryptRequest& request,
                       const DecryptCallback& callback) = 0;

  // Processes a SignRequest and responds with a SignReply.
  using SignCallback = base::Callback<void(const SignReply&)>;
  virtual void Sign(const SignRequest& request,
                    const SignCallback& callback) = 0;

  // Processes a RegisterKeyWithChapsTokenRequest and responds with a
  // RegisterKeyWithChapsTokenReply.
  using RegisterKeyWithChapsTokenCallback =
      base::Callback<void(const RegisterKeyWithChapsTokenReply&)>;
  virtual void RegisterKeyWithChapsToken(
      const RegisterKeyWithChapsTokenRequest& request,
      const RegisterKeyWithChapsTokenCallback& callback) = 0;

  // Processes a GetStatusRequest and responds with a
  // GetStatusReply.
  using GetStatusCallback =
      base::Callback<void(const GetStatusReply&)>;
  virtual void GetStatus(
      const GetStatusRequest& request,
      const GetStatusCallback& callback) = 0;

  // Processes a VerifyRequest and responds with a
  // VerifyReply.
  using VerifyCallback =
      base::Callback<void(const VerifyReply&)>;
  virtual void Verify(
      const VerifyRequest& request,
      const VerifyCallback& callback) = 0;

  // Processes a CreateEnrollRequestRequest and responds with a
  // CreateEnrollRequestReply.
  using CreateEnrollRequestCallback =
      base::Callback<void(const CreateEnrollRequestReply&)>;
  virtual void CreateEnrollRequest(
      const CreateEnrollRequestRequest& request,
      const CreateEnrollRequestCallback& callback) = 0;

  // Processes a FinishEnrollRequest and responds with a
  // FinishEnrollReply.
  using FinishEnrollCallback =
      base::Callback<void(const FinishEnrollReply&)>;
  virtual void FinishEnroll(
      const FinishEnrollRequest& request,
      const FinishEnrollCallback& callback) = 0;

  // Processes a CreateCertificateRequestRequest and responds with a
  // CreateCertificateRequestReply.
  using CreateCertificateRequestCallback =
      base::Callback<void(const CreateCertificateRequestReply&)>;
  virtual void CreateCertificateRequest(
      const CreateCertificateRequestRequest& request,
      const CreateCertificateRequestCallback& callback) = 0;

  // Processes a FinishCertificateRequestRequest and responds with a
  // FinishCertificateRequestReply.
  using FinishCertificateRequestCallback =
      base::Callback<void(const FinishCertificateRequestReply&)>;
  virtual void FinishCertificateRequest(
      const FinishCertificateRequestRequest& request,
      const FinishCertificateRequestCallback& callback) = 0;

  // Processes a SignEnterpriseChallengeRequest and responds with a
  // SignEnterpriseChallengeReply.
  using SignEnterpriseChallengeCallback =
      base::Callback<void(const SignEnterpriseChallengeReply&)>;
  virtual void SignEnterpriseChallenge(
      const SignEnterpriseChallengeRequest& request,
      const SignEnterpriseChallengeCallback& callback) = 0;

  // Processes a SignSimpleChallengeRequest and responds with a
  // SignSimpleChallengeReply.
  using SignSimpleChallengeCallback =
      base::Callback<void(const SignSimpleChallengeReply&)>;
  virtual void SignSimpleChallenge(
      const SignSimpleChallengeRequest& request,
      const SignSimpleChallengeCallback& callback) = 0;

  // Processes a SetKeyPayloadRequest and responds with a
  // SetKeyPayloadReply.
  using SetKeyPayloadCallback =
      base::Callback<void(const SetKeyPayloadReply&)>;
  virtual void SetKeyPayload(
      const SetKeyPayloadRequest& request,
      const SetKeyPayloadCallback& callback) = 0;

  // Processes a DeleteKeysRequest and responds with a
  // DeleteKeysReply.
  using DeleteKeysCallback =
      base::Callback<void(const DeleteKeysReply&)>;
  virtual void DeleteKeys(
      const DeleteKeysRequest& request,
      const DeleteKeysCallback& callback) = 0;

  // Processes a ResetIdentityRequest and responds with a
  // ResetIdentityReply.
  using ResetIdentityCallback =
      base::Callback<void(const ResetIdentityReply&)>;
  virtual void ResetIdentity(
      const ResetIdentityRequest& request,
      const ResetIdentityCallback& callback) = 0;

  // Processes a SetSystemSaltRequest and responds with a
  // SetSystemSaltReply.
  using SetSystemSaltCallback =
      base::Callback<void(const SetSystemSaltReply&)>;
  virtual void SetSystemSalt(
      const SetSystemSaltRequest& request,
      const SetSystemSaltCallback& callback) = 0;

};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_ATTESTATION_INTERFACE_H_
