// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_METRICS_H_
#define SHILL_METRICS_H_

#include <list>
#include <map>
#include <string>

#include <base/memory/scoped_vector.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

#include "shill/event_dispatcher.h"
#include "shill/ieee80211.h"
#include "shill/portal_detector.h"
#include "shill/power_manager.h"
#include "shill/refptr_types.h"
#include "shill/service.h"

namespace shill {

class WiFiMainTest;
class WiFiService;

class Metrics {
 public:
  enum WiFiChannel {
    kWiFiChannelUndef = 0,
    kWiFiChannel2412 = 1,
    kWiFiChannel2417 = 2,
    kWiFiChannel2422 = 3,
    kWiFiChannel2427 = 4,
    kWiFiChannel2432 = 5,
    kWiFiChannel2437 = 6,
    kWiFiChannel2442 = 7,
    kWiFiChannel2447 = 8,
    kWiFiChannel2452 = 9,
    kWiFiChannel2457 = 10,
    kWiFiChannel2462 = 11,
    kWiFiChannel2467 = 12,
    kWiFiChannel2472 = 13,
    kWiFiChannel2484 = 14,

    kWiFiChannel5180 = 15,
    kWiFiChannel5200 = 16,
    kWiFiChannel5220 = 17,
    kWiFiChannel5240 = 18,
    kWiFiChannel5260 = 19,
    kWiFiChannel5280 = 20,
    kWiFiChannel5300 = 21,
    kWiFiChannel5320 = 22,

    kWiFiChannel5500 = 23,
    kWiFiChannel5520 = 24,
    kWiFiChannel5540 = 25,
    kWiFiChannel5560 = 26,
    kWiFiChannel5580 = 27,
    kWiFiChannel5600 = 28,
    kWiFiChannel5620 = 29,
    kWiFiChannel5640 = 30,
    kWiFiChannel5660 = 31,
    kWiFiChannel5680 = 32,
    kWiFiChannel5700 = 33,

    kWiFiChannel5745 = 34,
    kWiFiChannel5765 = 35,
    kWiFiChannel5785 = 36,
    kWiFiChannel5805 = 37,
    kWiFiChannel5825 = 38,

    kWiFiChannel5170 = 39,
    kWiFiChannel5190 = 40,
    kWiFiChannel5210 = 41,
    kWiFiChannel5230 = 42,

    /* NB: ignore old 11b bands 2312..2372 and 2512..2532 */
    /* NB: ignore regulated bands 4920..4980 and 5020..5160 */
    kWiFiChannelMax
  };

  enum WiFiNetworkPhyMode {
    kWiFiNetworkPhyModeUndef = 0,    // Unknown/undefined
    kWiFiNetworkPhyMode11a = 1,      // 802.11a
    kWiFiNetworkPhyMode11b = 2,      // 802.11b
    kWiFiNetworkPhyMode11g = 3,      // 802.11g
    kWiFiNetworkPhyMode11n = 4,      // 802.11n
    kWiFiNetworkPhyModeHalf = 5,     // PSB Half-width
    kWiFiNetworkPhyModeQuarter = 6,  // PSB Quarter-width
    kWiFiNetworkPhyModeTurbo = 7,    // Atheros Turbo mode

    kWiFiNetworkPhyModeMax
  };

  enum EapOuterProtocol {
    kEapOuterProtocolUnknown = 0,
    kEapOuterProtocolLeap = 1,
    kEapOuterProtocolPeap = 2,
    kEapOuterProtocolTls = 3,
    kEapOuterProtocolTtls = 4,

    kEapOuterProtocolMax
  };

  enum EapInnerProtocol {
    kEapInnerProtocolUnknown = 0,
    kEapInnerProtocolNone = 1,
    kEapInnerProtocolPeapMd5 = 2,
    kEapInnerProtocolPeapMschapv2 = 3,
    kEapInnerProtocolTtlsEapMd5 = 4,
    kEapInnerProtocolTtlsEapMschapv2 = 5,
    kEapInnerProtocolTtlsMschapv2 = 6,
    kEapInnerProtocolTtlsMschap = 7,
    kEapInnerProtocolTtlsPap = 8,
    kEapInnerProtocolTtlsChap = 9,

    kEapInnerProtocolMax
  };

  enum WiFiSecurity {
    kWiFiSecurityUnknown = 0,
    kWiFiSecurityNone = 1,
    kWiFiSecurityWep = 2,
    kWiFiSecurityWpa = 3,
    kWiFiSecurityRsn = 4,
    kWiFiSecurity8021x = 5,
    kWiFiSecurityPsk = 6,

    kWiFiSecurityMax
  };

