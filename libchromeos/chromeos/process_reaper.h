// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBCHROMEOS_CHROMEOS_PROCESS_REAPER_H_
#define LIBCHROMEOS_CHROMEOS_PROCESS_REAPER_H_

#include <sys/wait.h>

#include <map>

#include <base/callback.h>
#include <base/location.h>
#include <base/macros.h>
#include <chromeos/asynchronous_signal_handler.h>
#include <chromeos/daemons/daemon.h>

namespace chromeos {

class CHROMEOS_EXPORT ProcessReaper final {
 public:
  // The callback called when a child exits.
  using ChildCallback = base::Callback<void(const siginfo_t&)>;

  ProcessReaper() = default;
  ~ProcessReaper();

  // Register the ProcessReaper using either the provided
  // chromeos::AsynchronousSignalHandlerInterface. You can call Unregister() to
  // remove this ProcessReapper or it will be called during shutdown.
  // You can only register this ProcessReaper with one signal handler at a time.
  void Register(AsynchronousSignalHandlerInterface* async_signal_handler);

  // Unregisters the ProcessReaper from the
  // chromeos::AsynchronousSignalHandlerInterface passed in Register(). It
  // doesn't do anything if not registered.
  void Unregister();

  // Watch for the child process |pid| to finish and call |callback| when the
  // selected process exits or the process terminates for other reason. The
  // |callback| receives the exit status and exit code of the terminated process
  // as a siginfo_t. See wait(2) for details about siginfo_t.
  bool WatchForChild(const tracked_objects::Location& from_here,
                     pid_t pid,
                     const ChildCallback& callback);

 private:
  // SIGCHLD handler for the AsynchronousSignalHandler. Always returns false
  // (meaning that the signal handler should not be unregistered).
  bool HandleSIGCHLD(const signalfd_siginfo& sigfd_info);

  struct WatchedProcess {
    tracked_objects::Location location;
    ChildCallback callback;
  };
  std::map<pid_t, WatchedProcess> watched_processes_;

  // The |async_signal_handler_| is owned by the caller and is |nullptr| when
  // not registered.
  AsynchronousSignalHandlerInterface* async_signal_handler_{nullptr};

  DISALLOW_COPY_AND_ASSIGN(ProcessReaper);
};

}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_PROCESS_REAPER_H_
