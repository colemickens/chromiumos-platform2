// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/portal_detector.h"

#include <string>

#include <base/bind.h>
#include <base/rand_util.h>
#include <base/strings/pattern.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/dns_client.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/net/ip_address.h"

using base::Bind;
using base::Callback;
using base::StringPrintf;
using std::string;

namespace {

// This keyword gets replaced with a number from the below range.
const char kRandomKeyword[] = "${RAND}";

// This range is determined by the server-side configuration.  See b/63033351
const int kMinRandomHost = 1;
const int kMaxRandomHost = 25;

// If |in| contains the substring |kRandomKeyword|, replace it with a
// random number between |kMinRandomHost| and |kMaxRandomHost| and return
// the newly-mangled string.  Otherwise return an exact copy of |in|.  This
// is used to rotate through alternate hostnames (e.g. alt1..alt25) on
// each portal check, to defeat IP-based blocking.
string RandomizeURL(string url) {
  int alt_host = base::RandInt(kMinRandomHost, kMaxRandomHost);
  base::ReplaceFirstSubstringAfterOffset(&url, 0, kRandomKeyword,
                                         base::IntToString(alt_host));
  return url;
}

}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kPortal;
static string ObjectID(Connection* c) { return c->interface_name(); }
}

const int PortalDetector::kDefaultCheckIntervalSeconds = 30;
const char PortalDetector::kDefaultCheckPortalList[] = "ethernet,wifi,cellular";

const int PortalDetector::kMaxRequestAttempts = 3;
const int PortalDetector::kMinTimeBetweenAttemptsSeconds = 3;
const int PortalDetector::kRequestTimeoutSeconds = 10;
const int PortalDetector::kMaxFailuresInContentPhase = 2;

const char PortalDetector::kDefaultHttpUrl[] =
    "http://www.gstatic.com/generate_204";
const char PortalDetector::kDefaultHttpsUrl[] =
    "https://www.google.com/generate_204";

PortalDetector::PortalDetector(
    ConnectionRefPtr connection,
    EventDispatcher* dispatcher,
    const Callback<void(const PortalDetector::Result&)>& callback)
    : attempt_count_(0),
      attempt_start_time_((struct timeval){0}),
      single_trial_(false),
      connection_(connection),
      dispatcher_(dispatcher),
      weak_ptr_factory_(this),
      portal_result_callback_(callback),
      time_(Time::GetInstance()),
      failures_in_content_phase_(0),
      request_success_callback_(Bind(&PortalDetector::RequestSuccessCallback,
                                     weak_ptr_factory_.GetWeakPtr())),
      request_error_callback_(Bind(&PortalDetector::RequestErrorCallback,
                                   weak_ptr_factory_.GetWeakPtr())),
      is_active_(false) {}

PortalDetector::~PortalDetector() {
  Stop();
}

bool PortalDetector::StartSingleTrial(const PortalDetector::Properties& props) {
  single_trial_ = true;
  return StartAfterDelay(props, 0);
}

bool PortalDetector::StartAfterDelay(const PortalDetector::Properties& props,
                                     int delay_seconds) {
  SLOG(connection_.get(), 3) << "In " << __func__;

  if (!StartTrial(props, delay_seconds * 1000)) {
    return false;
  }
  attempt_count_ = 1;
  // The attempt_start_time_ is calculated based on the current time and
  // |delay_seconds|.  This is used to determine if a portal detection attempt
  // is in progress.
  UpdateAttemptTime(delay_seconds);
  // If we're starting a new set of attempts, discard past failure history.
  failures_in_content_phase_ = 0;
  return true;
}

bool PortalDetector::StartTrial(const Properties& props,
                                int start_delay_milliseconds) {
  SLOG(connection_.get(), 3) << "In " << __func__;

  // This step is rerun on each attempt, but trying it here will allow
  // Start() to abort on any obviously malformed URL strings.
  HttpUrl http_url, https_url;
  if (!http_url.ParseFromString(RandomizeURL(props.http_url_string))) {
    LOG(ERROR) << "Failed to parse URL string: " << props.http_url_string;
    return false;
  }
  if (!https_url.ParseFromString(props.https_url_string)) {
    LOG(ERROR) << "Failed to parse URL string: " << props.https_url_string;
    return false;
  }
  http_url_string_ = props.http_url_string;

  if (http_request_) {
    CleanupTrial();
  } else {
    http_request_.reset(new HttpRequest(connection_, dispatcher_));
  }
  StartTrialAfterDelay(start_delay_milliseconds);
  return true;
}

