// Copyright 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_
#define WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_

#include "webservd/firewall_interface.h"

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "permission_broker/dbus-proxies.h"

namespace webservd {

class PermissionBrokerFirewall : public FirewallInterface {
 public:
  PermissionBrokerFirewall();
  ~PermissionBrokerFirewall() override;

  // Interface overrides.
  void WaitForServiceAsync(scoped_refptr<dbus::Bus> bus,
                           const base::Closure& callback) override;
  void PunchTcpHoleAsync(
      uint16_t port, const std::string& interface_name,
      const base::Callback<void(bool)>& success_cb,
      const base::Callback<void(brillo::Error*)>& failure_cb) override;

 private:
  // Callbacks to register to see when permission_broker starts or
  // restarts.
  void OnPermissionBrokerAvailable(bool available);
  void OnPermissionBrokerNameOwnerChanged(const std::string& old_owner,
                                          const std::string& new_owner);

  std::unique_ptr<org::chromium::PermissionBrokerProxy> proxy_;

  // Callback to use when firewall service starts or restarts.
  base::Closure service_started_cb_;

  // File descriptors for the two ends of the pipe used for communicating with
  // remote firewall server (permission_broker), where the remote firewall
  // server will use the read end of the pipe to detect when this process exits.
  int lifeline_read_fd_{-1};
  int lifeline_write_fd_{-1};

  base::WeakPtrFactory<PermissionBrokerFirewall> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(PermissionBrokerFirewall);
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_
