// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PORTAL_DETECTOR_H_
#define SHILL_PORTAL_DETECTOR_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <brillo/http/http_request.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/http_request.h"
#include "shill/http_url.h"
#include "shill/net/shill_time.h"
#include "shill/net/sockets.h"
#include "shill/refptr_types.h"

namespace shill {

class EventDispatcher;
class Time;

// The PortalDetector class implements the portal detection
// facility in shill, which is responsible for checking to see
// if a connection has "general internet connectivity".
//
// This information can be used for ranking one connection
// against another, or for informing UI and other components
// outside the connection manager whether the connection seems
// available for "general use" or if further user action may be
// necessary (e.g, click through of a WiFi Hotspot's splash
// page).
//
// This is achieved by using one or more trial attempts
// to access a URL and expecting a specific response.  Any result
// that deviates from this result (DNS or HTTP errors, as well as
// deviations from the expected content) are considered failures.
class PortalDetector {
 public:
  // The Phase enum indicates the phase at which the probe fails.
  enum class Phase {
    kUnknown,
    kConnection,  // Failure to establish connection with server
    kDNS,         // Failure to resolve hostname or DNS server failure
    kHTTP,        // Failure to read or write to server
    kContent      // Content mismatch in response
  };

  enum class Status { kFailure, kSuccess, kTimeout };

  struct Properties {
    Properties()
        : http_url_string(kDefaultHttpsUrl),
          https_url_string(kDefaultHttpsUrl) {}
    Properties(const std::string& http_url_string,
               const std::string& https_url_string)
        : http_url_string(http_url_string),
          https_url_string(https_url_string) {}
    bool operator==(const Properties& b) const {
      return http_url_string == b.http_url_string &&
             https_url_string == b.https_url_string;
    }

    std::string http_url_string;
    std::string https_url_string;
  };

  struct Result {
    Result()
        : phase(Phase::kUnknown),
          status(Status::kFailure),
          num_attempts(0),
          final(false) {}
    Result(Phase phase, Status status)
        : phase(phase), status(status), num_attempts(0), final(false) {}
    Result(Phase phase, Status status, int num_attempts, int final)
        : phase(phase),
          status(status),
          num_attempts(num_attempts),
          final(final) {}

    Phase phase;
    Status status;

    // Total number of connectivity trials attempted.
    // This includes failure, timeout and successful attempts.
    // This only valid when |final| is true.
    int num_attempts;
    bool final;
  };

  static const char kDefaultHttpUrl[];
  static const char kDefaultHttpsUrl[];
  static const int kDefaultCheckIntervalSeconds;
  static const char kDefaultCheckPortalList[];
  // Maximum number of times the PortalDetector will attempt a connection.
  static const int kMaxRequestAttempts;

  PortalDetector(ConnectionRefPtr connection,
                 EventDispatcher* dispatcher,
                 const base::Callback<void(const PortalDetector::Result&)>
                     &callback);
  virtual ~PortalDetector();

  // Static method used to map a portal detection phase to a string.  This
  // includes the phases for connection, DNS, HTTP, returned content and
  // unknown.
  static const std::string PhaseToString(Phase phase);

  // Static method to map from the result of a portal detection phase to a
  // status string. This method supports success, timeout and failure.
  static const std::string StatusToString(Status status);

  // Static method mapping from HttpRequest responses to ConntectivityTrial
  // phases for portal detection. For example, if the HttpRequest result is
  // HttpRequest::kResultDNSFailure, this method returns a
  // PortalDetector::Result with the phase set to
  // Phase::kDNS and the status set to
  // Status::kFailure.
  static Result GetPortalResultForRequestResult(HttpRequest::Result result);

  // Start a portal detection test.  Returns true if |url_string| correctly
  // parses as a URL.  Returns false (and does not start) if the |url_string|
  // fails to parse.
  //
  // As each attempt completes the callback handed to the constructor will
  // be called.  The PortalDetector will try up to kMaxRequestAttempts times
  // to successfully retrieve the URL.  If the attempt is successful or
  // this is the last attempt, the "final" flag in the Result structure will
  // be true, otherwise it will be false, and the PortalDetector will
  // schedule the next attempt.
  virtual bool StartAfterDelay(const Properties& props, int delay_seconds);

  // Do exactly one trial instead of up to kMaxRequestAttempts.
  virtual bool StartSingleTrial(const Properties& props);

  // End the current portal detection process if one exists, and do not call
  // the callback.
  virtual void Stop();

  // Returns whether portal request is "in progress": whether the
  // trial is in the progress of making attempts.  Returns true if
  // attempts are in progress, false otherwise.  Notably, this function
  // returns false during the period of time between calling "Start" or
  // "StartAfterDelay" and the actual start of the first attempt. In the case
  // where multiple attempts may be tried, IsInProgress will return true after
  // the first attempt has actively started testing the connection.
  virtual bool IsInProgress();

