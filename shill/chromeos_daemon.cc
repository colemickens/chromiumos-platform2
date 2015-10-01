//
// Copyright (C) 2015 The Android Open Source Project
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
//

#include "shill/chromeos_daemon.h"

#include <string>

#include <base/bind.h>

#if !defined(ENABLE_JSON_STORE)
#include <glib.h>
#include <glib-object.h>
#endif

#include "shill/control_interface.h"
#include "shill/dhcp/dhcp_provider.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/net/ndisc.h"
#include "shill/net/rtnl_handler.h"
#include "shill/process_manager.h"
#include "shill/routing_table.h"
#include "shill/shill_config.h"

#if !defined(DISABLE_WIFI)
#include "shill/net/netlink_manager.h"
#include "shill/net/nl80211_message.h"
#if defined(__BRILLO__)
#include "shill/net/wifi_driver_hal.h"
#endif  // __BRILLO__
#endif  // DISABLE_WIFI

using base::Bind;
using base::Unretained;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDaemon;
static string ObjectID(ChromeosDaemon* d) { return "(shill_daemon)"; }
}


ChromeosDaemon::ChromeosDaemon(const Settings& settings,
                               Config* config)
    : settings_(settings), config_(config) {}

ChromeosDaemon::~ChromeosDaemon() {}

void ChromeosDaemon::Init(ControlInterface* control,
                          EventDispatcher* dispatcher) {
  control_.reset(control);
  dispatcher_ = dispatcher;
  metrics_.reset(new Metrics(dispatcher_));
  rtnl_handler_ = RTNLHandler::GetInstance();
  routing_table_ = RoutingTable::GetInstance();
  dhcp_provider_ = DHCPProvider::GetInstance();
  process_manager_ = ProcessManager::GetInstance();
#if !defined(DISABLE_WIFI)
#if defined(__BRILLO__)
  wifi_driver_hal_ = WiFiDriverHal::GetInstance();
#endif  // __BRILLO__
  netlink_manager_ = NetlinkManager::GetInstance();
  callback80211_metrics_.reset(new Callback80211Metrics(metrics_.get()));
#endif  // DISABLE_WIFI
  manager_.reset(new Manager(control_.get(),
                             dispatcher_,
                             metrics_.get(),
                             config_->GetRunDirectory(),
                             config_->GetStorageDirectory(),
                             config_->GetUserStorageDirectory()));
  ApplySettings();
}

void ChromeosDaemon::ApplySettings() {
  for (const auto& device_name : settings_.device_blacklist) {
    manager_->AddDeviceToBlackList(device_name);
  }
  Error error;
  manager_->SetTechnologyOrder(settings_.default_technology_order, &error);
  CHECK(error.IsSuccess());  // Command line should have been validated.
  manager_->SetIgnoreUnknownEthernet(settings_.ignore_unknown_ethernet);
  if (settings_.use_portal_list) {
    manager_->SetStartupPortalList(settings_.portal_list);
  }
  if (settings_.passive_mode) {
    manager_->SetPassiveMode();
  }
  manager_->SetPrependDNSServers(settings_.prepend_dns_servers);
  if (settings_.minimum_mtu) {
    manager_->SetMinimumMTU(settings_.minimum_mtu);
  }
  manager_->SetAcceptHostnameFrom(settings_.accept_hostname_from);
  manager_->SetDHCPv6EnabledDevices(settings_.dhcpv6_enabled_devices);
}

bool ChromeosDaemon::Quit(const base::Closure& completion_callback) {
  SLOG(this, 1) << "Starting termination actions.";
  if (manager_->RunTerminationActionsAndNotifyMetrics(
          Bind(&ChromeosDaemon::TerminationActionsCompleted,
               Unretained(this)))) {
    SLOG(this, 1) << "Will wait for termination actions to complete";
    termination_completed_callback_ = completion_callback;
    return false;  // Note to caller: don't exit yet!
  } else {
    SLOG(this, 1) << "No termination actions were run";
    StopAndReturnToMain();
    return true;  // All done, ready to exit.
  }
}

void ChromeosDaemon::TerminationActionsCompleted(const Error& error) {
  SLOG(this, 1) << "Finished termination actions.  Result: " << error;
  metrics_->NotifyTerminationActionsCompleted(error.IsSuccess());

  // Daemon::TerminationActionsCompleted() should not directly call
  // Daemon::Stop(). Otherwise, it could lead to the call sequence below. That
  // is not safe as the HookTable's start callback only holds a weak pointer to
  // the Cellular object, which is destroyed in midst of the
  // Cellular::OnTerminationCompleted() call. We schedule the
  // Daemon::StopAndReturnToMain() call through the message loop instead.
  //
  // Daemon::Quit
  //   -> Manager::RunTerminationActionsAndNotifyMetrics
  //     -> Manager::RunTerminationActions
  //       -> HookTable::Run
  //         ...
  //         -> Cellular::OnTerminationCompleted
  //           -> Manager::TerminationActionComplete
  //             -> HookTable::ActionComplete
  //               -> Daemon::TerminationActionsCompleted
  //                 -> Daemon::Stop
  //                   -> Manager::Stop
  //                     -> DeviceInfo::Stop
  //                       -> Cellular::~Cellular
  //           -> Manager::RemoveTerminationAction
  dispatcher_->PostTask(
      Bind(&ChromeosDaemon::StopAndReturnToMain, Unretained(this)));
}

void ChromeosDaemon::StopAndReturnToMain() {
  Stop();
  if (!termination_completed_callback_.is_null()) {
    termination_completed_callback_.Run();
  }
}

void ChromeosDaemon::Start() {
#if !defined(ENABLE_JSON_STORE)
  g_type_init();
#endif
  metrics_->Start();
  rtnl_handler_->Start(
      RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE |
      RTMGRP_IPV6_IFADDR | RTMGRP_IPV6_ROUTE | RTMGRP_ND_USEROPT);
  routing_table_->Start();
  dhcp_provider_->Init(control_.get(), dispatcher_, metrics_.get());
  process_manager_->Init(dispatcher_);
#if !defined(DISABLE_WIFI)
  if (netlink_manager_) {
    netlink_manager_->Init();
    uint16_t nl80211_family_id = netlink_manager_->GetFamily(
        Nl80211Message::kMessageTypeString,
        Bind(&Nl80211Message::CreateMessage));
    if (nl80211_family_id == NetlinkMessage::kIllegalMessageType) {
      LOG(FATAL) << "Didn't get a legal message type for 'nl80211' messages.";
    }
    Nl80211Message::SetMessageType(nl80211_family_id);
    netlink_manager_->Start();

    // Install handlers for NetlinkMessages that don't have specific handlers
    // (which are registered by message sequence number).
    netlink_manager_->AddBroadcastHandler(Bind(
        &Callback80211Metrics::CollectDisconnectStatistics,
        callback80211_metrics_->AsWeakPtr()));
  }
#if defined(__BRILLO__)
  string interface = wifi_driver_hal_->SetupStationModeInterface();
  if (interface.empty()) {
    LOG(FATAL) << "Failed to setup station mode interface.";
  }
#endif  // __BRILLO__
#endif  // DISABLE_WIFI

  manager_->Start();
}

void ChromeosDaemon::Stop() {
  manager_->Stop();
  manager_ = nullptr;  // Release manager resources, including DBus adaptor.
#if !defined(DISABLE_WIFI)
  callback80211_metrics_ = nullptr;
#endif  // DISABLE_WIFI
  metrics_->Stop();
  process_manager_->Stop();
  dhcp_provider_->Stop();
  metrics_ = nullptr;
  control_ = nullptr;
}

}  // namespace shill