  enum WiFiApMode {
    kWiFiApModeUnknown = 0,
    kWiFiApModeManaged = 1,
    kWiFiApModeAdHoc = 2,

    kWiFiApModeMax
  };

  enum PortalResult {
    kPortalResultSuccess = 0,
    kPortalResultDNSFailure = 1,
    kPortalResultDNSTimeout = 2,
    kPortalResultConnectionFailure = 3,
    kPortalResultConnectionTimeout = 4,
    kPortalResultHTTPFailure = 5,
    kPortalResultHTTPTimeout = 6,
    kPortalResultContentFailure = 7,
    kPortalResultContentTimeout = 8,
    kPortalResultUnknown = 9,

    kPortalResultMax
  };

  enum LinkMonitorFailure {
    kLinkMonitorMacAddressNotFound = 0,
    kLinkMonitorClientStartFailure = 1,
    kLinkMonitorTransmitFailure = 2,
    kLinkMonitorFailureThresholdReached = 3,

    kLinkMonitorFailureMax
  };

  enum WiFiStatusType {
    kStatusCodeTypeByAp,
    kStatusCodeTypeByClient,
    kStatusCodeTypeByUser,
    kStatusCodeTypeConsideredDead,
    kStatusCodeTypeMax
  };

  enum WiFiDisconnectByWhom {
    kDisconnectedByAp,
    kDisconnectedNotByAp
  };

  enum WiFiScanResult {
    kScanResultProgressiveConnected,
    kScanResultProgressiveErrorAndFullFoundNothing,
    kScanResultProgressiveErrorButFullConnected,
    kScanResultProgressiveAndFullFoundNothing,
    kScanResultProgressiveAndFullConnected,
    kScanResultFullScanFoundNothing,
    kScanResultFullScanConnected,
    kScanResultInternalError,
    kScanResultMax
  };

  enum ServiceFixupProfileType {
    kMetricServiceFixupDefaultProfile,
    kMetricServiceFixupUserProfile,
    kMetricServiceFixupMax
  };

  enum TerminationActionResult {
    kTerminationActionResultSuccess,
    kTerminationActionResultFailure,
    kTerminationActionResultMax
  };

  enum TerminationActionReason {
    kTerminationActionReasonSuspend,
    kTerminationActionReasonTerminate
  };

  enum Cellular3GPPRegistrationDelayedDrop {
    kCellular3GPPRegistrationDelayedDropPosted = 0,
    kCellular3GPPRegistrationDelayedDropCanceled = 1,
    kCellular3GPPRegistrationDelayedDropMax
  };

  enum CellularDropTechnology {
    kCellularDropTechnology1Xrtt = 0,
    kCellularDropTechnologyEdge = 1,
    kCellularDropTechnologyEvdo = 2,
    kCellularDropTechnologyGprs = 3,
    kCellularDropTechnologyGsm = 4,
    kCellularDropTechnologyHspa = 5,
    kCellularDropTechnologyHspaPlus = 6,
    kCellularDropTechnologyLte = 7,
    kCellularDropTechnologyUmts = 8,
    kCellularDropTechnologyUnknown = 9,
    kCellularDropTechnologyMax
  };

  enum CellularOutOfCreditsReason {
    kCellularOutOfCreditsReasonConnectDisconnectLoop = 0,
    kCellularOutOfCreditsReasonTxCongested = 1,
    kCellularOutOfCreditsReasonElongatedTimeWait = 2,
    kCellularOutOfCreditsReasonMax
  };

  enum CorruptedProfile {
    kCorruptedProfile = 1,
    kCorruptedProfileMax
  };

  enum VpnDriver {
    kVpnDriverOpenVpn = 0,
    kVpnDriverL2tpIpsec = 1,
    kVpnDriverMax
  };

  enum VpnRemoteAuthenticationType {
    kVpnRemoteAuthenticationTypeOpenVpnDefault = 0,
    kVpnRemoteAuthenticationTypeOpenVpnCertificate = 1,
    kVpnRemoteAuthenticationTypeL2tpIpsecDefault = 2,
    kVpnRemoteAuthenticationTypeL2tpIpsecCertificate = 3,
    kVpnRemoteAuthenticationTypeL2tpIpsecPsk = 4,
    kVpnRemoteAuthenticationTypeMax
  };

  enum VpnUserAuthenticationType {
    kVpnUserAuthenticationTypeOpenVpnNone = 0,
    kVpnUserAuthenticationTypeOpenVpnCertificate = 1,
    kVpnUserAuthenticationTypeOpenVpnUsernamePassword = 2,
    kVpnUserAuthenticationTypeOpenVpnUsernamePasswordOtp = 3,
    kVpnUserAuthenticationTypeOpenVpnUsernameToken = 7,
    kVpnUserAuthenticationTypeL2tpIpsecNone = 4,
    kVpnUserAuthenticationTypeL2tpIpsecCertificate = 5,
    kVpnUserAuthenticationTypeL2tpIpsecUsernamePassword = 6,
    kVpnUserAuthenticationTypeMax
  };

