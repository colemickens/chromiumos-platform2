// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_WIMAX_MANAGER_PROXY_H_
#define SHILL_DBUS_CHROMEOS_WIMAX_MANAGER_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <wimax_manager/dbus-proxies.h>

#include "shill/wimax/wimax_manager_proxy_interface.h"

namespace shill {

class EventDispatcher;

class ChromeosWiMaxManagerProxy : public WiMaxManagerProxyInterface {
 public:
  ChromeosWiMaxManagerProxy(EventDispatcher* dispatcher,
                            const scoped_refptr<dbus::Bus>& bus,
                            const base::Closure& service_appeared_callback,
                            const base::Closure& service_vanished_callback);
  ~ChromeosWiMaxManagerProxy() override;

  // Inherited from WiMaxManagerProxyInterface.
  void set_devices_changed_callback(
      const DevicesChangedCallback& callback) override;
  RpcIdentifiers Devices(Error* error) override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    brillo::dbus_utils::Property<std::vector<dbus::ObjectPath>> devices;

   private:
    DISALLOW_COPY_AND_ASSIGN(PropertySet);
  };

  static const char kPropertyDevices[];

  // Called when service appeared or vanished.
  void OnServiceAvailable(bool available);

  // Service name owner changed handler.
  void OnServiceOwnerChanged(const std::string& old_owner,
                             const std::string& new_owner);

  // Signal callbacks inherited from WiMaxManager_proxy.
  void DevicesChanged(const std::vector<dbus::ObjectPath>& devices);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<org::chromium::WiMaxManagerProxy> proxy_;
  std::unique_ptr<PropertySet> properties_;
  EventDispatcher* dispatcher_;
  base::Closure service_appeared_callback_;
  base::Closure service_vanished_callback_;
  bool service_available_;
  DevicesChangedCallback devices_changed_callback_;

  base::WeakPtrFactory<ChromeosWiMaxManagerProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosWiMaxManagerProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_WIMAX_MANAGER_PROXY_H_
