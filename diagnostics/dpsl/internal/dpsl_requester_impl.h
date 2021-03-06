// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DPSL_INTERNAL_DPSL_REQUESTER_IMPL_H_
#define DIAGNOSTICS_DPSL_INTERNAL_DPSL_REQUESTER_IMPL_H_

#include <functional>
#include <memory>
#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/sequence_checker_impl.h>

#include "diagnostics/dpsl/public/dpsl_requester.h"
#include "diagnostics/grpc_async_adapter/async_grpc_client.h"

#include "diagnosticsd.grpc.pb.h"  // NOLINT(build/include)

namespace diagnostics {

// Real implementation of the DpslRequester interface.
class DpslRequesterImpl final : public DpslRequester {
 public:
  explicit DpslRequesterImpl(const std::string& diagnosticsd_grpc_uri);
  ~DpslRequesterImpl() override;

  // DpslRequester overrides:
  void SendMessageToUi(
      std::unique_ptr<grpc_api::SendMessageToUiRequest> request,
      SendMessageToUiCallback callback) override;
  void GetProcData(std::unique_ptr<grpc_api::GetProcDataRequest> request,
                   GetProcDataCallback callback) override;
  void GetSysfsData(std::unique_ptr<grpc_api::GetSysfsDataRequest> request,
                    GetSysfsDataCallback callback) override;
  void PerformWebRequest(
      std::unique_ptr<grpc_api::PerformWebRequestParameter> request,
      PerformWebRequestCallback callback) override;

 private:
  using AsyncGrpcDiagnosticsdClient = AsyncGrpcClient<grpc_api::Diagnosticsd>;

  // Posts a task to the main thread that runs CallGrpcClientMethod() with the
  // specified arguments.
  //
  // Note: this method can be called from any thread.
  template <typename GrpcStubMethod,
            typename RequestType,
            typename ResponseType>
  void ScheduleGrpcClientMethodCall(
      const tracked_objects::Location& location,
      GrpcStubMethod grpc_stub_method,
      std::unique_ptr<RequestType> request,
      std::function<void(std::unique_ptr<ResponseType>)> response_callback);

  // Runs |async_grpc_client_|'s CallRpc() method with the specified arguments.
  template <typename GrpcStubMethod,
            typename RequestType,
            typename ResponseType>
  void CallGrpcClientMethod(
      GrpcStubMethod grpc_stub_method,
      std::unique_ptr<RequestType> request,
      base::Callback<void(std::unique_ptr<ResponseType>)> response_callback);

  // Message loop of the main thread (on which this instance was created).
  base::MessageLoop* const message_loop_;

  AsyncGrpcDiagnosticsdClient async_grpc_client_;

  // Whether Shutdown() was already called on |async_grpc_client_|.
  bool async_grpc_client_shutting_down_ = false;

  base::SequenceCheckerImpl sequence_checker_;

  // Must be the last member.
  base::WeakPtrFactory<DpslRequesterImpl> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DpslRequesterImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DPSL_INTERNAL_DPSL_REQUESTER_IMPL_H_
