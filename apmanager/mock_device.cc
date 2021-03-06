// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apmanager/mock_device.h"

namespace apmanager {

MockDevice::MockDevice(Manager* manager) : Device(manager, "", 0) {}

MockDevice::~MockDevice() {}

}  // namespace apmanager
