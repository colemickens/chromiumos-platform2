// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/time/time.h>

#include "modemfwd/component.h"
#include "modemfwd/firmware_directory.h"
#include "modemfwd/modem.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/modem_helper_directory.h"
#include "modemfwd/modem_tracker.h"

namespace {

constexpr int kComponentNumRetries = 3;
constexpr base::TimeDelta kComponentRetryDelay =
    base::TimeDelta::FromMinutes(10);

}  // namespace

namespace modemfwd {

Daemon::Daemon(const std::string& journal_file,
               const std::string& helper_directory)
    : Daemon(journal_file, helper_directory, "") {}

Daemon::Daemon(const std::string& journal_file,
               const std::string& helper_directory,
               const std::string& firmware_directory)
    : journal_file_path_(journal_file),
      helper_dir_path_(helper_directory),
      firmware_dir_path_(firmware_directory),
      component_reload_retries_(kComponentNumRetries),
      weak_ptr_factory_(this) {}

int Daemon::OnInit() {
  int exit_code = brillo::DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;
  DCHECK(!helper_dir_path_.empty());

  if (!base::DirectoryExists(helper_dir_path_)) {
    LOG(ERROR) << "Supplied modem-specific helper directory "
               << helper_dir_path_.value() << " does not exist";
    return EX_UNAVAILABLE;
  }

  helper_directory_ = CreateModemHelperDirectory(helper_dir_path_);
  if (!helper_directory_) {
    LOG(ERROR) << "No suitable helpers found in " << helper_dir_path_.value();
    return EX_UNAVAILABLE;
  }

  // If no firmware directory was supplied, we'll set up the firmware directory
  // and its dependents later after the message loop has started.
  if (firmware_dir_path_.empty()) {
    DLOG(INFO) << "Deferring initialization until firmware component"
               << " is available";
    return EX_OK;
  }

  if (!base::DirectoryExists(firmware_dir_path_)) {
    LOG(ERROR) << "Supplied firmware directory " << firmware_dir_path_.value()
               << " does not exist";
    return EX_UNAVAILABLE;
  }

  return CompleteInitialization();
}

int Daemon::CompleteInitialization() {
  CHECK(!firmware_dir_path_.empty());

  auto firmware_directory = CreateFirmwareDirectory(firmware_dir_path_);
  if (!firmware_directory) {
    LOG(ERROR) << "Could not load firmware directory (bad manifest?)";
    return EX_UNAVAILABLE;
  }

  auto journal = OpenJournal(journal_file_path_, firmware_directory.get(),
                             helper_directory_.get());
  if (!journal) {
    LOG(ERROR) << "Could not open journal file";
    return EX_UNAVAILABLE;
  }

  modem_flasher_ = std::make_unique<modemfwd::ModemFlasher>(
      std::move(firmware_directory), std::move(journal));

  modem_tracker_ = std::make_unique<modemfwd::ModemTracker>(
      bus_,
      base::Bind(&Daemon::OnModemAppeared, weak_ptr_factory_.GetWeakPtr()));

  return EX_OK;
}

int Daemon::OnEventLoopStarted() {
  // We've already done this initialization if we have a dir path.
  if (!firmware_dir_path_.empty())
    return EX_OK;

  component_ = Component::Load(bus_);
  if (!component_) {
    // This could be because the component updater hasn't registered the
    // component yet. Try again later.
    if (--component_reload_retries_ > 0) {
      DLOG(INFO) << "Could not load component, retrying in "
                 << kComponentRetryDelay.InMinutes() << " minutes";
      base::MessageLoop::current()->task_runner()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&Daemon::RetryComponent, weak_ptr_factory_.GetWeakPtr()),
          kComponentRetryDelay);
      return EX_OK;
    }

    LOG(ERROR) << "Exhausted retries for loading component, exiting";
    return EX_UNAVAILABLE;
  }

  firmware_dir_path_ = component_->GetPath();
  CHECK(base::DirectoryExists(firmware_dir_path_));
  return CompleteInitialization();
}

void Daemon::RetryComponent() {
  LOG(INFO) << "Retrying loading the firmware component";
  int exit_code = OnEventLoopStarted();
  if (exit_code != EX_OK)
    QuitWithExitCode(exit_code);
}

void Daemon::OnModemAppeared(
    std::unique_ptr<org::chromium::flimflam::DeviceProxy> device) {
  auto modem = CreateModem(std::move(device), helper_directory_.get());
  if (!modem)
    return;

  DLOG(INFO) << "Modem appeared with equipment ID \"" << modem->GetEquipmentId()
             << "\"";
  modem_flasher_->TryFlash(modem.get());
}

}  // namespace modemfwd
