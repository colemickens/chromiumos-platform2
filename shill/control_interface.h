// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CONTROL_INTERFACE_H_
#define SHILL_CONTROL_INTERFACE_H_

#include <algorithm>
#include <memory>
#include <string>

#include <base/callback.h>

#include "shill/logging.h"

namespace shill {

class Device;
class DeviceAdaptorInterface;
class IPConfig;
class IPConfigAdaptorInterface;
class Manager;
class ManagerAdaptorInterface;
class Profile;
class ProfileAdaptorInterface;
class RPCTask;
class RPCTaskAdaptorInterface;
class Service;
class ServiceAdaptorInterface;
class ThirdPartyVpnDriver;
class ThirdPartyVpnAdaptorInterface;

class DBusObjectManagerProxyInterface;
class DBusPropertiesProxyInterface;
class DHCPCDListenerInterface;
class DHCPProvider;
class DHCPProxyInterface;
class ModemCdmaProxyInterface;
class ModemGsmCardProxyInterface;
class ModemGsmNetworkProxyInterface;
class ModemGobiProxyInterface;
class ModemManagerClassic;
class ModemManagerProxyInterface;
class ModemProxyInterface;
class ModemSimpleProxyInterface;
class PowerManagerProxyDelegate;
class PowerManagerProxyInterface;
class UpstartProxyInterface;
class WiMaxDeviceProxyInterface;
class WiMaxManagerProxyInterface;
class WiMaxNetworkProxyInterface;

#if !defined(DISABLE_WIFI)
class SupplicantBSSProxyInterface;
class WiFiEndpoint;
#endif  // DISABLE_WIFI

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
class SupplicantEventDelegateInterface;
class SupplicantInterfaceProxyInterface;
class SupplicantNetworkProxyInterface;
class SupplicantProcessProxyInterface;
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

namespace mm1 {

class ModemLocationProxyInterface;
class ModemModem3gppProxyInterface;
class ModemModemCdmaProxyInterface;
class ModemProxyInterface;
class ModemSimpleProxyInterface;
class SimProxyInterface;

}  // namespace mm1

// This is the Interface for an object factory that creates adaptor/proxy
// objects
class ControlInterface {
 public:
  virtual ~ControlInterface() {}
  virtual void RegisterManagerObject(
      Manager* manager, const base::Closure& registration_done_callback) = 0;
  virtual std::unique_ptr<DeviceAdaptorInterface> CreateDeviceAdaptor(
      Device* device) = 0;
  virtual std::unique_ptr<IPConfigAdaptorInterface> CreateIPConfigAdaptor(
      IPConfig* ipconfig) = 0;
  virtual std::unique_ptr<ManagerAdaptorInterface> CreateManagerAdaptor(
      Manager* manager) = 0;
  virtual std::unique_ptr<ProfileAdaptorInterface> CreateProfileAdaptor(
      Profile* profile) = 0;
  virtual std::unique_ptr<ServiceAdaptorInterface> CreateServiceAdaptor(
      Service* service) = 0;
  virtual std::unique_ptr<RPCTaskAdaptorInterface> CreateRPCTaskAdaptor(
      RPCTask* task) = 0;
#ifndef DISABLE_VPN
  virtual std::unique_ptr<ThirdPartyVpnAdaptorInterface>
  CreateThirdPartyVpnAdaptor(ThirdPartyVpnDriver* driver) = 0;
#endif

  virtual const std::string& NullRPCIdentifier() = 0;

  // The caller retains ownership of 'delegate'.  It must not be deleted before
  // the proxy.
  virtual std::unique_ptr<PowerManagerProxyInterface> CreatePowerManagerProxy(
      PowerManagerProxyDelegate* delegate,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback) = 0;

#if !defined(DISABLE_WIFI) || !defined(DISABLE_WIRED_8021X)
  virtual std::unique_ptr<SupplicantProcessProxyInterface>
  CreateSupplicantProcessProxy(
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback) = 0;

  virtual std::unique_ptr<SupplicantInterfaceProxyInterface>
  CreateSupplicantInterfaceProxy(SupplicantEventDelegateInterface* delegate,
                                 const std::string& object_path) = 0;

