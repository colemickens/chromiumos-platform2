// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_
#define VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_

#include <stdint.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <brillo/process.h>

#include "vm_tools/concierge/mac_address_generator.h"
#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/subnet.h"
#include "vm_tools/concierge/subnet_pool.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

#include "vm_guest.grpc.pb.h"  // NOLINT(build/include)

namespace vm_tools {
namespace concierge {

// Represents a single instance of a running virtual machine.
class VirtualMachine {
 public:
  // Type of a disk image.
  enum class DiskImageType {
    // Raw disk image file.
    RAW,

    // QCOW2 disk image.
    QCOW2,
  };

  // Describes a disk image to be mounted inside the VM.
  struct Disk {
    // Path to the disk image on the host.
    base::FilePath path;

    // Whether the disk should be writable by the VM.
    bool writable;
  };

  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::unique_ptr<VirtualMachine> Create(
      base::FilePath kernel,
      base::FilePath rootfs,
      std::vector<Disk> disks,
      MacAddress mac_addr,
      std::unique_ptr<Subnet> subnet,
      uint32_t vsock_cid,
      std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
      base::FilePath runtime_dir);
  ~VirtualMachine();

  // Shuts down the VM.  First attempts a clean shutdown of the VM by sending
  // a Shutdown RPC to maitre'd.  If that fails, attempts to shut down the VM
  // using the control socket for the hypervisor.  If that fails, then sends a
  // SIGTERM to the hypervisor.  Finally, if nothing works forcibly stops the VM
  // by sending it a SIGKILL.  Returns true if the VM was shut down and false
  // otherwise.
  bool Shutdown();

  // Configures the network interfaces inside the VM.  Returns true iff
  // successful.
  bool ConfigureNetwork(const std::vector<std::string>& nameservers,
                        const std::vector<std::string>& search_domains);

  // Mounts a file system inside the VM.  Both |source| (if it is a file path)
  // and |target| must be valid paths inside the VM.  Returns true on success.
  bool Mount(std::string source,
             std::string target,
             std::string fstype,
             uint64_t mountflags,
             std::string options);

  // Starts Termina-specific services in the guest.
  bool StartTermina(std::string lxd_subnet, std::string* out_error);

  // Mount a 9p file system inside the VM.  The guest VM connects to a server
  // listening on the vsock port |port| and mounts the file system on |target|.
  bool Mount9P(uint32_t port, std::string target);

  // Sets the resolv.conf in the VM to |config|. Returns true if successful,
  // false if the resolv.conf in the guest could not be updated.
  bool SetResolvConfig(const std::vector<std::string>& nameservers,
                       const std::vector<std::string>& search_domains);

  // Set the guest time to the current time as given by gettimeofday.
  grpc::Status SetTime();

  // Sets the container subnet for this VM to |subnet|. This subnet is intended
  // to be provided to a container runtime as a DHCP pool.
  void SetContainerSubnet(std::unique_ptr<Subnet> subnet);

  // The pid of the child process.
  pid_t pid() { return process_.pid(); }

  // The VM's cid.
  uint32_t cid() const { return vsock_cid_; }

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM.  Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const {
    if (seneschal_server_proxy_) {
      return seneschal_server_proxy_->handle();
    }

    return 0;
  }

  // The IPv4 address of the VM's gateway in network byte order.
  uint32_t GatewayAddress() const;

  // The IPv4 address of the VM in network byte order.
  uint32_t IPv4Address() const;

  // The netmask of the VM's subnet in network byte order.
  uint32_t Netmask() const;

  // The VM's container subnet netmask in network byte order. Returns INADDR_ANY
  // if there is no container subnet.
  uint32_t ContainerNetmask() const;

  // The VM's container subnet prefix. Returns 0 if there is no container
  // subnet.
  size_t ContainerPrefix() const;

  // The first address in the VM's container subnet in network byte order.
  // Returns INADDR_ANY if there is no container subnet.
  uint32_t ContainerSubnet() const;

  // Whether a TremplinStartedSignal has been received for the VM.
  bool IsTremplinStarted() const { return is_tremplin_started_; }
  void SetTremplinStarted() { is_tremplin_started_ = true; }

  static std::unique_ptr<VirtualMachine> CreateForTesting(
      MacAddress mac_addr,
      std::unique_ptr<Subnet> subnet,
      uint32_t vsock_cid,
      base::FilePath runtime_dir,
      std::unique_ptr<vm_tools::Maitred::Stub> stub);

 private:
  VirtualMachine(MacAddress mac_addr,
                 std::unique_ptr<Subnet> subnet,
                 uint32_t vsock_cid,
                 std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
                 base::FilePath runtime_dir);

  // Starts the VM with the given kernel and root file system.
  bool Start(base::FilePath kernel,
             base::FilePath rootfs,
             std::vector<Disk> disks);

  void set_stub_for_testing(std::unique_ptr<vm_tools::Maitred::Stub> stub);

  // EUI-48 mac address for the VM's network interface.
  MacAddress mac_addr_;

  // The /30 subnet assigned to the VM.
  std::unique_ptr<Subnet> subnet_;

  // An optional /28 container subnet.
  std::unique_ptr<Subnet> container_subnet_;

  // Virtual socket context id to be used when communicating with this VM.
  uint32_t vsock_cid_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Runtime directory for this VM.
  base::ScopedTempDir runtime_dir_;

  // Handle to the VM process.
  brillo::ProcessImpl process_;

  // Handle to logger(1) process.
  brillo::ProcessImpl logger_process_;

  // Stub for making RPC requests to the maitre'd process inside the VM.
  std::unique_ptr<vm_tools::Maitred::Stub> stub_;

  // Whether a TremplinStartedSignal has been received for the VM.
  bool is_tremplin_started_ = false;

  DISALLOW_COPY_AND_ASSIGN(VirtualMachine);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VIRTUAL_MACHINE_H_
