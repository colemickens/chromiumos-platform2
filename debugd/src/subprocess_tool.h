// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_SUBPROCESS_TOOL_H_
#define DEBUGD_SRC_SUBPROCESS_TOOL_H_

#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>

#include "debugd/src/process_with_id.h"

namespace debugd {

class SubprocessTool {
 public:
  SubprocessTool() = default;
  virtual ~SubprocessTool() = default;

  virtual ProcessWithId* CreateProcess(bool sandboxed,
                                       bool allow_root_mount_ns);

  // TODO(vapier): Rework sandboxing so we can re-internalize this function.
  bool RecordProcess(std::unique_ptr<ProcessWithId> process);

  virtual bool Stop(const std::string& handle, brillo::ErrorPtr* error);

 private:
  std::map<std::string, std::unique_ptr<ProcessWithId>> processes_;

  DISALLOW_COPY_AND_ASSIGN(SubprocessTool);
};

}  // namespace debugd

#endif  // DEBUGD_SRC_SUBPROCESS_TOOL_H_