  enum UserInitiatedEvent {
    kUserInitiatedEventWifiScan = 0,
    kUserInitiatedEventReserved,
    kUserInitiatedEventMax
  };

  enum UserInitiatedConnectionResult {
    kUserInitiatedConnectionResultSuccess = 0,
    kUserInitiatedConnectionResultFailure = 1,
    kUserInitiatedConnectionResultAborted = 2,
    kUserInitiatedConnectionResultMax
  };

  enum DNSTestResult {
    kFallbackDNSTestResultSuccess = 0,
    kFallbackDNSTestResultFailure,
    kFallbackDNSTestResultMax
  };

  // Network problem detected by traffic monitor.
  enum NetworkProblem {
    kNetworkProblemCongestedTCPTxQueue = 0,
    kNetworkProblemDNSFailure,
    kNetworkProblemMax
  };

  // Device's connection status.
  enum ConnectionStatus {
    kConnectionStatusOffline = 0,
    kConnectionStatusConnected = 1,
    kConnectionStatusOnline = 2,
    kConnectionStatusMax
  };

  enum UserInitiatedConnectionFailureReason {
    kUserInitiatedConnectionFailureReasonUnknown = 0,
    kUserInitiatedConnectionFailureReasonBadPassphrase = 1,
    kUserInitiatedConnectionFailureReasonBadWEPKey = 2,
    kUserInitiatedConnectionFailureReasonConnect = 3,
    kUserInitiatedConnectionFailureReasonDHCP = 4,
    kUserInitiatedConnectionFailureReasonDNSLookup = 5,
    kUserInitiatedConnectionFailureReasonEAPAuthentication = 6,
    kUserInitiatedConnectionFailureReasonEAPLocalTLS = 7,
    kUserInitiatedConnectionFailureReasonEAPRemoteTLS = 8,
    kUserInitiatedConnectionFailureReasonOutOfRange = 9,
    kUserInitiatedConnectionFailureReasonPinMissing = 10,
    kUserInitiatedConnectionFailureReasonMax
  };

  enum DhcpClientStatus {
    kDhcpClientStatusArpGateway = 0,
    kDhcpClientStatusArpSelf = 1,
    kDhcpClientStatusBound = 2,
    kDhcpClientStatusDiscover = 3,
    kDhcpClientStatusIgnoreAdditionalOffer = 4,
    kDhcpClientStatusIgnoreFailedOffer = 5,
    kDhcpClientStatusIgnoreInvalidOffer = 6,
    kDhcpClientStatusIgnoreNonOffer = 7,
    kDhcpClientStatusInform = 8,
    kDhcpClientStatusInit = 9,
    kDhcpClientStatusNakDefer = 10,
    kDhcpClientStatusRebind = 11,
    kDhcpClientStatusReboot = 12,
    kDhcpClientStatusRelease = 13,
    kDhcpClientStatusRenew = 14,
    kDhcpClientStatusRequest = 15,
    kDhcpClientStatusMax
  };

