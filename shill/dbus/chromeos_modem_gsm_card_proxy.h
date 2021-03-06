// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_MODEM_GSM_CARD_PROXY_H_
#define SHILL_DBUS_CHROMEOS_MODEM_GSM_CARD_PROXY_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/modem_gsm_card_proxy_interface.h"

namespace shill {

// A proxy to (old) ModemManager.Modem.Gsm.Card.
class ChromeosModemGsmCardProxy : public ModemGsmCardProxyInterface {
 public:
  // Constructs a ModemManager.Modem.Gsm.Card DBus
  // object proxy at |path| owned by |service|.
  ChromeosModemGsmCardProxy(const scoped_refptr<dbus::Bus>& bus,
                            const std::string& path,
                            const std::string& service);
  ~ChromeosModemGsmCardProxy() override;

  // Inherited from ModemGsmCardProxyInterface.
  void GetIMEI(Error* error,
               const GsmIdentifierCallback& callback,
               int timeout) override;
  void GetIMSI(Error* error,
               const GsmIdentifierCallback& callback,
               int timeout) override;
  void GetSPN(Error* error,
              const GsmIdentifierCallback& callback,
              int timeout) override;
  void GetMSISDN(Error* error,
                 const GsmIdentifierCallback& callback,
                 int timeout) override;
  void EnablePIN(const std::string& pin,
                 bool enabled,
                 Error* error,
                 const ResultCallback& callback,
                 int timeout) override;
  void SendPIN(const std::string& pin,
               Error* error,
               const ResultCallback& callback,
               int timeout) override;
  void SendPUK(const std::string& puk,
               const std::string& pin,
               Error* error,
               const ResultCallback& callback,
               int timeout) override;
  void ChangePIN(const std::string& old_pin,
                 const std::string& new_pin,
                 Error* error,
                 const ResultCallback& callback,
                 int timeout) override;
  uint32_t EnabledFacilityLocks() override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    brillo::dbus_utils::Property<uint32_t> enabled_facility_locks;

   private:
    DISALLOW_COPY_AND_ASSIGN(PropertySet);
  };

  static const char kPropertyEnabledFacilityLocks[];

  // Callbacks for various GSMIdentifier Get async calls.
  void OnGetGsmIdentifierSuccess(const GsmIdentifierCallback& callback,
                                 const std::string& identifier_name,
                                 const std::string& identifier_value);
  void OnGetGsmIdentifierFailure(const GsmIdentifierCallback& callback,
                                 const std::string& identifier_name,
                                 brillo::Error* dbus_error);
  void OnOperationSuccess(const ResultCallback& callback,
                          const std::string& operation_name);
  void OnOperationFailure(const ResultCallback& callback,
                          const std::string& operation_name,
                          brillo::Error* dbus_error);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  std::unique_ptr<org::freedesktop::ModemManager::Modem::Gsm::CardProxy> proxy_;
  std::unique_ptr<PropertySet> properties_;

  base::WeakPtrFactory<ChromeosModemGsmCardProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosModemGsmCardProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_MODEM_GSM_CARD_PROXY_H_
