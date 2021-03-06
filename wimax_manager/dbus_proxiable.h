// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_DBUS_PROXIABLE_H_
#define WIMAX_MANAGER_DBUS_PROXIABLE_H_

#include <memory>

#include <base/macros.h>
#include <dbus-c++/dbus.h>

#include "wimax_manager/dbus_control.h"

namespace wimax_manager {

template <typename Self, typename Proxy>
class DBusProxiable {
 public:
  DBusProxiable() = default;
  ~DBusProxiable() = default;

  void CreateDBusProxy() {
    if (dbus_proxy_.get())
      return;

    dbus_proxy_.reset(
        new Proxy(DBusControl::GetConnection(), static_cast<Self*>(this)));
  }

  void InvalidateDBusProxy() { dbus_proxy_.reset(); }

  Proxy* dbus_proxy() const { return dbus_proxy_.get(); }

 private:
  std::unique_ptr<Proxy> dbus_proxy_;

  DISALLOW_COPY_AND_ASSIGN(DBusProxiable);
};

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_DBUS_PROXIABLE_H_
