// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_DBUS_SERVICE_H_
#define DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_DBUS_SERVICE_H_

#include <string>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/errors/error.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>

namespace diagnostics {

// Implements the "org.chromium.DiagnosticsdInterface" D-Bus interface exposed
// by the diagnosticsd daemon (see constants for the API methods at
// src/platform/system_api/dbus/diagnosticsd/dbus-constants.h).
class DiagnosticsdDBusService final {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Called when a Mojo invitation is received via a D-Bus call.
    //
    // Should start the diagnosticsd Mojo service factory that talks through the
    // pipe specified by the passed |mojo_pipe_fd|. Should return whether the
    // Mojo service factory was successfully started, and when false should fill
    // |*error_message|.
    //
    // In production the pipe's parent side end belongs to the Chrome browser
    // process.
    virtual bool StartMojoServiceFactory(base::ScopedFD mojo_pipe_fd,
                                         std::string* error_message) = 0;
  };

  explicit DiagnosticsdDBusService(Delegate* delegate);
  ~DiagnosticsdDBusService();

  // Implementation of the "org.chromium.DiagnosticsdInterface" D-Bus interface:
  bool BootstrapMojoConnection(brillo::ErrorPtr* error,
                               const base::ScopedFD& mojo_fd);

 private:
  // Implements BootstrapMojoConnection(), with the main difference in how
  // errors are returned.
  bool DoBootstrapMojoConnection(const base::ScopedFD& mojo_fd,
                                 std::string* error_message);

  // Unowned. The delegate should outlive this instance.
  Delegate* const delegate_;

  DISALLOW_COPY_AND_ASSIGN(DiagnosticsdDBusService);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DIAGNOSTICSD_DIAGNOSTICSD_DBUS_SERVICE_H_
