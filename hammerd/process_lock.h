// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HAMMERD_PROCESS_LOCK_H_
#define HAMMERD_PROCESS_LOCK_H_

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace hammerd {

// The file-based lock.
class ProcessLock {
 public:
  explicit ProcessLock(const base::FilePath& lock_file);
  virtual ~ProcessLock();
  bool IsLocked() const;
  bool Acquire();
  bool Release();

 private:
  base::FilePath lock_file_;
  base::ScopedFD fd_;
  DISALLOW_COPY_AND_ASSIGN(ProcessLock);
};

}  // namespace hammerd
#endif  // HAMMERD_PROCESS_LOCK_H_