 private:
  friend class PortalDetectorTest;
  FRIEND_TEST(PortalDetectorTest, StartAttemptFailed);
  FRIEND_TEST(PortalDetectorTest, AdjustStartDelayImmediate);
  FRIEND_TEST(PortalDetectorTest, AdjustStartDelayAfterDelay);
  FRIEND_TEST(PortalDetectorTest, AttemptCount);
  FRIEND_TEST(PortalDetectorTest, ReadBadHeadersRetry);
  FRIEND_TEST(PortalDetectorTest, ReadBadHeader);
  FRIEND_TEST(PortalDetectorTest, RequestTimeout);
  FRIEND_TEST(PortalDetectorTest, ReadPartialHeaderTimeout);
  FRIEND_TEST(PortalDetectorTest, ReadCompleteHeader);
  FRIEND_TEST(PortalDetectorTest, RequestSuccess);
  FRIEND_TEST(PortalDetectorTest, RequestFail);
  FRIEND_TEST(PortalDetectorTest, StartSingleTrial);
  FRIEND_TEST(PortalDetectorTest, TrialRetry);
  FRIEND_TEST(PortalDetectorTest, TrialRetryFail);
  FRIEND_TEST(PortalDetectorTest, InvalidURL);
  FRIEND_TEST(PortalDetectorTest, IsActive);

  // Minimum time between attempts to connect to server.
  static const int kMinTimeBetweenAttemptsSeconds;
  // Time to wait for request to complete.
  static const int kRequestTimeoutSeconds;
  // Maximum number of failures in content phase before we stop attempting
  // connections.
  static const int kMaxFailuresInContentPhase;

  // Internal method to update the start time of the next event.  This is used
  // to keep attempts spaced by at least kMinTimeBetweenAttemptsSeconds in the
  // event of a retry.
  void UpdateAttemptTime(int delay_seconds);

  // Internal method used to adjust the start delay in the event of a retry.
  // Calculates the elapsed time between the most recent attempt and the point
  // the retry is scheduled.  Adds an additional delay to meet the
  // kMinTimeBetweenAttemptsSeconds requirement.
  int AdjustStartDelay(int init_delay_seconds);

  // Called after each trial to return |result| after attempting to determine
  // connectivitiy status.
  void CompleteAttempt(Result result);

  // Start a trial with the supplied delay in ms.
  void StartTrialAfterDelay(int start_delay_milliseconds);

  // Start a trial with the supplied URL and starting delay (ms).
  // Returns true if |url_string| correctly parses as a URL.  Returns false (and
  // does not start) if the |url_string| fails to parse.
  //
  // After a trial completes, the callback supplied in the constructor is
  // called.
  bool StartTrial(const Properties& props, int start_delay_milliseconds);

  // Internal method used to start the actual connectivity trial, called after
  // the start delay completes.
  void StartTrialTask();

  // Callback used to return data read from the HttpRequest.
  void RequestSuccessCallback(std::shared_ptr<brillo::http::Response> response);

  // Callback used to return the error from the HttpRequest.
  void RequestErrorCallback(HttpRequest::Result result);

  // Internal method used to clean up state and call CompleteAttempt.
  void CompleteTrial(Result result);

  // Internal method used to cancel the timeout timer and stop an active
  // HttpRequest.  If |reset_request| is true, this method resets the underlying
  // HttpRequest object.
  void CleanupTrial();

  // Callback used to cancel the underlying HttpRequest in the event of a
  // timeout.
  void TimeoutTrialTask();

  // After a trial completes, the calling class may call Retry on the trial.
  // This allows the underlying HttpRequest object to be reused.  The URL is not
  // reparsed and the original URL supplied in the Start command is used.  The
  // |start_delay| is the time (ms) to wait before starting the trial.  Retry
  // returns true if the underlying HttpRequest is still available.  If the
  // HttpRequest was reset or never created, Retry will return false.
  virtual bool Retry(int start_delay_milliseconds);

  // Method to return if the connection is being actively tested.
  virtual bool IsActive();

  int attempt_count_;
  struct timeval attempt_start_time_;
  bool single_trial_;
  ConnectionRefPtr connection_;
  EventDispatcher* dispatcher_;
  base::WeakPtrFactory<PortalDetector> weak_ptr_factory_;
  base::Callback<void(const Result&)> portal_result_callback_;
  Time* time_;
  int failures_in_content_phase_;
  int trial_timeout_seconds_;
  base::Callback<void(std::shared_ptr<brillo::http::Response>)>
      request_success_callback_;
  base::Callback<void(HttpRequest::Result)> request_error_callback_;
  std::unique_ptr<HttpRequest> http_request_;

  std::string http_url_string_;
  base::CancelableClosure trial_;
  base::CancelableClosure trial_timeout_;
  bool is_active_;

  DISALLOW_COPY_AND_ASSIGN(PortalDetector);
};

}  // namespace shill

#endif  // SHILL_PORTAL_DETECTOR_H_
