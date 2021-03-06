// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOLIB_H_
#define CRYPTOHOME_CRYPTOLIB_H_

#include <string>

#include <openssl/bn.h>
#include <openssl/rsa.h>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/secure_blob.h>

#include "attestation.pb.h"  // NOLINT(build/include)

namespace cryptohome {

extern const unsigned int kDefaultPasswordRounds;
extern const unsigned int kWellKnownExponent;
extern const unsigned int kAesBlockSize;

class CryptoLib {
 public:
  CryptoLib();
  ~CryptoLib();

  enum PaddingScheme {
    kPaddingNone = 0,
    // Also called PKCS padding.
    // See http://tools.ietf.org/html/rfc5652#section-6.3.
    kPaddingStandard = 1,
    kPaddingCryptohomeDefault = 2,
  };

  enum BlockMode {
    kEcb = 1,
    kCbc = 2,
  };

  static void GetSecureRandom(unsigned char *bytes, size_t len);
  static bool CreateRsaKey(size_t bits, brillo::SecureBlob* n,
                           brillo::SecureBlob* p);

  // TODO(jorgelo,crbug.com/728047): Review current usage of these functions and
  // consider making the functions that take a plain Blob also return a plain
  // Blob.
  static brillo::SecureBlob Sha1(const brillo::Blob& data);
  static brillo::SecureBlob Sha1(const brillo::SecureBlob& data);

  static brillo::SecureBlob Sha256(const brillo::Blob& data);
  static brillo::SecureBlob Sha256(const brillo::SecureBlob& data);

  static brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                                       const brillo::Blob& data);
  static brillo::SecureBlob HmacSha512(const brillo::SecureBlob& key,
                                       const brillo::SecureBlob& data);

  static brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                                       const brillo::Blob& data);
  static brillo::SecureBlob HmacSha256(const brillo::SecureBlob& key,
                                       const brillo::SecureBlob& data);

  static size_t GetAesBlockSize();
  static bool PasskeyToAesKey(const brillo::Blob& passkey,
                              const brillo::Blob& salt,
                              unsigned int rounds,
                              brillo::SecureBlob* key,
                              brillo::SecureBlob* iv);

  // Decrypts data encrypted with AesEncrypt.
  //
  // Parameters
  //   wrapped - The blob containing the encrypted data
  //   key - The AES key to use in decryption
  //   iv - The initialization vector to use
  //   plaintext - The unwrapped (decrypted) data
  static bool AesDecrypt(const brillo::Blob& ciphertext,
                        const brillo::SecureBlob& key,
                        const brillo::SecureBlob& iv,
                        brillo::SecureBlob* plaintext);

  // AES encrypts the plain text data using the specified key and IV.  This
  // method uses custom padding and is not inter-operable with other crypto
  // systems.  The encrypted data can be decrypted with AesDecrypt.
  //
  // Parameters
  //   plaintext - The plain text data to encrypt
  //   key - The AES key to use
  //   iv - The initialization vector to use
  //   ciphertext - On success, the encrypted data
  static bool AesEncrypt(const brillo::Blob& plaintext,
                         const brillo::SecureBlob& key,
                         const brillo::SecureBlob& iv,
                         brillo::SecureBlob* ciphertext);

  // Same as AesDecrypt, but allows using either CBC or ECB
  static bool AesDecryptSpecifyBlockMode(const brillo::Blob& ciphertext,
                                         unsigned int start, unsigned int count,
                                         const brillo::SecureBlob& key,
                                         const brillo::SecureBlob& iv,
                                         PaddingScheme padding, BlockMode mode,
                                         brillo::SecureBlob* plaintext);

  // Same as AesEncrypt, but allows using either CBC or ECB
  static bool AesEncryptSpecifyBlockMode(const brillo::Blob& plaintext,
                                         unsigned int start, unsigned int count,
                                         const brillo::SecureBlob& key,
                                         const brillo::SecureBlob& iv,
                                         PaddingScheme padding, BlockMode mode,
                                         brillo::SecureBlob* ciphertext);

  // Obscure an RSA message by encrypting part of it.
  // The TPM could _in theory_ produce an RSA message (as a response from Bind)
  // that contains a header of a known format. If it did, and we encrypted the
  // whole message with a passphrase-derived AES key, then one could test
  // passphrase correctness by trial-decrypting the header. Instead, encrypt
  // only part of the message, and hope the part we encrypt is part of the RSA
  // message.
  //
  // In practice, this never makes any difference, because no TPM does that; the
  // result is always a bare PKCS1.5-padded RSA-encrypted message, which is
  // (as far as the author knows, although no proof is known) indistinguishable
  // from random data, and hence the attack this would protect against is
  // infeasible.
  static bool ObscureRSAMessage(const brillo::SecureBlob& plaintext,
                                const brillo::SecureBlob& key,
                                brillo::SecureBlob* ciphertext);
  static bool UnobscureRSAMessage(const brillo::SecureBlob& ciphertext,
                                  const brillo::SecureBlob& key,
                                  brillo::SecureBlob* plaintext);

  // Decrypts the data encrypted with RSA OAEP with the SHA-1 hash function, the
  // MGF1 mask function, and the label parameter equal to |oaep_label|.
  static bool RsaOaepDecrypt(const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& oaep_label,
                             RSA* key,
                             brillo::SecureBlob* plaintext);

  // Encodes a binary blob to hex-ascii. Similar to base::HexEncode but
  // produces lowercase letters for hex digits.
  //
  // Parameters
  //   blob - The binary blob to convert
  static std::string BlobToHex(const brillo::Blob& blob);
  // Parameters
  //   blob - The binary blob to convert
  //   buffer (IN/OUT) - Where to store the converted blob
  //   buffer_length - The size of the buffer
  static void BlobToHexToBuffer(const brillo::Blob& blob,
                                void* buffer,
                                size_t buffer_length);

  // Computes an HMAC over the iv and encrypted_data fields of an EncryptedData
  // protobuf.
  // Parameters
  //   encrypted_data - encrypted data protobuf..
  //   hmac_key - secret key to use in hmac computation.
  static std::string ComputeEncryptedDataHMAC(
      const EncryptedData& encrypted_data,
      const brillo::SecureBlob& hmac_key);

  // Encrypts data using the TPM_ES_RSAESOAEP_SHA1_MGF1 scheme.
  //
  // Parameters
  //   key - The RSA public key.
  //   input - The data to be encrypted.
  //   output - The encrypted data.
  static bool TpmCompatibleOAEPEncrypt(RSA* key,
                                       const brillo::SecureBlob& input,
                                       brillo::SecureBlob* output);

  // Checks an RSA key modulus for the ROCA fingerprint (i.e. whether the RSA
  // modulus has a discrete logarithm modulus small primes). See research paper
  // for details: https://crocs.fi.muni.cz/public/papers/rsa_ccs17
  static bool TestRocaVulnerable(BIGNUM* rsa_modulus);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOLIB_H_