  static const char kMetricDisconnectSuffix[];
  static const int kMetricDisconnectMax;
  static const int kMetricDisconnectMin;
  static const int kMetricDisconnectNumBuckets;
  static const char kMetricSignalAtDisconnectSuffix[];
  static const int kMetricSignalAtDisconnectMin;
  static const int kMetricSignalAtDisconnectMax;
  static const int kMetricSignalAtDisconnectNumBuckets;
  static const char kMetricNetworkApModeSuffix[];
  static const char kMetricNetworkChannelSuffix[];
  static const int kMetricNetworkChannelMax;
  static const char kMetricNetworkEapInnerProtocolSuffix[];
  static const int kMetricNetworkEapInnerProtocolMax;
  static const char kMetricNetworkEapOuterProtocolSuffix[];
  static const int kMetricNetworkEapOuterProtocolMax;
  static const char kMetricNetworkPhyModeSuffix[];
  static const int kMetricNetworkPhyModeMax;
  static const char kMetricNetworkSecuritySuffix[];
  static const int kMetricNetworkSecurityMax;
  static const char kMetricNetworkServiceErrors[];
  static const int kMetricNetworkServiceErrorsMax;
  static const char kMetricNetworkSignalStrengthSuffix[];
  static const int kMetricNetworkSignalStrengthMin;
  static const int kMetricNetworkSignalStrengthMax;
  static const int kMetricNetworkSignalStrengthNumBuckets;
  static const char kMetricRememberedWiFiNetworkCount[];
  static const int kMetricRememberedWiFiNetworkCountMin;
  static const int kMetricRememberedWiFiNetworkCountMax;
  static const int kMetricRememberedWiFiNetworkCountNumBuckets;
  static const char kMetricTimeOnlineSecondsSuffix[];
  static const int kMetricTimeOnlineSecondsMax;
  static const int kMetricTimeOnlineSecondsMin;
  static const int kMetricTimeOnlineSecondsNumBuckets;
  static const char kMetricTimeResumeToReadyMillisecondsSuffix[];
  static const char kMetricTimeToConfigMillisecondsSuffix[];
  static const char kMetricTimeToConnectMillisecondsSuffix[];
  static const int kMetricTimeToConnectMillisecondsMax;
  static const int kMetricTimeToConnectMillisecondsMin;
  static const int kMetricTimeToConnectMillisecondsNumBuckets;
  static const char kMetricTimeToScanAndConnectMillisecondsSuffix[];
  static const char kMetricTimeToDropSeconds[];
  static const int kMetricTimeToDropSecondsMax;
  static const int kMetricTimeToDropSecondsMin;
  static const char kMetricTimeToDisableMillisecondsSuffix[];
  static const int kMetricTimeToDisableMillisecondsMax;
  static const int kMetricTimeToDisableMillisecondsMin;
  static const int kMetricTimeToDisableMillisecondsNumBuckets;
  static const char kMetricTimeToEnableMillisecondsSuffix[];
  static const int kMetricTimeToEnableMillisecondsMax;
  static const int kMetricTimeToEnableMillisecondsMin;
  static const int kMetricTimeToEnableMillisecondsNumBuckets;
  static const char kMetricTimeToInitializeMillisecondsSuffix[];
  static const int kMetricTimeToInitializeMillisecondsMax;
  static const int kMetricTimeToInitializeMillisecondsMin;
  static const int kMetricTimeToInitializeMillisecondsNumBuckets;
  static const char kMetricTimeToJoinMillisecondsSuffix[];
  static const char kMetricTimeToOnlineMillisecondsSuffix[];
  static const char kMetricTimeToPortalMillisecondsSuffix[];
  static const char kMetricTimeToScanMillisecondsSuffix[];
  static const int kMetricTimeToScanMillisecondsMax;
  static const int kMetricTimeToScanMillisecondsMin;
  static const int kMetricTimeToScanMillisecondsNumBuckets;
  static const int kTimerHistogramMillisecondsMax;
  static const int kTimerHistogramMillisecondsMin;
  static const int kTimerHistogramNumBuckets;

  // The number of portal detections attempted for each pass.
  // This includes both failure/timeout attempts and successful attempt
  // (if any).
  static const char kMetricPortalAttemptsSuffix[];
  static const int kMetricPortalAttemptsMax;
  static const int kMetricPortalAttemptsMin;
  static const int kMetricPortalAttemptsNumBuckets;

  // The total number of portal detections attempted between the Connected
  // state and the Online state.  This includes both failure/timeout attempts
  // and the final successful attempt.
  static const char kMetricPortalAttemptsToOnlineSuffix[];
  static const int kMetricPortalAttemptsToOnlineMax;
  static const int kMetricPortalAttemptsToOnlineMin;
  static const int kMetricPortalAttemptsToOnlineNumBuckets;

  // The result of the portal detection.
  static const char kMetricPortalResultSuffix[];

  // Wifi connection frequencies.
  static const char kMetricFrequenciesConnectedEver[];
  static const int kMetricFrequenciesConnectedMax;
  static const int kMetricFrequenciesConnectedMin;
  static const int kMetricFrequenciesConnectedNumBuckets;

  static const char kMetricScanResult[];
  static const char kMetricWiFiScanTimeInEbusyMilliseconds[];

  static const char kMetricPowerManagerKey[];

  // LinkMonitor statistics.
  static const char kMetricLinkMonitorFailureSuffix[];
  static const char kMetricLinkMonitorResponseTimeSampleSuffix[];
  static const int kMetricLinkMonitorResponseTimeSampleMin;
  static const int kMetricLinkMonitorResponseTimeSampleMax;
  static const int kMetricLinkMonitorResponseTimeSampleNumBuckets;
  static const char kMetricLinkMonitorSecondsToFailureSuffix[];
  static const int kMetricLinkMonitorSecondsToFailureMin;
  static const int kMetricLinkMonitorSecondsToFailureMax;
  static const int kMetricLinkMonitorSecondsToFailureNumBuckets;
  static const char kMetricLinkMonitorBroadcastErrorsAtFailureSuffix[];
  static const char kMetricLinkMonitorUnicastErrorsAtFailureSuffix[];
  static const int kMetricLinkMonitorErrorCountMin;
  static const int kMetricLinkMonitorErrorCountMax;
  static const int kMetricLinkMonitorErrorCountNumBuckets;

