// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/errors/error.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/diagnosticsd/diagnosticsd_dbus_service.h"
#include "diagnostics/diagnosticsd/mojo_test_utils.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::WithArg;

namespace diagnostics {

namespace {

class MockDiagnosticsdDBusServiceDelegate
    : public DiagnosticsdDBusService::Delegate {
 public:
  // Delegate overrides:
  bool StartMojoServiceFactory(base::ScopedFD mojo_pipe_fd,
                               std::string* error_message) override {
    // Redirect to a separate mockable method to workaround GMock's issues with
    // move-only parameters.
    return StartMojoServiceFactoryImpl(mojo_pipe_fd.get(), error_message);
  }

  MOCK_METHOD2(StartMojoServiceFactoryImpl,
               bool(int mojo_pipe_fd, std::string* error_message));
};

// Tests for the DiagnosticsdDBusService class.
class DiagnosticsdDBusServiceTest : public testing::Test {
 protected:
  StrictMock<MockDiagnosticsdDBusServiceDelegate> delegate_;
  DiagnosticsdDBusService service_{&delegate_};
};

}  // namespace

// Test that BootstrapMojoConnection() successfully calls into the delegate
// method when called with a valid file descriptor.
TEST_F(DiagnosticsdDBusServiceTest, BootstrapMojoConnectionBasic) {
  const FakeMojoFdGenerator fake_mojo_fd_generator;

  EXPECT_CALL(delegate_, StartMojoServiceFactoryImpl(_, _))
      .WillOnce(DoAll(
          WithArg<0 /* mojo_pipe_fd */>(
              Invoke([&fake_mojo_fd_generator](int mojo_pipe_fd) {
                EXPECT_TRUE(fake_mojo_fd_generator.IsDuplicateFd(mojo_pipe_fd));
              })),
          Return(true)));

  brillo::ErrorPtr error;
  EXPECT_TRUE(service_.BootstrapMojoConnection(
      &error, fake_mojo_fd_generator.MakeFd()));
  EXPECT_FALSE(error);
}

// Test that BootstrapMojoConnection() fails when an empty file descriptor is
// supplied.
TEST_F(DiagnosticsdDBusServiceTest, BootstrapMojoConnectionInvalidFd) {
  brillo::ErrorPtr error;
  EXPECT_FALSE(service_.BootstrapMojoConnection(
      &error, base::ScopedFD() /* mojo_pipe_fd */));
  EXPECT_TRUE(error);
}

}  // namespace diagnostics
