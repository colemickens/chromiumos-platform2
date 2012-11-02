// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/config80211.h"

#include <ctype.h>
#include <netlink/msg.h>

#include <map>
#include <sstream>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/stl_util.h>

#include "shill/io_handler.h"
#include "shill/logging.h"
#include "shill/nl80211_socket.h"
#include "shill/scope_logger.h"
#include "shill/user_bound_nlmessage.h"

using base::Bind;
using base::LazyInstance;
using std::list;
using std::string;

namespace shill {

namespace {
LazyInstance<Config80211> g_config80211 = LAZY_INSTANCE_INITIALIZER;
}  // namespace

Config80211::EventTypeStrings *Config80211::event_types_ = NULL;

Config80211::Config80211()
    : wifi_state_(kWifiDown),
      dispatcher_(NULL),
      weak_ptr_factory_(this),
      dispatcher_callback_(Bind(&Config80211::HandleIncomingEvents,
                              weak_ptr_factory_.GetWeakPtr())),
      sock_(NULL) {
}

Config80211::~Config80211() {
  // Since Config80211 is a singleton, it should be safe to delete a static
  // member.
  delete event_types_;
  event_types_ = NULL;
}

Config80211 *Config80211::GetInstance() {
  return g_config80211.Pointer();
}

void Config80211::Reset() {
  wifi_state_ = kWifiDown;
  subscribed_events_.clear();
  ClearBroadcastCallbacks();
}

bool Config80211::Init(EventDispatcher *dispatcher) {
  if (!sock_) {
    sock_ = new Nl80211Socket;
    if (!sock_) {
      LOG(ERROR) << "No memory";
      return false;
    }

    if (!sock_->Init()) {
      return false;
    }
  }

  if (!event_types_) {
    event_types_ = new EventTypeStrings;
    (*event_types_)[Config80211::kEventTypeConfig] = "config";
    (*event_types_)[Config80211::kEventTypeScan] = "scan";
    (*event_types_)[Config80211::kEventTypeRegulatory] = "regulatory";
    (*event_types_)[Config80211::kEventTypeMlme] = "mlme";
  }

  // Install ourselves in the shill mainloop so we receive messages on the
  // nl80211 socket.
  dispatcher_ = dispatcher;
  if (dispatcher_) {
    dispatcher_handler_.reset(
      dispatcher_->CreateReadyHandler(GetFd(),
                                      IOHandler::kModeInput,
                                      dispatcher_callback_));
  }
  return true;
}

bool Config80211::AddBroadcastCallback(const Callback &callback) {
  list<Callback>::iterator i;
  if (FindBroadcastCallback(callback)) {
    LOG(WARNING) << "Trying to re-add a callback";
    return false;  // Should only be one copy in the list.
  }
  if (callback.is_null()) {
    LOG(WARNING) << "Trying to add a NULL callback";
    return false;
  }
  // And add the callback to the list.
  SLOG(WiFi, 3) << "Config80211::" << __func__ << " - adding callback";
  broadcast_callbacks_.push_back(callback);
  return true;
}

bool Config80211::RemoveBroadcastCallback(const Callback &callback) {
  list<Callback>::iterator i;
  for (i = broadcast_callbacks_.begin(); i != broadcast_callbacks_.end(); ++i) {
    if ((*i).Equals(callback)) {
      broadcast_callbacks_.erase(i);
      // Should only be one copy in the list so we don't have to continue
      // looking for another one.
      return true;
    }
  }
  LOG(WARNING) << "Callback not found.";
  return false;
}

bool Config80211::FindBroadcastCallback(const Callback &callback) const {
  list<Callback>::const_iterator i;
  for (i = broadcast_callbacks_.begin(); i != broadcast_callbacks_.end(); ++i) {
    if ((*i).Equals(callback)) {
      return true;
    }
  }
  return false;
}

void Config80211::ClearBroadcastCallbacks() {
  broadcast_callbacks_.clear();
}

bool Config80211::SetMessageCallback(const KernelBoundNlMessage &message,
                                     const Callback &callback) {
  LOG(INFO) << "Setting callback for message " << message.GetId();
  uint32_t message_id = message.GetId();
  if (message_id == 0) {
    LOG(ERROR) << "Message ID 0 is reserved for broadcast callbacks.";
    return false;
  }

  if (ContainsKey(message_callbacks_, message_id)) {
    LOG(ERROR) << "Already a callback assigned for id " << message_id;
    return false;
  }

  if (callback.is_null()) {
    LOG(ERROR) << "Trying to add a NULL callback for id " << message_id;
    return false;
  }

  message_callbacks_[message_id] = callback;
  return true;
}

bool Config80211::UnsetMessageCallbackById(uint32_t message_id) {
  if (!ContainsKey(message_callbacks_, message_id)) {
    LOG(WARNING) << "No callback assigned for id " << message_id;
    return false;
  }
  message_callbacks_.erase(message_id);
  return true;
}

// static
bool Config80211::GetEventTypeString(EventType type, string *value) {
  if (!value) {
    LOG(ERROR) << "NULL |value|";
    return false;
  }
  if (!event_types_) {
    LOG(ERROR) << "NULL |event_types_|";
    return false;
  }

  EventTypeStrings::iterator match = (*event_types_).find(type);
  if (match == (*event_types_).end()) {
    LOG(ERROR) << "Event type " << type << " not found";
    return false;
  }
  *value = match->second;
  return true;
}

void Config80211::SetWifiState(WifiState new_state) {
  if (wifi_state_ == new_state) {
    return;
  }

  // If we're newly-up, subscribe to all the event types that have been
  // requested.
  if (new_state == kWifiUp) {
    SubscribedEvents::const_iterator i;
    for (i = subscribed_events_.begin(); i != subscribed_events_.end(); ++i) {
      ActuallySubscribeToEvents(*i);
    }
  }
  wifi_state_ = new_state;
}

bool Config80211::SubscribeToEvents(EventType type) {
  bool it_worked = true;
  if (!ContainsKey(subscribed_events_, type)) {
    if (wifi_state_ == kWifiUp) {
      it_worked = ActuallySubscribeToEvents(type);
    }
    // |subscribed_events_| is a list of events to which we want to subscribe
    // when wifi comes up (including when it comes _back_ up after it goes
    // down sometime in the future).
    subscribed_events_.insert(type);
  }
  return it_worked;
}

bool Config80211::ActuallySubscribeToEvents(EventType type) {
  string group_name;

  if (!GetEventTypeString(type, &group_name)) {
    return false;
  }
  if (!sock_->AddGroupMembership(group_name)) {
    return false;
  }
  // No sequence checking for multicast messages.
  if (!sock_->DisableSequenceChecking()) {
    return false;
  }

  // Install the global NetLink Callback for messages along with a parameter.
  // The Netlink Callback's parameter is passed to 'C' as a 'void *'.
  if (!sock_->SetNetlinkCallback(OnRawNlMessageReceived,
                                 static_cast<void *>(this))) {
    return false;
  }
  return true;
}

void Config80211::HandleIncomingEvents(int unused_fd) {
  sock_->GetMessages();
}

// NOTE: the "struct nl_msg" declaration, below, is a complete fabrication
// (but one consistent with the nl_socket_modify_cb call to which
// |OnRawNlMessageReceived| is a parameter).  |raw_message| is actually a
// "struct sk_buff" but that data type is only visible in the kernel.  We'll
// scrub this erroneous datatype with the "nlmsg_hdr" call, below, which
// extracts an nlmsghdr pointer from |raw_message|.  We'll, then, pass this to
// a separate method, |OnNlMessageReceived|, to make testing easier.

// static
int Config80211::OnRawNlMessageReceived(struct nl_msg *raw_message,
                                        void *void_config80211) {
  if (!void_config80211) {
    LOG(WARNING) << "NULL config80211 parameter";
    return NL_SKIP;  // Skip current message, continue parsing buffer.
  }

  Config80211 *config80211 = static_cast<Config80211 *>(void_config80211);
  SLOG(WiFi, 3) << "  " << __func__ << " calling OnNlMessageReceived";
  return config80211->OnNlMessageReceived(nlmsg_hdr(raw_message));
}

int Config80211::OnNlMessageReceived(nlmsghdr *msg) {
  SLOG(WiFi, 3) << "\t  Entering " << __func__
                << "( msg:" << msg->nlmsg_seq << ")";
  scoped_ptr<UserBoundNlMessage> message(
      UserBoundNlMessageFactory::CreateMessage(msg));
  if (message == NULL) {
    SLOG(WiFi, 3) << __func__ << "(msg:NULL)";
  } else {
    SLOG(WiFi, 3) << __func__ << "(msg:" << msg->nlmsg_seq << ")";
    // Call (then erase) any message-specific callback.
    if (ContainsKey(message_callbacks_, message->GetId())) {
      SLOG(WiFi, 3) << "found message-specific callback";
      if (message_callbacks_[message->GetId()].is_null()) {
        LOG(ERROR) << "Callback exists but is NULL for ID " << message->GetId();
      } else {
        message_callbacks_[message->GetId()].Run(*message);
      }
      UnsetMessageCallbackById(message->GetId());
    } else {
      list<Callback>::iterator i = broadcast_callbacks_.begin();
      while (i != broadcast_callbacks_.end()) {
        SLOG(WiFi, 3) << "found a broadcast callback";
        if (i->is_null()) {
          list<Callback>::iterator temp = i;
          ++temp;
          broadcast_callbacks_.erase(i);
          i = temp;
        } else {
          SLOG(WiFi, 3) << "      " << __func__ << " - calling callback";
          i->Run(*message);
          ++i;
        }
      }
    }
  }

  return NL_SKIP;  // Skip current message, continue parsing buffer.
}

}  // namespace shill.