  static const char kMetricLinkClientDisconnectReason[];
  static const char kMetricLinkApDisconnectReason[];
  static const char kMetricLinkClientDisconnectType[];
  static const char kMetricLinkApDisconnectType[];

  // Shill termination action statistics.
  static const char kMetricTerminationActionTimeOnTerminate[];
  static const char kMetricTerminationActionResultOnTerminate[];
  static const char kMetricTerminationActionTimeOnSuspend[];
  static const char kMetricTerminationActionResultOnSuspend[];
  static const int kMetricTerminationActionTimeMillisecondsMax;
  static const int kMetricTerminationActionTimeMillisecondsMin;

  // WiFiService Entry Fixup.
  static const char kMetricServiceFixupEntriesSuffix[];

  // Cellular specific statistics.
  static const char kMetricCellular3GPPRegistrationDelayedDrop[];
  static const char kMetricCellularAutoConnectTries[];
  static const int kMetricCellularAutoConnectTriesMax;
  static const int kMetricCellularAutoConnectTriesMin;
  static const int kMetricCellularAutoConnectTriesNumBuckets;
  static const char kMetricCellularAutoConnectTotalTime[];
  static const int kMetricCellularAutoConnectTotalTimeMax;
  static const int kMetricCellularAutoConnectTotalTimeMin;
  static const int kMetricCellularAutoConnectTotalTimeNumBuckets;
  static const char kMetricCellularDrop[];
  static const char kMetricCellularDropsPerHour[];
  static const int kMetricCellularDropsPerHourMax;
  static const int kMetricCellularDropsPerHourMin;
  static const int kMetricCellularDropsPerHourNumBuckets;
  static const char kMetricCellularFailureReason[];
  static const char kMetricCellularOutOfCreditsReason[];
  static const char kMetricCellularSignalStrengthBeforeDrop[];
  static const int kMetricCellularSignalStrengthBeforeDropMax;
  static const int kMetricCellularSignalStrengthBeforeDropMin;
  static const int kMetricCellularSignalStrengthBeforeDropNumBuckets;

  // Profile statistics.
  static const char kMetricCorruptedProfile[];

  // VPN connection statistics.
  static const char kMetricVpnDriver[];
  static const int kMetricVpnDriverMax;
  static const char kMetricVpnRemoteAuthenticationType[];
  static const int kMetricVpnRemoteAuthenticationTypeMax;
  static const char kMetricVpnUserAuthenticationType[];
  static const int kMetricVpnUserAuthenticationTypeMax;

  // The length in seconds of a lease that has expired while the DHCP
  // client was attempting to renew the lease..
  static const char kMetricExpiredLeaseLengthSecondsSuffix[];
  static const int kMetricExpiredLeaseLengthSecondsMax;
  static const int kMetricExpiredLeaseLengthSecondsMin;
  static const int kMetricExpiredLeaseLengthSecondsNumBuckets;

  // Number of wifi services available when auto-connect is initiated.
  static const char kMetricWifiAutoConnectableServices[];
  static const int kMetricWifiAutoConnectableServicesMax;
  static const int kMetricWifiAutoConnectableServicesMin;
  static const int kMetricWifiAutoConnectableServicesNumBuckets;

  // Number of BSSes available for a wifi service when we attempt to connect
  // to that service.
  static const char kMetricWifiAvailableBSSes[];
  static const int kMetricWifiAvailableBSSesMax;
  static const int kMetricWifiAvailableBSSesMin;
  static const int kMetricWifiAvailableBSSesNumBuckets;

  // Number of services associated with currently connected network.
  static const char kMetricServicesOnSameNetwork[];
  static const int kMetricServicesOnSameNetworkMax;
  static const int kMetricServicesOnSameNetworkMin;
  static const int kMetricServicesOnSameNetworkNumBuckets;

  // Metric for user-initiated events.
  static const char kMetricUserInitiatedEvents[];

  // Wifi TX bitrate in Mbps.
  static const char kMetricWifiTxBitrate[];
  static const int kMetricWifiTxBitrateMax;
  static const int kMetricWifiTxBitrateMin;
  static const int kMetricWifiTxBitrateNumBuckets;

  // User-initiated wifi connection attempt result.
  static const char kMetricWifiUserInitiatedConnectionResult[];

