// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HAMMERD_HAMMER_UPDATER_H_
#define HAMMERD_HAMMER_UPDATER_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include "hammerd/pair_utils.h"
#include "hammerd/update_fw.h"

namespace hammerd {

class HammerUpdater {
 public:
  enum class RunStatus {
    kNoUpdate,
    kFatalError,
    kNeedReset,
    kNeedJump,
    kNeedInjectEntropy,
    kLostConnection,
    kInvalidFirmware,
  };

  HammerUpdater(const std::string& image,
                uint16_t vendor_id,
                uint16_t product_id,
                int bus,
                int port);
  virtual ~HammerUpdater() = default;

  // Handle the whole update process, including pre-processing, main update
  // logic loop, and the post-processing.
  virtual bool Run();
  // Handle the main update logic loop. For each round, it establishes the USB
  // connection, calls RunOnce() method, and runs some actions according the
  // returned status.
  virtual RunStatus RunLoop();
  // Handle the update logic from connecting to the EC to sending reset signal.
  // There is only one USB connection during each RunOnce() method call.
  // |post_rw_jump| indicates whether we jumped to RW section last round.
  virtual RunStatus RunOnce(const bool post_rw_jump,
                            const bool need_inject_entropy);
  virtual RunStatus PostRWProcess();
  virtual RunStatus Pair();

 protected:
  // Used in unittests to inject mock instance.
  HammerUpdater(const std::string& image,
                std::unique_ptr<FirmwareUpdaterInterface> fw_updater,
                std::unique_ptr<PairManagerInterface> pair_manager);

  template <typename HammerUpdaterType>
  friend class HammerUpdaterTest;

 private:
  // The image data to be updated.
  std::string image_;
  // The main firmware updater.
  std::unique_ptr<FirmwareUpdaterInterface> fw_updater_;
  // The pairing manager.
  std::unique_ptr<PairManagerInterface> pair_manager_;

  DISALLOW_COPY_AND_ASSIGN(HammerUpdater);
};

}  // namespace hammerd
#endif  // HAMMERD_HAMMER_UPDATER_H_
