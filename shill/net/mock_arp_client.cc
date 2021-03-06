// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/net/mock_arp_client.h"

namespace shill {

MockArpClient::MockArpClient() : ArpClient(0) {}

MockArpClient::~MockArpClient() {}

}  // namespace shill