  // The reason of failed user-initiated wifi connection attempt.
  static const char kMetricWifiUserInitiatedConnectionFailureReason[];

  // DNS test result.
  static const char kMetricFallbackDNSTestResultSuffix[];

  // Network problem detected by traffic monitor
  static const char kMetricNetworkProblemDetectedSuffix[];

  // Device's connection status.
  static const char kMetricDeviceConnectionStatus[];

  // DHCP client status.
  static const char kMetricDhcpClientStatus[];

  explicit Metrics(EventDispatcher *dispatcher);
  virtual ~Metrics();

  // Converts the WiFi frequency into the associated UMA channel enumerator.
  static WiFiChannel WiFiFrequencyToChannel(uint16_t frequency);

  // Converts a flimflam security string into its UMA security enumerator.
  static WiFiSecurity WiFiSecurityStringToEnum(const std::string &security);

  // Converts a flimflam AP mode string into its UMA AP mode enumerator.
  static WiFiApMode WiFiApModeStringToEnum(const std::string &ap_mode);

  // Converts a flimflam EAP outer protocol string into its UMA enumerator.
  static EapOuterProtocol EapOuterProtocolStringToEnum(
      const std::string &outer);

  // Converts a flimflam EAP inner protocol string into its UMA enumerator.
  static EapInnerProtocol EapInnerProtocolStringToEnum(
      const std::string &inner);

  // Converts portal detection result to UMA portal result enumerator.
  static PortalResult PortalDetectionResultToEnum(
      const PortalDetector::Result &result);

  // Starts this object.  Call this during initialization.
  virtual void Start();

  // Stops this object.  Call this during cleanup.
  virtual void Stop();

  // Registers a service with this object so it can use the timers to track
  // state transition metrics.
  void RegisterService(const Service &service);

  // Deregisters the service from this class.  All state transition timers
  // will be removed.
  void DeregisterService(const Service &service);

  // Tracks the time it takes |service| to go from |start_state| to
  // |stop_state|.  When |stop_state| is reached, the time is sent to UMA.
  virtual void AddServiceStateTransitionTimer(
      const Service &service, const std::string &histogram_name,
      Service::ConnectState start_state, Service::ConnectState stop_state);

  // Specializes |metric_suffix| for the specified |technology_id|.
  std::string GetFullMetricName(const char *metric_suffix,
                                Technology::Identifier technology_id);

  // Notifies this object that the default service has changed.
  // |service| is the new default service.
  virtual void NotifyDefaultServiceChanged(const Service *service);

  // Notifies this object that |service| state has changed.
  virtual void NotifyServiceStateChanged(const Service &service,
                                         Service::ConnectState new_state);

  // Notifies this object that |service| has been disconnected.
  void NotifyServiceDisconnect(const Service &service);

  // Notifies this object of power at disconnect.
  void NotifySignalAtDisconnect(const Service &service,
                                int16_t signal_strength);

  // Notifies this object of the end of a suspend attempt.
  void NotifySuspendDone();

  // Notifies this object that termination actions started executing.
  void NotifyTerminationActionsStarted(TerminationActionReason reason);

  // Notifies this object that termination actions have been completed.
  // |success| is true, if the termination actions completed successfully.
  void NotifyTerminationActionsCompleted(
      TerminationActionReason reason, bool success);

  // Notifies this object of a failure in LinkMonitor.
  void NotifyLinkMonitorFailure(
      Technology::Identifier technology,
      LinkMonitorFailure failure,
      int seconds_to_failure,
      int broadcast_error_count,
      int unicast_error_count);

  // Notifies this object that LinkMonitor has added a response time sample
  // for |connection| with a value of |response_time_milliseconds|.
  void NotifyLinkMonitorResponseTimeSampleAdded(
      Technology::Identifier technology,
      int response_time_milliseconds);

  // Notifies this object of WiFi disconnect.
  virtual void Notify80211Disconnect(WiFiDisconnectByWhom by_whom,
                                     IEEE_80211::WiFiReasonCode reason);

  // Registers a device with this object so the device can use the timers to
  // track state transition metrics.
  void RegisterDevice(int interface_index,
                      Technology::Identifier technology);

  // Checks to see if the device has already been registered.
  bool IsDeviceRegistered(int interface_index,
                          Technology::Identifier technology);

  // Deregisters the device from this class.  All state transition timers
  // will be removed.
  void DeregisterDevice(int interface_index);

  // Notifies this object that a device has been initialized.
  void NotifyDeviceInitialized(int interface_index);

  // Notifies this object that a device has started the enable process.
  void NotifyDeviceEnableStarted(int interface_index);

  // Notifies this object that a device has completed the enable process.
  void NotifyDeviceEnableFinished(int interface_index);