void PortalDetector::StartTrialAfterDelay(int start_delay_milliseconds) {
  SLOG(connection_.get(), 4)
      << "In " << __func__ << " delay = " << start_delay_milliseconds << "ms.";
  trial_.Reset(
      Bind(&PortalDetector::StartTrialTask, weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, trial_.callback(),
                               start_delay_milliseconds);
}

void PortalDetector::StartTrialTask() {
  HttpUrl url;
  if (!url.ParseFromString(RandomizeURL(http_url_string_))) {
    LOG(ERROR) << "Failed to parse URL string: " << http_url_string_;
    CompleteTrial(Result(Phase::kUnknown, Status::kFailure));
    return;
  }

  HttpRequest::Result result = http_request_->Start(
      url, request_success_callback_, request_error_callback_);
  if (result != HttpRequest::kResultInProgress) {
    CompleteTrial(PortalDetector::GetPortalResultForRequestResult(result));
    return;
  }
  is_active_ = true;

  trial_timeout_.Reset(
      Bind(&PortalDetector::TimeoutTrialTask, weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(FROM_HERE, trial_timeout_.callback(),
                               kRequestTimeoutSeconds * 1000);
}

bool PortalDetector::IsActive() {
  return is_active_;
}

void PortalDetector::CompleteTrial(Result result) {
  SLOG(connection_.get(), 3)
      << StringPrintf("Trial completed with phase==%s, status==%s",
                      PhaseToString(result.phase).c_str(),
                      StatusToString(result.status).c_str());
  CleanupTrial();
  CompleteAttempt(result);
}

void PortalDetector::CleanupTrial() {
  trial_timeout_.Cancel();

  if (http_request_)
    http_request_->Stop();

  is_active_ = false;
}

void PortalDetector::TimeoutTrialTask() {
  LOG(ERROR) << "Trial request timed out";
  CompleteTrial(Result(Phase::kUnknown, Status::kTimeout));
}

bool PortalDetector::Retry(int start_delay_milliseconds) {
  SLOG(connection_.get(), 3) << "In " << __func__;
  if (!http_request_)
    return false;

  CleanupTrial();
  StartTrialAfterDelay(start_delay_milliseconds);
  return true;
}

void PortalDetector::Stop() {
  SLOG(connection_.get(), 3) << "In " << __func__;

  attempt_count_ = 0;
  failures_in_content_phase_ = 0;
  single_trial_ = false;
  if (!http_request_)
    return;

  CleanupTrial();
  http_request_.reset();
}

void PortalDetector::RequestSuccessCallback(
    std::shared_ptr<brillo::http::Response> response) {
  // TODO(matthewmwang): check for 0 length data as well
  if (response->GetStatusCode() == brillo::http::status_code::NoContent) {
    CompleteTrial(Result(Phase::kContent, Status::kSuccess));
  } else {
    CompleteTrial(Result(Phase::kContent, Status::kFailure));
  }
}

void PortalDetector::RequestErrorCallback(HttpRequest::Result result) {
  CompleteTrial(GetPortalResultForRequestResult(result));
}

// IsInProgress returns true if a trial is actively testing the
// connection.  If Start has been called, but the trial was delayed,
// IsInProgress will return false.  PortalDetector implements this by
// calculating the start time of the next trial. After an initial
// trial and in the case where multiple attempts may be tried, IsInProgress will
// return true.
bool PortalDetector::IsInProgress() {
  if (attempt_count_ > 1)
    return true;
  if (attempt_count_ == 1)
    return is_active_;
  return false;
}

void PortalDetector::CompleteAttempt(PortalDetector::Result trial_result) {
  if (trial_result.status == Status::kFailure &&
      trial_result.phase == Phase::kContent) {
    failures_in_content_phase_++;
  }

  LOG(INFO) << StringPrintf(
      "Portal detection completed attempt %d with "
      "phase==%s, status==%s, failures in content==%d",
      attempt_count_, PortalDetector::PhaseToString(trial_result.phase).c_str(),
      PortalDetector::StatusToString(trial_result.status).c_str(),
      failures_in_content_phase_);

  if (single_trial_ || trial_result.status == Status::kSuccess ||
      attempt_count_ >= kMaxRequestAttempts ||
      failures_in_content_phase_ >= kMaxFailuresInContentPhase) {
    trial_result.num_attempts = attempt_count_;
    trial_result.final = true;
    Stop();
  } else {
    attempt_count_++;
    int retry_delay_seconds = AdjustStartDelay(0);
    Retry(retry_delay_seconds * 1000);
    UpdateAttemptTime(retry_delay_seconds);
  }
  portal_result_callback_.Run(trial_result);
}

void PortalDetector::UpdateAttemptTime(int delay_seconds) {
  time_->GetTimeMonotonic(&attempt_start_time_);
  struct timeval delay_timeval = { delay_seconds, 0 };
  timeradd(&attempt_start_time_, &delay_timeval, &attempt_start_time_);
}


int PortalDetector::AdjustStartDelay(int init_delay_seconds) {
  int next_attempt_delay_seconds = 0;
  if (attempt_count_ > 0) {
    // Ensure that attempts are spaced at least by a minimal interval.
    struct timeval now, elapsed_time;
    time_->GetTimeMonotonic(&now);
    timersub(&now, &attempt_start_time_, &elapsed_time);
    SLOG(connection_.get(), 4) << "Elapsed time from previous attempt is "
                               << elapsed_time.tv_sec << " seconds.";
    if (elapsed_time.tv_sec < kMinTimeBetweenAttemptsSeconds) {
      next_attempt_delay_seconds = kMinTimeBetweenAttemptsSeconds -
                                   elapsed_time.tv_sec;
    }
  } else {
    LOG(FATAL) << "AdjustStartDelay in PortalDetector called without "
                  "previous attempts";
  }
  SLOG(connection_.get(), 3) << "Adjusting trial start delay from "
                             << init_delay_seconds << " seconds to "
                             << next_attempt_delay_seconds << " seconds.";
  return next_attempt_delay_seconds;
}

// static
const string PortalDetector::PhaseToString(Phase phase) {
  switch (phase) {
    case Phase::kConnection:
      return kPortalDetectionPhaseConnection;
    case Phase::kDNS:
      return kPortalDetectionPhaseDns;
    case Phase::kHTTP:
      return kPortalDetectionPhaseHttp;
    case Phase::kContent:
      return kPortalDetectionPhaseContent;
    case Phase::kUnknown:
    default:
      return kPortalDetectionPhaseUnknown;
  }
}

// static
const string PortalDetector::StatusToString(Status status) {
  switch (status) {
    case Status::kSuccess:
      return kPortalDetectionStatusSuccess;
    case Status::kTimeout:
      return kPortalDetectionStatusTimeout;
    case Status::kFailure:
    default:
      return kPortalDetectionStatusFailure;
  }
}

PortalDetector::Result PortalDetector::GetPortalResultForRequestResult(
    HttpRequest::Result result) {
  switch (result) {
    case HttpRequest::kResultSuccess:
      // The request completed without receiving the expected payload.
      return Result(Phase::kContent, Status::kFailure);
    case HttpRequest::kResultDNSFailure:
      return Result(Phase::kDNS, Status::kFailure);
    case HttpRequest::kResultDNSTimeout:
      return Result(Phase::kDNS, Status::kTimeout);
    case HttpRequest::kResultConnectionFailure:
      return Result(Phase::kConnection, Status::kFailure);
    case HttpRequest::kResultHTTPFailure:
      return Result(Phase::kHTTP, Status::kFailure);
    case HttpRequest::kResultHTTPTimeout:
      return Result(Phase::kHTTP, Status::kTimeout);
    case HttpRequest::kResultUnknown:
    default:
      return Result(Phase::kUnknown, Status::kFailure);
  }
}

}  // namespace shill
