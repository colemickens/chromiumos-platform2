//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef SHILL_IPCONFIG_H_
#define SHILL_IPCONFIG_H_

#include <memory>
#include <string>
#include <sys/time.h>
#include <vector>

#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/net/ip_address.h"
#include "shill/property_store.h"
#include "shill/refptr_types.h"
#include "shill/routing_table_entry.h"

namespace shill {
class ControlInterface;
class Error;
class IPConfigAdaptorInterface;
class StaticIPParameters;
class Time;

// IPConfig superclass. Individual IP configuration types will inherit from this
// class.
class IPConfig : public base::RefCounted<IPConfig> {
 public:
  struct Route {
    Route() : prefix(0) {}
    Route(const std::string& host_in,
          int prefix_in,
          const std::string& gateway_in)
        : host(host_in), prefix(prefix_in), gateway(gateway_in) {}
    std::string host;
    int prefix;
    std::string gateway;
  };

  struct Properties {
    Properties() : address_family(IPAddress::kFamilyUnknown),
                   subnet_prefix(0),
                   default_route(true),
                   blackhole_ipv6(false),
                   mtu(kUndefinedMTU),
                   lease_duration_seconds(0) {}

    IPAddress::Family address_family;
    std::string address;
    int32_t subnet_prefix;
    std::string broadcast_address;
    std::vector<std::string> dns_servers;
    std::string domain_name;
    std::string accepted_hostname;
    std::vector<std::string> domain_search;
    std::string gateway;
    std::string method;
    std::string peer_address;
    // Each map represents a single address or prefix in a DHCPv6 lease.
    // Multiple addresses can be returned with different lifetimes, for example
    // when aging out an old prefix and switching to a new one.  The available
    // keys are all of the form "kDhcpv6*".
    // IPv6 addresses delegated from a DHCPv6 server.
    Stringmaps dhcpv6_addresses;
    // IPv6 prefix delegated from a DHCPv6 server.
    Stringmaps dhcpv6_delegated_prefixes;
    // If |allowed_uids| and/or |allowed_iifs| is set, IP policy rules will
    // be created so that only traffic from the whitelisted UIDs and/or
    // input interfaces can use this connection.  If neither is set,
    // all system traffic can use this connection.
    std::vector<uint32_t> allowed_uids;
    std::vector<std::string> allowed_iifs;
    // List of uids that have their traffic blocked.
    std::vector<uint32_t> blackholed_uids;
    // Set the flag to true when the interface should be set as the default
    // route.
    bool default_route;
    // A list of IP blocks in CIDR format that should be excluded from VPN.
    std::vector<std::string> exclusion_list;
    // Block IPv6 traffic.  Used if connected to an IPv4-only VPN.
    bool blackhole_ipv6;
    // MTU to set on the interface.  If unset, defaults to |kDefaultMTU|.
    int32_t mtu;
    // A list of (host,prefix,gateway) tuples for this connection.
    std::vector<Route> routes;
    // Vendor encapsulated option string gained from DHCP.
    ByteArray vendor_encapsulated_options;
    // iSNS option data gained from DHCP.
    ByteArray isns_option_data;
    // Web Proxy Auto Discovery (WPAD) URL gained from DHCP.
    std::string web_proxy_auto_discovery;
    // Length of time the lease was granted.
    uint32_t lease_duration_seconds;
  };

  enum Method {
    kMethodUnknown,
    kMethodPPP,
    kMethodStatic,
    kMethodDHCP
  };

  enum ReleaseReason {
    kReleaseReasonDisconnect,
    kReleaseReasonStaticIP
  };

  using UpdateCallback = base::Callback<void(const IPConfigRefPtr&, bool)>;
  using Callback = base::Callback<void(const IPConfigRefPtr&)>;

  // Define a default and a minimum viable MTU value.
  static const int kDefaultMTU;
  static const int kMinIPv4MTU;
  static const int kMinIPv6MTU;
  static const int kUndefinedMTU;

  IPConfig(ControlInterface* control_interface, const std::string& device_name);
  IPConfig(ControlInterface* control_interface,
           const std::string& device_name,
           const std::string& type);
  virtual ~IPConfig();

  const std::string& device_name() const { return device_name_; }
  const std::string& type() const { return type_; }
  uint32_t serial() const { return serial_; }

  std::string GetRpcIdentifier() const;

  // Registers a callback that's executed every time the configuration
  // properties are acquired. Takes ownership of |callback|.  Pass NULL
  // to remove a callback. The callback's first argument is a pointer to this IP
  // configuration instance allowing clients to more easily manage multiple IP
  // configurations. The callback's second argument is a boolean indicating
  // whether or not a DHCP lease was acquired from the server.
  void RegisterUpdateCallback(const UpdateCallback& callback);

  // Registers a callback that's executed every time the configuration
  // properties fail to be acquired. Takes ownership of |callback|.  Pass NULL
  // to remove a callback. The callback's argument is a pointer to this IP
  // configuration instance allowing clients to more easily manage multiple IP
  // configurations.
  void RegisterFailureCallback(const Callback& callback);

