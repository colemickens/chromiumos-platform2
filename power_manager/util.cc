// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/util.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "power_manager/power_constants.h"

namespace {

const char kWakeupCountPath[] = "/sys/power/wakeup_count";

}  // namespace

namespace power_manager {
namespace util {

bool OOBECompleted() {
  return access("/home/chronos/.oobe_completed", F_OK) == 0;
}

void Launch(const char* cmd) {
  LOG(INFO) << "Launching " << cmd;
  pid_t pid = fork();
  if (pid == 0) {
    // Detach from parent so that powerd doesn't need to wait around for us
    setsid();
    exit(fork() == 0 ? system(cmd) : 0);
  } else if (pid > 0) {
    waitpid(pid, NULL, 0);
  }
}

void CreateStatusFile(const FilePath& file) {
  if (!file_util::WriteFile(file, NULL, 0) == -1)
    LOG(ERROR) << "Unable to create " << file.value();
  else
    LOG(INFO) << "Created " << file.value();
}

void RemoveStatusFile(const FilePath& file) {
  if (file_util::PathExists(file)) {
    if (!file_util::Delete(file, false))
      LOG(ERROR) << "Unable to remove " << file.value();
    else
      LOG(INFO) << "Removed " << file.value();
  }
}

bool GetWakeupCount(unsigned int *value) {
  int64 temp_value;
  FilePath path(kWakeupCountPath);
  std::string buf;
  if (file_util::ReadFileToString(path, &buf)) {
    TrimWhitespaceASCII(buf, TRIM_TRAILING, &buf);
    if (base::StringToInt64(buf, &temp_value)) {
      *value = static_cast<unsigned int>(temp_value);
      return true;
    } else {
      LOG(ERROR) << "Garbage found in " << path.value();
    }
  }
  LOG(INFO) << "Could not read " << path.value();
  return false;
}

}  // namespace util
}  // namespace power_manager
