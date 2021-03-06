// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_DBUS_OBJECTMANAGER_PROXY_H_
#define SHILL_DBUS_CHROMEOS_DBUS_OBJECTMANAGER_PROXY_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/dbus_objectmanager_proxy_interface.h"

namespace shill {

class EventDispatcher;

class ChromeosDBusObjectManagerProxy : public DBusObjectManagerProxyInterface {
 public:
  ChromeosDBusObjectManagerProxy(
      EventDispatcher* dispatcher,
      const scoped_refptr<dbus::Bus>& bus,
      const std::string& path,
      const std::string& service,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback);
  ~ChromeosDBusObjectManagerProxy() override;

  // Inherited methods from DBusObjectManagerProxyInterface.
  void GetManagedObjects(Error* error,
                         const ManagedObjectsCallback& callback,
                         int timeout) override;

  void set_interfaces_added_callback(
      const InterfacesAddedSignalCallback& callback) override {
    interfaces_added_callback_ = callback;
  }

  void set_interfaces_removed_callback(
      const InterfacesRemovedSignalCallback& callback) override {
    interfaces_removed_callback_ = callback;
  }

 private:
  using DBusInterfaceToProperties =
      std::map<std::string, brillo::VariantDictionary>;
  using DBusObjectsWithProperties =
      std::map<dbus::ObjectPath, DBusInterfaceToProperties>;

  // Signal handlers.
  void InterfacesAdded(
      const dbus::ObjectPath& object_path,
      const DBusInterfaceToProperties& interfaces_and_properties);
  void InterfacesRemoved(const dbus::ObjectPath& object_path,
                         const std::vector<std::string>& interfaces);

  // GetManagedObject method callbacks
  void OnGetManagedObjectsSuccess(
      const ManagedObjectsCallback& callback,
      const DBusObjectsWithProperties& objects_with_properties);
  void OnGetManagedObjectsFailure(const ManagedObjectsCallback& callback,
                                  brillo::Error* error);

  // Called when service appeared or vanished.
  void OnServiceAvailable(bool available);

  // Service name owner changed handler.
  void OnServiceOwnerChanged(const std::string& old_owner,
                             const std::string& new_owner);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  void ConvertDBusInterfaceProperties(
      const DBusInterfaceToProperties& dbus_interface_to_properties,
      InterfaceToProperties* interface_to_properties);

  InterfacesAddedSignalCallback interfaces_added_callback_;
  InterfacesRemovedSignalCallback interfaces_removed_callback_;

  std::unique_ptr<org::freedesktop::DBus::ObjectManagerProxy> proxy_;
  EventDispatcher* dispatcher_;
  base::Closure service_appeared_callback_;
  base::Closure service_vanished_callback_;
  bool service_available_;

  base::WeakPtrFactory<ChromeosDBusObjectManagerProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosDBusObjectManagerProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_DBUS_OBJECTMANAGER_PROXY_H_
