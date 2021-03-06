// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/capability.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/syslog_logging.h>
#include <libminijail.h>
#include <metrics/metrics_library.h>
#include <scoped_minijail.h>

#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/crash_sender_util.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

namespace {

// Records that the crash sending is done.
void RecordCrashDone() {
  if (util::IsMock()) {
    // For testing purposes, emit a message to log so that we
    // know when the test has received all the messages from this run.
    // The string is referenced in
    // third_party/autotest/files/client/cros/crash/crash_test.py
    LOG(INFO) << "crash_sender done. (mock)";
  }
}

// Sets up the minijail sandbox.
//
// crash_sender currently needs to run as root:
// - System crash reports in /var/spool/crash are owned by root.
// - User crash reports in /home/chronos/ are owned by chronos.
//
// crash_sender needs network access in order to upload things.
//
void SetUpSandbox(struct minijail* jail) {
  // Keep CAP_DAC_OVERRIDE in order to access non-root paths.
  minijail_use_caps(jail, CAP_TO_MASK(CAP_DAC_OVERRIDE));
  // Set ambient capabilities because crash_sender runs other programs.
  // TODO(satorux): Remove this once the code is entirely C++.
  minijail_set_ambient_caps(jail);
  minijail_no_new_privs(jail);
  minijail_namespace_ipc(jail);
  minijail_namespace_pids(jail);
  minijail_remount_proc_readonly(jail);
  minijail_namespace_vfs(jail);
  minijail_mount_tmp(jail);
  minijail_namespace_uts(jail);
  minijail_forward_signals(jail);
}

// Locks on the file, or exits if locking fails.
void LockOrExit(const base::File& lock_file) {
  if (flock(lock_file.GetPlatformFile(), LOCK_EX | LOCK_NB) != 0) {
    if (errno == EWOULDBLOCK) {
      LOG(INFO) << "Already running; quitting.";
    } else {
      PLOG(ERROR) << "Failed to acquire a lock.";
    }
    RecordCrashDone();
    exit(EXIT_FAILURE);
  }
}

// Runs the main function for the child process.
int RunChildMain(int argc, char* argv[]) {
  // Ensure only one instance of crash_sender runs at the same time.
  base::File lock_file(paths::Get(paths::kLockFile),
                       base::File::FLAG_OPEN_ALWAYS);
  LockOrExit(lock_file);

  util::ParseCommandLine(argc, argv);

  if (util::ShouldPauseSending()) {
    LOG(INFO) << "Exiting early due to " << paths::kPauseCrashSending;
    return EXIT_FAILURE;
  }

  if (util::IsTestImage()) {
    LOG(INFO) << "Exiting early due to test image.";
    return EXIT_FAILURE;
  }

  base::FilePath missing_path;
  if (!util::CheckDependencies(&missing_path)) {
    LOG(ERROR) << "Crash sending disabled: " << missing_path.value()
               << " not found.";
    return EXIT_FAILURE;
  }

  auto metrics_lib = std::make_unique<MetricsLibrary>();
  metrics_lib->Init();
  util::Sender::Options options;
  util::Sender sender(std::move(metrics_lib), options);
  if (!sender.Init()) {
    LOG(ERROR) << "Failed to initialize util::Sender";
    return EXIT_FAILURE;
  }

  // Send system-wide crashes.
  if (!sender.SendCrashes(paths::Get(paths::kSystemCrashDirectory)))
    LOG(ERROR) << "Skipped: " << paths::kSystemCrashDirectory;

  // Send user-specific crashes in the fallback diriectory.
  if (!sender.SendCrashes(paths::Get(paths::kFallbackUserCrashDirectory)))
    LOG(ERROR) << "Skipped: " << paths::kFallbackUserCrashDirectory;

  // Send user-specific crashes.
  sender.SendUserCrashes();

  return EXIT_SUCCESS;
}

// Cleans up. This function runs in the parent process (not sandboxed), hence
// should be very minimal. No need to delete temporary files manually in /tmp,
// that's a unique tmpfs provided by minijail, that'll automatically go away
// when the child process is terminated.
void CleanUp() {
  RecordCrashDone();
}

}  // namespace

int main(int argc, char* argv[]) {
  // Log to syslog (/var/log/messages), and stderr if stdin is a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  // Register the cleanup function to be called at exit.
  atexit(&CleanUp);

  // Set up a sandbox, and jail the child process.
  ScopedMinijail jail(minijail_new());
  SetUpSandbox(jail.get());
  const pid_t pid = minijail_fork(jail.get());

  if (pid == 0)
    return RunChildMain(argc, argv);

  // We rely on the child handling its own exit status, and a non-zero status
  // isn't necessarily a bug (e.g. if mocked out that way).  Only warn for an
  // internal error.
  const int status = minijail_wait(jail.get());
  LOG_IF(ERROR, status < 0)
      << "Child process " << pid << " did not finish cleanly: " << status;
  return status;
}
