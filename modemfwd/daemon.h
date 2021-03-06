// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_H_
#define MODEMFWD_DAEMON_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "modemfwd/component.h"
#include "modemfwd/journal.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_tracker.h"

namespace modemfwd {

class Daemon : public brillo::DBusDaemon {
 public:
  // Constructor for Daemon which loads the cellular component to get
  // firmware.
  Daemon(const std::string& journal_file, const std::string& helper_directory);
  // Constructor for Daemon which loads from already set-up
  // directories.
  Daemon(const std::string& journal_file,
         const std::string& helper_directory,
         const std::string& firmware_directory);
  ~Daemon() override = default;

 protected:
  // brillo::Daemon overrides.
  int OnInit() override;
  int OnEventLoopStarted() override;

 private:
  // If the component fails to load at start-up, we can still try again later
  // when the component updater service has checked for more components.
  void RetryComponent();

  // Once we have a path for the firmware directory we can set up the
  // journal and flasher.
  int CompleteInitialization();

  // Called when a modem appears. Generally this means on startup but can
  // also be called in response to e.g. rebooting the modem or SIM hot
  // swapping.
  void OnModemAppeared(
      std::unique_ptr<org::chromium::flimflam::DeviceProxy> modem);

  std::unique_ptr<Component> component_;

  base::FilePath journal_file_path_;
  base::FilePath helper_dir_path_;
  base::FilePath firmware_dir_path_;

  int component_reload_retries_;

  std::unique_ptr<ModemHelperDirectory> helper_directory_;

  std::unique_ptr<Journal> journal_;
  std::unique_ptr<ModemTracker> modem_tracker_;
  std::unique_ptr<ModemFlasher> modem_flasher_;

  base::WeakPtrFactory<Daemon> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_H_
