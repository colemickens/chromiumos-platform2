// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_PERMISSION_BROKER_H_
#define PERMISSION_BROKER_PERMISSION_BROKER_H_

#include <dbus/dbus.h>
#include <grp.h>
#include <libudev.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>

namespace permission_broker {

class Rule;

// The PermissionBroker encapsulates the execution of a chain of Rules which
// decide whether or not to grant access to a given path. The PermissionBroker
// is also responsible for providing a DBus interface to clients.
class PermissionBroker {
 public:
  PermissionBroker(const std::string& access_group,
                   const std::string& udev_run_path,
                   int poll_interval_msecs);
  virtual ~PermissionBroker();

  // Initializes the broker and loops waiting for requests on the DBus
  // interface. Never returns.
  void Run();

  // Adds |rule| to the end of the existing rule chain. Takes ownership of
  // |rule|.
  void AddRule(Rule* rule);

 protected:
  // This constructor is for use by test code only.
  explicit PermissionBroker(const gid_t access_group);

 private:
  friend class PermissionBrokerTest;

  // The callback invoked by the DBus method handler in order to dispatch method
  // calls to their individual handlers.
  static DBusHandlerResult MainDBusMethodHandler(DBusConnection* connection,
                                                 DBusMessage* message,
                                                 void* data);

  // Waits for all queued udev events to complete before returning. Is
  // equivalent to invoking 'udevadm settle', but without the external
  // dependency and overhead.
  virtual void WaitForEmptyUdevQueue();

  // Invokes each of the rules in order on |path| until either a rule explicitly
  // denies access to the path or until there are no more rules left. If, after
  // executing all of the stored rules, no rule has explicitly allowed access to
  // the path then access is denied. If _any_ rule denies access to |path| then
  // processing the rules is aborted early and access is denied.
  bool ProcessPath(const std::string& path, int interface_id);

  // Grants access to |path|, which is accomplished by changing the owning group
  // on the path to the one specified numerically by the 'access_group' flag.
  virtual bool GrantAccess(const std::string& path);

  DBusMessage* HandleRequestPathAccessMethod(DBusMessage* message);

  struct udev* udev_;
  gid_t access_group_;
  std::vector<Rule*> rules_;

  int poll_interval_msecs_;
  std::string udev_run_path_;

  DISALLOW_COPY_AND_ASSIGN(PermissionBroker);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_PERMISSION_BROKER_H_
