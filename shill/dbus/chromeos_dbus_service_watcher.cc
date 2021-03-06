// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_dbus_service_watcher.h"

namespace shill {

ChromeosDBusServiceWatcher::ChromeosDBusServiceWatcher(
      scoped_refptr<dbus::Bus> bus,
      const std::string& connection_name,
      const base::Closure& on_connection_vanished)
    : watcher_(
        new brillo::dbus_utils::DBusServiceWatcher(
            bus, connection_name, on_connection_vanished)) {}

ChromeosDBusServiceWatcher::~ChromeosDBusServiceWatcher() {}

}  // namespace shill
