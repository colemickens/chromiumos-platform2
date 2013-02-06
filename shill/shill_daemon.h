// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DAEMON_H_
#define SHILL_DAEMON_H_

#include <string>

#include <base/memory/scoped_ptr.h>

#include "shill/event_dispatcher.h"
#include "shill/glib.h"
#include "shill/manager.h"
#include "shill/sockets.h"
#include "shill/callback80211_object.h"
#include "shill/callback80211_metrics.h"

namespace shill {

class Config;
class Config80211;
class ControlInterface;
class DHCPProvider;
class Error;
class GLib;
class Metrics;
class NSS;
class ProxyFactory;
class RoutingTable;
class RTNLHandler;

class Daemon {
 public:
  Daemon(Config *config, ControlInterface *control);
  ~Daemon();

  void AddDeviceToBlackList(const std::string &device_name);
  void SetStartupProfiles(const std::vector<std::string> &profile_path);
  void SetStartupPortalList(const std::string &portal_list);
  // Main for connection manager.  Starts main process and holds event loop.
  void Run();

  // Starts the termination actions in the manager.
  void Quit();

 private:
  friend class ShillDaemonTest;

  // Causes the dispatcher message loop to terminate, calls Stop(), and returns
  // to the main function which started the daemon.  Called when the termination
  // actions are completed.
  void TerminationActionsCompleted(const Error &error);
  void Start();
  void Stop();

  Config *config_;
  ControlInterface *control_;
  scoped_ptr<Metrics> metrics_;
  NSS *nss_;
  ProxyFactory *proxy_factory_;
  RTNLHandler *rtnl_handler_;
  RoutingTable *routing_table_;
  DHCPProvider *dhcp_provider_;
  Config80211 *config80211_;
  scoped_ptr<Manager> manager_;
  Callback80211Object callback80211_output_;
  Callback80211Metrics callback80211_metrics_;
  EventDispatcher dispatcher_;
  Sockets sockets_;
  GLib glib_;
};

}  // namespace shill

#endif  // SHILL_DAEMON_H_
