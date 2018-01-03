// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPASSWORDPROVIDER_FAKE_PASSWORD_PROVIDER_H_
#define LIBPASSWORDPROVIDER_FAKE_PASSWORD_PROVIDER_H_

#include "libpasswordprovider/password_provider.h"

#include <memory>
#include <string>

#include <base/macros.h>

#include "libpasswordprovider/password.h"

namespace password_provider {

// Fake implementation of password storage.
class FakePasswordProvider : public PasswordProviderInterface {
 public:
  FakePasswordProvider() {}

  bool password_saved() const { return password_saved_; }
  bool password_discarded() const {
    return password_discarded_ && password_.size() == 0;
  }

  // PasswordProviderInterface overrides
  bool SavePassword(const Password& password) override {
    password_saved_ = true;

    password_ = std::string(password.GetRaw(), password.size());
    return true;
  }

  std::unique_ptr<Password> GetPassword() override {
    if (password_discarded()) {
      return nullptr;
    }

    auto password = std::make_unique<Password>();

    memcpy(password->GetMutableRaw(), password_.c_str(), password_.size());
    password->SetSize(password_.size());

    return password;
  }

  bool DiscardPassword() override {
    password_.clear();
    password_discarded_ = true;
    return true;
  }

 private:
  bool password_saved_ = false;      // true if the password was ever saved.
  bool password_discarded_ = false;  // true if password_ is cleared out.
  std::string password_;

  DISALLOW_COPY_AND_ASSIGN(FakePasswordProvider);
};

}  // namespace password_provider

#endif  // LIBPASSWORDPROVIDER_FAKE_PASSWORD_PROVIDER_H_