  // Notifies this object that a device has started the disable process.
  void NotifyDeviceDisableStarted(int interface_index);

  // Notifies this object that a device has completed the disable process.
  void NotifyDeviceDisableFinished(int interface_index);

  // Notifies this object that a device has started the scanning process.
  virtual void NotifyDeviceScanStarted(int interface_index);

  // Notifies this object that a device has completed the scanning process.
  virtual void NotifyDeviceScanFinished(int interface_index);

  // Terminates an underway scan (does nothing if a scan wasn't underway).
  virtual void ResetScanTimer(int interface_index);

  // Notifies this object that a device has started the connect process.
  virtual void NotifyDeviceConnectStarted(int interface_index,
                                          bool is_auto_connecting);

  // Notifies this object that a device has completed the connect process.
  virtual void NotifyDeviceConnectFinished(int interface_index);

  // Resets both the connect_timer and the scan_connect_timer the timer (the
  // latter so that a future connect will not erroneously be associated with
  // the previous scan).
  virtual void ResetConnectTimer(int interface_index);

  // Notifies this object that a cellular device has been dropped by the
  // network.
  void NotifyCellularDeviceDrop(const std::string &network_technology,
                                uint16_t signal_strength);

  // Notifies this object about 3GPP registration drop events.
  virtual void Notify3GPPRegistrationDelayedDropPosted();
  virtual void Notify3GPPRegistrationDelayedDropCanceled();

  // Notifies this object about a cellular device failure code.
  void NotifyCellularDeviceFailure(const Error &error);

  // Notifies this object that a cellular service has been marked as
  // out-of-credits.
  void NotifyCellularOutOfCredits(Metrics::CellularOutOfCreditsReason reason);

  // Notifies this object about number of wifi services available for auto
  // connect when auto-connect is initiated.
  virtual void NotifyWifiAutoConnectableServices(int num_services);

  // Notifies this object about number of BSSes available for a wifi service
  // when attempt to connect to that service.
  virtual void NotifyWifiAvailableBSSes(int num_services);

  // Notifies this object about number of services associated to the
  // currently connected network.
  virtual void NotifyServicesOnSameNetwork(int num_services);

  // Notifies this object about WIFI TX bitrate in Mbps.
  virtual void NotifyWifiTxBitrate(int bitrate);

  // Notifies this object about the result of user-initiated connection
  // attempt.
  virtual void NotifyUserInitiatedConnectionResult(const std::string &name,
                                                   int result);

  // Notifies this object about the reason of failed user-initiated connection
  // attempt.
  virtual void NotifyUserInitiatedConnectionFailureReason(
      const std::string &name, const Service::ConnectFailure failure);

  // Notifies this object about a corrupted profile.
  virtual void NotifyCorruptedProfile();

  // Notifies this object about user-initiated event.
  virtual void NotifyUserInitiatedEvent(int event);

  // Notifies this object about the result of the fallback DNS test.
  virtual void NotifyFallbackDNSTestResult(Technology::Identifier technology_id,
                                           int result);

  // Notifies this object about a network problem detected on the currently
  // connected network.
  virtual void NotifyNetworkProblemDetected(
      Technology::Identifier technology_id, int reason);

  // Notifies this object about current connection status (online vs offline).
  virtual void NotifyDeviceConnectionStatus(Metrics::ConnectionStatus status);

  // Notifies this object about the DHCP client status.
  virtual void NotifyDhcpClientStatus(Metrics::DhcpClientStatus status);

  // Sends linear histogram data to UMA.
  virtual bool SendEnumToUMA(const std::string &name, int sample, int max);

  // Send histogram data to UMA.
  virtual bool SendToUMA(const std::string &name, int sample, int min,
                         int max, int num_buckets);

 private:
  friend class MetricsTest;
  FRIEND_TEST(MetricsTest, CellularDropsPerHour);
  FRIEND_TEST(MetricsTest, FrequencyToChannel);
  FRIEND_TEST(MetricsTest, ResetConnectTimer);
  FRIEND_TEST(MetricsTest, ServiceFailure);
  FRIEND_TEST(MetricsTest, TimeOnlineTimeToDrop);
  FRIEND_TEST(MetricsTest, TimeToConfig);
  FRIEND_TEST(MetricsTest, TimeToOnline);
  FRIEND_TEST(MetricsTest, TimeToPortal);
  FRIEND_TEST(MetricsTest, TimeToScanIgnore);
  FRIEND_TEST(MetricsTest, WiFiServiceChannel);
  FRIEND_TEST(MetricsTest, WiFiServicePostReady);
  FRIEND_TEST(WiFiMainTest, GetGeolocationObjects);