  virtual std::unique_ptr<SupplicantNetworkProxyInterface>
  CreateSupplicantNetworkProxy(const std::string& object_path) = 0;
#endif  // DISABLE_WIFI || DISABLE_WIRED_8021X

#if !defined(DISABLE_WIFI)
  // See comment in supplicant_bss_proxy.h, about bare pointer.
  virtual std::unique_ptr<SupplicantBSSProxyInterface> CreateSupplicantBSSProxy(
      WiFiEndpoint* wifi_endpoint, const std::string& object_path) = 0;
#endif  // DISABLE_WIFI

  virtual std::unique_ptr<UpstartProxyInterface> CreateUpstartProxy() = 0;

  virtual std::unique_ptr<DHCPCDListenerInterface> CreateDHCPCDListener(
      DHCPProvider* provider) = 0;

  virtual std::unique_ptr<DHCPProxyInterface> CreateDHCPProxy(
      const std::string& service) = 0;

#if !defined(DISABLE_CELLULAR)
  virtual std::unique_ptr<DBusPropertiesProxyInterface>
  CreateDBusPropertiesProxy(const std::string& path,
                            const std::string& service) = 0;

  virtual std::unique_ptr<DBusObjectManagerProxyInterface>
  CreateDBusObjectManagerProxy(
      const std::string& path,
      const std::string& service,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback) = 0;

  virtual std::unique_ptr<ModemManagerProxyInterface> CreateModemManagerProxy(
      ModemManagerClassic* manager,
      const std::string& path,
      const std::string& service,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback) = 0;

  virtual std::unique_ptr<ModemProxyInterface> CreateModemProxy(
      const std::string& path, const std::string& service) = 0;

  virtual std::unique_ptr<ModemSimpleProxyInterface> CreateModemSimpleProxy(
      const std::string& path, const std::string& service) = 0;

  virtual std::unique_ptr<ModemCdmaProxyInterface> CreateModemCdmaProxy(
      const std::string& path,
      const std::string& service) = 0;

  virtual std::unique_ptr<ModemGsmCardProxyInterface> CreateModemGsmCardProxy(
      const std::string& path, const std::string& service) = 0;

  virtual std::unique_ptr<ModemGsmNetworkProxyInterface>
  CreateModemGsmNetworkProxy(const std::string& path,
                             const std::string& service) = 0;

  virtual std::unique_ptr<ModemGobiProxyInterface> CreateModemGobiProxy(
      const std::string& path, const std::string& service) = 0;

  // Proxies for ModemManager1 interfaces
  virtual std::unique_ptr<mm1::ModemLocationProxyInterface>
  CreateMM1ModemLocationProxy(const std::string& path,
                              const std::string& service) = 0;

  virtual std::unique_ptr<mm1::ModemModem3gppProxyInterface>
  CreateMM1ModemModem3gppProxy(const std::string& path,
                               const std::string& service) = 0;

  virtual std::unique_ptr<mm1::ModemModemCdmaProxyInterface>
  CreateMM1ModemModemCdmaProxy(const std::string& path,
                               const std::string& service) = 0;

  virtual std::unique_ptr<mm1::ModemProxyInterface> CreateMM1ModemProxy(
      const std::string& path, const std::string& service) = 0;

  virtual std::unique_ptr<mm1::ModemSimpleProxyInterface>
  CreateMM1ModemSimpleProxy(const std::string& path,
                            const std::string& service) = 0;

  virtual std::unique_ptr<mm1::SimProxyInterface> CreateMM1SimProxy(
      const std::string& path, const std::string& service) = 0;
#endif  // DISABLE_CELLULAR

#if !defined(DISABLE_WIMAX)
  virtual std::unique_ptr<WiMaxDeviceProxyInterface> CreateWiMaxDeviceProxy(
      const std::string& path) = 0;
  virtual std::unique_ptr<WiMaxManagerProxyInterface> CreateWiMaxManagerProxy(
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback) = 0;
  virtual std::unique_ptr<WiMaxNetworkProxyInterface> CreateWiMaxNetworkProxy(
      const std::string& path) = 0;
#endif  // DISABLE_WIMAX
};

}  // namespace shill

#endif  // SHILL_CONTROL_INTERFACE_H_
