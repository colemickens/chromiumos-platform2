// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PEERD_MOCK_AVAHI_CLIENT_H_
#define PEERD_MOCK_AVAHI_CLIENT_H_

#include "peerd/peer.h"

#include <gmock/gmock.h>

namespace peerd {

class MockAvahiClient: public AvahiClient {
 public:
  MockAvahiClient() : AvahiClient(scoped_refptr<dbus::Bus>{}) { }
  ~MockAvahiClient() override = default;
  MOCK_METHOD1(RegisterAsync, void(const CompletionAction& cb));
  MOCK_METHOD1(RegisterOnAvahiRestartCallback,
               void(const OnAvahiRestartCallback& cb));
  MOCK_METHOD0(GetPublisher, base::WeakPtr<ServicePublisherInterface>());
};

}  // namespace peerd

#endif  // PEERD_MOCK_AVAHI_CLIENT_H_
