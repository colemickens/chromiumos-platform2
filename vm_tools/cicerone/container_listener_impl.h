// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_
#define VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_

#include <stdint.h>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/sequenced_task_runner.h>
#include <base/time/time.h>
#include <grpc++/grpc++.h>
#include <vm_applications/proto_bindings/apps.pb.h>

#include "container_host.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace cicerone {

class Service;

// gRPC server implementation for receiving messages from a container in a VM.
class ContainerListenerImpl final
    : public vm_tools::container::ContainerListener::Service {
 public:
  explicit ContainerListenerImpl(
      base::WeakPtr<vm_tools::cicerone::Service> service);
  ~ContainerListenerImpl() override = default;

  // ContainerListener overrides.
  grpc::Status ContainerReady(
      grpc::ServerContext* ctx,
      const vm_tools::container::ContainerStartupInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status ContainerShutdown(
      grpc::ServerContext* ctx,
      const vm_tools::container::ContainerShutdownInfo* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status UpdateApplicationList(
      grpc::ServerContext* ctx,
      const vm_tools::container::UpdateApplicationListRequest* request,
      vm_tools::EmptyMessage* response) override;
  grpc::Status OpenUrl(grpc::ServerContext* ctx,
                       const vm_tools::container::OpenUrlRequest* request,
                       vm_tools::EmptyMessage* response) override;

 private:
  base::WeakPtr<vm_tools::cicerone::Service> service_;  // not owned
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // We rate limit the requests to open a URL in Chrome to prevent an
  // accidental DOS of Chrome from a bad script in Linux. We use a fixed window
  // rate control algorithm to do this.
  uint32_t open_url_count_;
  base::TimeTicks open_url_rate_window_start_;

  DISALLOW_COPY_AND_ASSIGN(ContainerListenerImpl);
};

}  // namespace cicerone
}  // namespace vm_tools

#endif  // VM_TOOLS_CICERONE_CONTAINER_LISTENER_IMPL_H_