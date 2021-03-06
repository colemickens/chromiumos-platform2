// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_manager.h"

#include <memory>
#include <utility>

#include <base/stl_util.h>
#include <mm/mm-modem.h>

#include "shill/cellular/modem.h"
#include "shill/cellular/modem_manager_proxy_interface.h"
#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"

using std::string;
using std::vector;

namespace shill {

ModemManager::ModemManager(const string& service,
                           const string& path,
                           ModemInfo* modem_info)
    : service_(service),
      path_(path),
      service_connected_(false),
      modem_info_(modem_info) {}

ModemManager::~ModemManager() {}

void ModemManager::Connect() {
  // Inheriting classes call this superclass method.
  service_connected_ = true;
}

void ModemManager::Disconnect() {
  // Inheriting classes call this superclass method.
  modems_.clear();
  service_connected_ = false;
}

void ModemManager::OnAppeared() {
  LOG(INFO) << "Modem manager " << service_ << " appeared.";
  Connect();
}

void ModemManager::OnVanished() {
  LOG(INFO) << "Modem manager " << service_ << " vanished.";
  Disconnect();
}

bool ModemManager::ModemExists(const std::string& path) const {
  CHECK(service_connected_);
  if (base::ContainsKey(modems_, path)) {
    LOG(INFO) << "ModemExists: " << path << " already exists.";
    return true;
  } else {
    return false;
  }
}

void ModemManager::RecordAddedModem(std::unique_ptr<Modem> modem) {
  modems_[modem->path()] = std::move(modem);
}

void ModemManager::RemoveModem(const string& path) {
  LOG(INFO) << "Remove modem: " << path;
  CHECK(service_connected_);
  modems_.erase(path);
}

void ModemManager::OnDeviceInfoAvailable(const string& link_name) {
  for (const auto& link_name_modem_pair : modems_) {
    link_name_modem_pair.second->OnDeviceInfoAvailable(link_name);
  }
}

// ModemManagerClassic
ModemManagerClassic::ModemManagerClassic(const string& service,
                                         const string& path,
                                         ModemInfo* modem_info)
    : ModemManager(service, path, modem_info) {}

ModemManagerClassic::~ModemManagerClassic() {
  Stop();
}

void ModemManagerClassic::Start() {
  LOG(INFO) << "Start watching modem manager service: " << service();
  CHECK(!proxy_);
  proxy_ = control_interface()->CreateModemManagerProxy(
      this,
      path(),
      service(),
      base::Bind(&ModemManagerClassic::OnAppeared, base::Unretained(this)),
      base::Bind(&ModemManagerClassic::OnVanished, base::Unretained(this)));
}

void ModemManagerClassic::Stop() {
  LOG(INFO) << "Stop watching modem manager service: " << service();
  proxy_.reset();
  Disconnect();
}

void ModemManagerClassic::Connect() {
  ModemManager::Connect();
  // TODO(petkov): Switch to asynchronous calls (crbug.com/200687).
  vector<string> devices = proxy_->EnumerateDevices();
  for (const auto& device : devices) {
    AddModemClassic(device);
  }
}

void ModemManagerClassic::AddModemClassic(const string& path) {
  if (ModemExists(path)) {
    return;
  }

  auto modem = std::make_unique<ModemClassic>(service(), path, modem_info());
  InitModemClassic(modem.get());

  RecordAddedModem(std::move(modem));
}

void ModemManagerClassic::Disconnect() {
  ModemManager::Disconnect();
}

void ModemManagerClassic::InitModemClassic(ModemClassic* modem) {
  // TODO(rochberg): Switch to asynchronous calls (crbug.com/200687).
  std::unique_ptr<DBusPropertiesProxyInterface> properties_proxy =
      control_interface()->CreateDBusPropertiesProxy(modem->path(),
                                                     modem->service());
  KeyValueStore properties =
      properties_proxy->GetAll(MM_MODEM_INTERFACE);

  modem->CreateDeviceClassic(properties);
}

void ModemManagerClassic::OnDeviceAdded(const string& path) {
  AddModemClassic(path);
}

void ModemManagerClassic::OnDeviceRemoved(const string& path) {
  RemoveModem(path);
}

}  // namespace shill
