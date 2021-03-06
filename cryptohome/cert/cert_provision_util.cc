// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Utility classes for cert_provision library.

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "cryptohome/cert/cert_provision_util.h"

namespace {

#if OPENSSL_VERSION_NUMBER < 0x10100000L
static inline void RSA_get0_key(const RSA *rsa, const BIGNUM **n,
                                const BIGNUM **e, const BIGNUM **d) {
  if (n != nullptr) *n = rsa->n;
  if (e != nullptr) *e = rsa->e;
  if (d != nullptr) *d = rsa->d;
}
#endif

}  // namespace

namespace cert_provision {

void ProgressReporter::Step(const std::string& message) {
  VLOG(1) << "Step " << cur_step_ << "/" << total_steps_ << ": " << message;
  Report(Status::Success, cur_step_, total_steps_, message);
  if (cur_step_ < total_steps_) {
    cur_step_++;
  }
}

void ProgressReporter::Report(Status status,
                              int cur_step,
                              int total_steps,
                              const std::string& message) {
  int progress;

  if (cur_step == 0) {
    progress = 0;
  } else if (cur_step >= total_steps) {
    progress = 100;
  } else {
    progress = (cur_step * 100) / total_steps;
  }
  callback_.Run(status, progress, message);
}

std::string GetKeyID(const brillo::SecureBlob& public_key) {
  const unsigned char* ptr = public_key.data();
  crypto::ScopedRSA rsa(d2i_RSA_PUBKEY(NULL, &ptr, public_key.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return std::string();
  }

  brillo::SecureBlob modulus(RSA_size(rsa.get()));
  const BIGNUM* rsa_n;
  RSA_get0_key(rsa.get(), &rsa_n, NULL, NULL);
  int len = BN_bn2bin(rsa_n, modulus.data());
  if (len <= 0) {
    LOG(ERROR) << "Failed to extract public key modulus.";
    return std::string();
  }
  modulus.resize(len);

  SHA_CTX sha_context;
  unsigned char md_value[SHA_DIGEST_LENGTH];
  SHA1_Init(&sha_context);
  SHA1_Update(&sha_context, modulus.data(), modulus.size());
  SHA1_Final(md_value, &sha_context);

  return std::string(reinterpret_cast<char*>(md_value), arraysize(md_value));
}

}  // namespace cert_provision