  // Registers a callback that's executed every time the Refresh method
  // on the ipconfig is called.  Takes ownership of |callback|. Pass NULL
  // to remove a callback. The callback's argument is a pointer to this IP
  // configuration instance allowing clients to more easily manage multiple IP
  // configurations.
  void RegisterRefreshCallback(const Callback& callback);

  // Registers a callback that's executed every time the the lease exipres
  // and the IPConfig is about to perform a restart to attempt to regain it.
  // Takes ownership of |callback|. Pass NULL  to remove a callback. The
  // callback's argument is a pointer to this IP configuration instance
  // allowing clients to more easily manage multiple IP configurations.
  void RegisterExpireCallback(const Callback& callback);

  void set_properties(const Properties& props) { properties_ = props; }
  virtual const Properties& properties() const { return properties_; }

  // Update DNS servers setting for this ipconfig, this allows Chrome
  // to retrieve the new DNS servers.
  virtual void UpdateDNSServers(const std::vector<std::string>& dns_servers);

  // Reset the IPConfig properties to their default values.
  virtual void ResetProperties();

  // Request, renew and release IP configuration. Return true on success, false
  // otherwise. The default implementation always returns false indicating a
  // failure.  ReleaseIP is advisory: if we are no longer connected, it is not
  // possible to properly vacate the lease on the remote server.  Also,
  // depending on the configuration of the specific IPConfig subclass, we may
  // end up holding on to the lease so we can resume to the network lease
  // faster.
  virtual bool RequestIP();
  virtual bool RenewIP();
  virtual bool ReleaseIP(ReleaseReason reason);

  // Refresh IP configuration.  Called by the DBus Adaptor "Refresh" call.
  void Refresh(Error* error);

  PropertyStore* mutable_store() { return &store_; }
  const PropertyStore& store() const { return store_; }
  void ApplyStaticIPParameters(StaticIPParameters* static_ip_parameters);

  // Restore the fields of |properties_| to their original values before
  // static IP parameters were previously applied.
  void RestoreSavedIPParameters(StaticIPParameters* static_ip_parameters);

  // Updates |current_lease_expiration_time_| by adding |new_lease_duration| to
  // the current time.
  virtual void UpdateLeaseExpirationTime(uint32_t new_lease_duration);

  // Resets |current_lease_expiration_time_| to its default value.
  virtual void ResetLeaseExpirationTime();

  // Returns the time left (in seconds) till the current DHCP lease is to be
  // renewed in |time_left|. Returns false if an error occurs (i.e. current
  // lease has already expired or no current DHCP lease), true otherwise.
  bool TimeToLeaseExpiry(uint32_t* time_left);

  // Returns whether the function call changed the configuration.
  bool SetBlackholedUids(const std::vector<uint32_t>& uids);
  bool ClearBlackholedUids();

 protected:
  // Inform RPC listeners of changes to our properties. MAY emit
  // changes even on unchanged properties.
  virtual void EmitChanges();

  // Updates the IP configuration properties and notifies registered listeners
  // about the event.
  virtual void UpdateProperties(const Properties& properties,
                                bool new_lease_acquired);

  // Notifies registered listeners that the configuration process has failed.
  virtual void NotifyFailure();

  // Notifies registered listeners that the lease has expired.
  virtual void NotifyExpiry();

 private:
  friend class IPConfigAdaptorInterface;
  friend class IPConfigTest;
  friend class ConnectionTest;

  FRIEND_TEST(DeviceTest, AcquireIPConfigWithoutSelectedService);
  FRIEND_TEST(DeviceTest, AcquireIPConfigWithSelectedService);
  FRIEND_TEST(DeviceTest, DestroyIPConfig);
  FRIEND_TEST(DeviceTest, IsConnectedViaTether);
  FRIEND_TEST(DeviceTest, OnIPConfigExpired);
  FRIEND_TEST(IPConfigTest, UpdateProperties);
  FRIEND_TEST(IPConfigTest, UpdateLeaseExpirationTime);
  FRIEND_TEST(IPConfigTest, TimeToLeaseExpiry_NoDHCPLease);
  FRIEND_TEST(IPConfigTest, TimeToLeaseExpiry_CurrentLeaseExpired);
  FRIEND_TEST(IPConfigTest, TimeToLeaseExpiry_Success);
  FRIEND_TEST(ResolverTest, Empty);
  FRIEND_TEST(ResolverTest, NonEmpty);
  FRIEND_TEST(RoutingTableTest, ConfigureRoutes);
  FRIEND_TEST(RoutingTableTest, RouteAddDelete);

  static const char kType[];

  static uint32_t global_serial_;
  PropertyStore store_;
  const std::string device_name_;
  const std::string type_;
  const uint32_t serial_;
  std::unique_ptr<IPConfigAdaptorInterface> adaptor_;
  Properties properties_;
  UpdateCallback update_callback_;
  Callback failure_callback_;
  Callback refresh_callback_;
  Callback expire_callback_;
  struct timeval current_lease_expiration_time_;
  Time* time_;

  DISALLOW_COPY_AND_ASSIGN(IPConfig);
};

}  // namespace shill

#endif  // SHILL_IPCONFIG_H_
