// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/le_credential_manager.h"

#include <fcntl.h>

#include <utility>

#include <base/files/file_util.h>

#include "cryptohome/cryptolib.h"

namespace {

// Location where we store the Hash Tree data structures.
const char kSignInHashTreeDir[] = "/home/.shadow/low_entropy_creds";

}  // namespace

namespace cryptohome {

LECredentialManager::LECredentialManager(LECredentialBackend* le_backend)
    : is_locked_(false), le_tpm_backend_(le_backend) {
  CHECK(le_tpm_backend_);

  // Check if hash tree already exists.
  bool new_hash_tree = !base::PathExists(base::FilePath(kSignInHashTreeDir));

  hash_tree_ = std::make_unique<SignInHashTree>(
      kLengthLabels, kBitsPerLevel, base::FilePath(kSignInHashTreeDir));

  // Reset the root hash in the TPM to its initial value.
  if (new_hash_tree) {
    CHECK(le_tpm_backend_->Reset());
    hash_tree_->GenerateAndStoreHashCache();
  }
}

LECredError LECredentialManager::InsertCredential(
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const DelaySchedule& delay_sched,
    uint64_t* ret_label) {
  if (is_locked_) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  SignInHashTree::Label label = hash_tree_->GetFreeLabel();
  if (!label.is_valid()) {
    LOG(ERROR) << "No free labels available.";
    return LE_CRED_ERROR_NO_FREE_LABEL;
  }

  std::vector<std::vector<uint8_t>> h_aux = GetAuxHashes(label);
  if (h_aux.empty()) {
    LOG(ERROR) << "Error getting aux hashes for label: " << label.value();
    return LE_CRED_ERROR_HASH_TREE;
  }

  std::vector<uint8_t> cred_metadata, mac;
  bool success = le_tpm_backend_->InsertCredential(
      label.value(), h_aux, le_secret, he_secret, reset_secret, delay_sched,
      &cred_metadata, &mac);
  if (!success) {
    LOG(ERROR) << "Error executing TPM InsertCredential command.";
    return LE_CRED_ERROR_HASH_TREE;
  }

  if (!hash_tree_->StoreLabel(label, mac, cred_metadata)) {
    LOG(ERROR)
        << "InsertCredential succeeded in TPM but disk updated failed, label: "
        << label.value();
    // The insert into the disk hash tree failed, so let us remove
    // the credential from the TPM state so that we are back to where
    // we started.
    success = le_tpm_backend_->RemoveCredential(label.value(), h_aux, mac);
    if (!success) {
      LOG(ERROR) << " Failed to rewind aborted InsertCredential in TPM, label: "
                 << label.value();
      // The attempt to undo the TPM side operation has also failed, Can't do
      // much else now. We block further LE operations until at least the next
      // boot.
      is_locked_ = true;
      // TODO(crbug.com/809749): Report failure to UMA.
    }
    return LE_CRED_ERROR_HASH_TREE;
  }

  *ret_label = label.value();
  return LE_CRED_SUCCESS;
}

LECredError LECredentialManager::CheckCredential(
    const uint64_t& label,
    const brillo::SecureBlob& le_secret,
    brillo::SecureBlob* he_secret) {
  if (is_locked_) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  he_secret->clear();

  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  LECredError ret =
      RetrieveLabelInfo(label_object, &orig_cred, &orig_mac, &h_aux);
  if (ret != LE_CRED_SUCCESS) {
    return ret;
  }

  brillo::SecureBlob new_cred, new_mac;
  LECredBackendError err;
  le_tpm_backend_->CheckCredential(label, h_aux, orig_cred, le_secret,
                                   &new_cred, &new_mac, he_secret, &err);
  if (err == LE_TPM_ERROR_HASH_TREE_SYNC || err == LE_TPM_ERROR_TPM_OP_FAILED) {
    LOG(ERROR) << "Error running CheckCredential in TPM for label: " << label;
    return LE_CRED_ERROR_HASH_TREE;
  }

  if (!hash_tree_->StoreLabel(label_object, new_mac, new_cred)) {
    LOG(ERROR) << "Failed to update credential in disk hash tree for label: "
               << label;
    // This is an un-salvageable state. We can't make LE updates anymore,
    // since the disk state can't be updated.
    // We block further LE operations until at least the next boot.
    // The hope is that on reboot, the disk operations start working. In that
    // case, we will be able to replay this operation from the TPM log.
    is_locked_ = true;
    // TODO(crbug.com/809749): Report failure to UMA.
    return LE_CRED_ERROR_HASH_TREE;
  }

  return ConvertTpmError(err);
}

LECredError LECredentialManager::RemoveCredential(const uint64_t& label) {
  if (is_locked_) {
    return LE_CRED_ERROR_HASH_TREE;
  }

  SignInHashTree::Label label_object(label, kLengthLabels, kBitsPerLevel);
  std::vector<uint8_t> orig_cred, orig_mac;
  std::vector<std::vector<uint8_t>> h_aux;
  LECredError ret =
      RetrieveLabelInfo(label_object, &orig_cred, &orig_mac, &h_aux);
  if (ret != LE_CRED_SUCCESS) {
    return ret;
  }

  bool success = le_tpm_backend_->RemoveCredential(label, h_aux, orig_mac);
  if (!success) {
    LOG(ERROR) << "Error executing TPM RemoveCredential command.";
    return LE_CRED_ERROR_HASH_TREE;
  }

  if (!hash_tree_->RemoveLabel(label_object)) {
    LOG(ERROR) << "Removed label from TPM but hash tree removal "
                  "encountered error: "
               << label;
    // This is an un-salvageable state. We can't make LE updates anymore,
    // since the disk state can't be updated.
    // We block further LE operations until at least the next boot.
    // The hope is that on reboot, the disk operations start working. In that
    // case, we will be able to replay this operation from the TPM log.
    is_locked_ = true;
    // TODO(crbug.com/809749): Report failure to UMA.
    return LE_CRED_ERROR_HASH_TREE;
  }

  return LE_CRED_SUCCESS;
}

LECredError LECredentialManager::RetrieveLabelInfo(
    const SignInHashTree::Label& label,
    std::vector<uint8_t>* cred_metadata,
    std::vector<uint8_t>* mac,
    std::vector<std::vector<uint8_t>>* h_aux) {
  if (!hash_tree_->GetLabelData(label, mac, cred_metadata)) {
    LOG(ERROR) << "Failed to get the credential in disk hash tree for label: "
               << label.value();
    return LE_CRED_ERROR_INVALID_LABEL;
  }

  // Any empty |cred_metadata| means the label isn't present in the hash tree.
  if (cred_metadata->empty()) {
    LOG(ERROR) << "Label doesn't exist in hash tree: " << label.value();
    return LE_CRED_ERROR_INVALID_LABEL;
  }

  *h_aux = GetAuxHashes(label);
  if (h_aux->empty()) {
    LOG(ERROR) << "Error retrieving aux hashes from hash tree for label: "
               << label.value();
    return LE_CRED_ERROR_HASH_TREE;
  }
  return LE_CRED_SUCCESS;
}

std::vector<std::vector<uint8_t>> LECredentialManager::GetAuxHashes(
    const SignInHashTree::Label& label) {
  auto aux_labels = hash_tree_->GetAuxiliaryLabels(label);
  std::vector<std::vector<uint8_t>> h_aux;
  if (aux_labels.empty()) {
    LOG(ERROR) << "Error getting h_aux for label:" << label.value();
    return h_aux;
  }

  h_aux.reserve(aux_labels.size());
  for (auto cur_aux_label : aux_labels) {
    brillo::SecureBlob hash, cred_data;
    if (!hash_tree_->GetLabelData(cur_aux_label, &hash, &cred_data)) {
      LOG(INFO) << "Error getting aux label :" << cur_aux_label.value()
                << " for label: " << label.value();
      h_aux.clear();
      break;
    }
    h_aux.push_back(std::move(hash));
  }

  return h_aux;
}

LECredError LECredentialManager::ConvertTpmError(LECredBackendError err) {
  switch (err) {
    case LE_TPM_SUCCESS:
      return LE_CRED_SUCCESS;
    case LE_TPM_ERROR_INVALID_LE_SECRET:
      return LE_CRED_ERROR_INVALID_LE_SECRET;
    case LE_TPM_ERROR_TOO_MANY_ATTEMPTS:
      return LE_CRED_ERROR_TOO_MANY_ATTEMPTS;
    case LE_TPM_ERROR_HASH_TREE_SYNC:
    case LE_TPM_ERROR_TPM_OP_FAILED:
      return LE_CRED_ERROR_HASH_TREE;
  }

  return LE_CRED_ERROR_HASH_TREE;
}

}  // namespace cryptohome