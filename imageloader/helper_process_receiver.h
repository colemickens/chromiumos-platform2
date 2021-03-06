// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IMAGELOADER_HELPER_PROCESS_RECEIVER_H_
#define IMAGELOADER_HELPER_PROCESS_RECEIVER_H_

#include <base/files/scoped_file.h>
#include <base/message_loop/message_loop.h>
#include <brillo/daemons/daemon.h>

#include "imageloader/ipc.pb.h"
#include "imageloader/verity_mounter.h"

using MessageLoopForIO = base::MessageLoopForIO;

struct cmsghdr;

namespace imageloader {

// Main loop for the Mount helper process.
// This object is used in the subprocess.
class HelperProcessReceiver : public brillo::Daemon,
                              public base::MessageLoopForIO::Watcher {
 public:
  explicit HelperProcessReceiver(base::ScopedFD control_fd);

  // Helper function defined in helper_process_receiver_fuzzer.cc.
  friend void helper_process_receiver_fuzzer_run(const uint8_t*, size_t);

 protected:
  // Overrides Daemon init callback.
  int OnInit() override;

  // Overrides MessageLoopForIO callbacks for new data on |control_fd_|.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override {}

 private:
  CommandResponse HandleCommand(const ImageCommand& image_command,
                                struct cmsghdr* cmsg);
  void SendResponse(const CommandResponse& response);

  base::ScopedFD control_fd_;
  MessageLoopForIO::FileDescriptorWatcher control_watcher_;
  int pending_fd_;
  VerityMounter mounter_;

  DISALLOW_COPY_AND_ASSIGN(HelperProcessReceiver);
};

}  // namespace imageloader

#endif  // IMAGELOADER_HELPER_PROCESS_RECEIVER_H_
