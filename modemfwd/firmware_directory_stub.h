// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_
#define MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_

#include <map>
#include <string>
#include <utility>

#include <base/macros.h>

#include "modemfwd/firmware_directory.h"

namespace modemfwd {

class FirmwareDirectoryStub : public FirmwareDirectory {
 public:
  FirmwareDirectoryStub() = default;

  void AddMainFirmware(const std::string& device_id, FirmwareFileInfo info);
  void AddCarrierFirmware(const std::string& device_id,
                          const std::string& carrier_id,
                          FirmwareFileInfo info);

  // modemfwd::FirmwareDirectory overrides.
  bool FindMainFirmware(const std::string& device_id,
                        FirmwareFileInfo* out_info) override;
  bool FindCarrierFirmware(const std::string& device_id,
                           std::string* carrier_id,
                           FirmwareFileInfo* out_info) override;

 private:
  std::map<std::string, FirmwareFileInfo> main_fw_info_;
  std::map<std::pair<std::string, std::string>, FirmwareFileInfo>
      carrier_fw_info_;

  DISALLOW_COPY_AND_ASSIGN(FirmwareDirectoryStub);
};

}  // namespace modemfwd

#endif  // MODEMFWD_FIRMWARE_DIRECTORY_STUB_H_