  typedef ScopedVector<chromeos_metrics::TimerReporter> TimerReporters;
  typedef std::list<chromeos_metrics::TimerReporter *> TimerReportersList;
  typedef std::map<Service::ConnectState, TimerReportersList>
      TimerReportersByState;
  struct ServiceMetrics {
    // All TimerReporter objects are stored in |timers| which owns the objects.
    // |start_on_state| and |stop_on_state| contain pointers to the
    // TimerReporter objects and control when to start and stop the timers.
    TimerReporters timers;
    TimerReportersByState start_on_state;
    TimerReportersByState stop_on_state;
  };
  typedef std::map<const Service *, std::shared_ptr<ServiceMetrics> >
      ServiceMetricsLookupMap;

  struct DeviceMetrics {
    DeviceMetrics() : auto_connect_tries(0) {}
    Technology::Identifier technology;
    scoped_ptr<chromeos_metrics::TimerReporter> initialization_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> enable_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> disable_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> scan_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> connect_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> scan_connect_timer;
    scoped_ptr<chromeos_metrics::TimerReporter> auto_connect_timer;
    int auto_connect_tries;
  };
  typedef std::map<const int, std::shared_ptr<DeviceMetrics> >
      DeviceMetricsLookupMap;

  static const uint16_t kWiFiBandwidth5MHz;
  static const uint16_t kWiFiBandwidth20MHz;
  static const uint16_t kWiFiFrequency2412;
  static const uint16_t kWiFiFrequency2472;
  static const uint16_t kWiFiFrequency2484;
  static const uint16_t kWiFiFrequency5170;
  static const uint16_t kWiFiFrequency5180;
  static const uint16_t kWiFiFrequency5230;
  static const uint16_t kWiFiFrequency5240;
  static const uint16_t kWiFiFrequency5320;
  static const uint16_t kWiFiFrequency5500;
  static const uint16_t kWiFiFrequency5700;
  static const uint16_t kWiFiFrequency5745;
  static const uint16_t kWiFiFrequency5825;

  void InitializeCommonServiceMetrics(const Service &service);
  void UpdateServiceStateTransitionMetrics(ServiceMetrics *service_metrics,
                                           Service::ConnectState new_state);
  void SendServiceFailure(const Service &service);

  DeviceMetrics *GetDeviceMetrics(int interface_index) const;
  void AutoConnectMetricsReset(DeviceMetrics *device_metrics);

  // For unit test purposes.
  void set_library(MetricsLibraryInterface *library);
  void set_time_online_timer(chromeos_metrics::Timer *timer) {
    time_online_timer_.reset(timer);  // Passes ownership
  }
  void set_time_to_drop_timer(chromeos_metrics::Timer *timer) {
    time_to_drop_timer_.reset(timer);  // Passes ownership
  }
  void set_time_resume_to_ready_timer(chromeos_metrics::Timer *timer) {
    time_resume_to_ready_timer_.reset(timer);  // Passes ownership
  }
  void set_time_termination_actions_timer(
    chromeos_metrics::Timer *timer) {
    time_termination_actions_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_scan_timer(int interface_index,
                              chromeos_metrics::TimerReporter *timer) {
    DeviceMetrics *device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_connect_timer(int interface_index,
                                 chromeos_metrics::TimerReporter *timer) {
    DeviceMetrics *device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->connect_timer.reset(timer);  // Passes ownership
  }
  void set_time_to_scan_connect_timer(int interface_index,
                                      chromeos_metrics::TimerReporter *timer) {
    DeviceMetrics *device_metrics = GetDeviceMetrics(interface_index);
    device_metrics->scan_connect_timer.reset(timer);  // Passes ownership
  }

  // |library_| points to |metrics_library_| when shill runs normally.
  // However, in order to allow for unit testing, we point |library_| to a
  // MetricsLibraryMock object instead.
  EventDispatcher *dispatcher_;
  MetricsLibrary metrics_library_;
  MetricsLibraryInterface *library_;
  ServiceMetricsLookupMap services_metrics_;
  Technology::Identifier last_default_technology_;
  bool was_online_;
  scoped_ptr<chromeos_metrics::Timer> time_online_timer_;
  scoped_ptr<chromeos_metrics::Timer> time_to_drop_timer_;
  scoped_ptr<chromeos_metrics::Timer> time_resume_to_ready_timer_;
  scoped_ptr<chromeos_metrics::Timer> time_termination_actions_timer;
  bool collect_bootstats_;
  DeviceMetricsLookupMap devices_metrics_;

  DISALLOW_COPY_AND_ASSIGN(Metrics);
};

}  // namespace shill

#endif  // SHILL_METRICS_H_
