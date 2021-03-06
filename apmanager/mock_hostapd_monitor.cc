// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apmanager/mock_hostapd_monitor.h"

namespace apmanager {

MockHostapdMonitor::MockHostapdMonitor()
    : HostapdMonitor(EventCallback(), "", "") {}
MockHostapdMonitor::~MockHostapdMonitor() {}

}  // namespace apmanager
