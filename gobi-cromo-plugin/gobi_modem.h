// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef PLUGIN_GOBI_MODEM_H_
#define PLUGIN_GOBI_MODEM_H_

#include <glib.h>
#include <pthread.h>
#include <base/basictypes.h>
// TODO(ers) remove following #include once logging spew is resolved
#include <glog/logging.h>
#include <gtest/gtest_prod.h>  // For FRIEND_TEST
#include <map>

#include <cromo/cromo_server.h>
#include <cromo/modem_server_glue.h>
#include <cromo/modem-simple_server_glue.h>
#include <cromo/modem-cdma_server_glue.h>

#include "modem_gobi_server_glue.h"
#include "gobi_sdk_wrapper.h"

typedef std::map<std::string, DBus::Variant> DBusPropertyMap;

// Qualcomm device element, capitalized to their naming conventions
struct DEVICE_ELEMENT {
  char deviceNode[256];
  char deviceKey[16];
};

class CromoServer;
class GobiModemHandler;
class GobiModem
    : public org::freedesktop::ModemManager::Modem_adaptor,
      public org::freedesktop::ModemManager::Modem::Simple_adaptor,
      public org::freedesktop::ModemManager::Modem::Cdma_adaptor,
      public org::chromium::ModemManager::Modem::Gobi_adaptor,
      public DBus::IntrospectableAdaptor,
      public DBus::PropertiesAdaptor,
      public DBus::ObjectAdaptor {
 public:
  typedef std::map<ULONG, int> StrengthMap;

  GobiModem(DBus::Connection& connection,
            const DBus::Path& path,
            const DEVICE_ELEMENT &device,
            gobi::Sdk *sdk);

  virtual ~GobiModem();

  int last_seen() {return last_seen_;}
  void set_last_seen(int scan_count) {
    last_seen_ = scan_count;
  }

  // DBUS Methods: Modem
  virtual void Enable(const bool& enable, DBus::Error& error);
  virtual void Connect(const std::string& number, DBus::Error& error);
  virtual void Disconnect(DBus::Error& error);
  virtual void FactoryReset(const std::string& number, DBus::Error& error);

  virtual ::DBus::Struct<
  uint32_t, uint32_t, uint32_t, uint32_t> GetIP4Config(DBus::Error& error);

  virtual ::DBus::Struct<
    std::string, std::string, std::string> GetInfo(DBus::Error& error);

  // DBUS Methods: ModemSimple
  virtual void Connect(const DBusPropertyMap& properties, DBus::Error& error);
  virtual DBusPropertyMap GetStatus(DBus::Error& error);

  // DBUS Methods: ModemCDMA
  virtual uint32_t GetSignalQuality(DBus::Error& error);
  virtual std::string GetEsn(DBus::Error& error);
  virtual DBus::Struct<uint32_t, std::string, uint32_t> GetServingSystem(
      DBus::Error& error);
  virtual void GetRegistrationState(
      uint32_t& cdma_1x_state, uint32_t& evdo_state, DBus::Error& error);
  virtual void Activate(const std::string& carrier_name,
                        DBus::Error& error);

  // DBUS Methods: ModemGobi
  virtual void SetCarrier(const std::string& image, DBus::Error& error);
  virtual void SoftReset(DBus::Error& error);
  virtual void PowerCycle(DBus::Error& error);

  // DBUS Property Getter
  virtual void on_get_property(DBus::InterfaceAdaptor& interface,
                               const std::string& property,
                               DBus::Variant& value,
                               DBus::Error& error);

  static void set_handler(GobiModemHandler* handler) { handler_ = handler; }

 protected:
  void ActivateOmadm(DBus::Error& error);
  // Verizon uses OTASP, code *22899
  void ActivateOtasp(const std::string& number, DBus::Error& error);
  void ApiConnect(DBus::Error& error);
  ULONG ApiDisconnect(void);
  void GetSignalStrengthDbm(int& strength,
                            StrengthMap *interface_to_strength,
                            DBus::Error& error);
  void RegisterCallbacks();
  void ResetModem(DBus::Error& error);

  struct SerialNumbers {
    std::string esn;
    std::string imei;
    std::string meid;
  };
  void GetSerialNumbers(SerialNumbers* out, DBus::Error &error);
  void LogGobiInformation();

  struct CallbackArgs {
    CallbackArgs()
     : path(NULL) { }
    ~CallbackArgs() { delete path; }
    DBus::Path* path;
  };

  static void PostCallbackRequest(GSourceFunc callback,
                                  CallbackArgs* args) {
    pthread_mutex_lock(&modem_mutex_.mutex_);
    if (connected_modem_) {
      args->path = new DBus::Path(connected_modem_->path());
      g_idle_add(callback, args);
    } else
      delete args;
    pthread_mutex_unlock(&modem_mutex_.mutex_);
  }

  struct NmeaPlusArgs : public CallbackArgs {
    NmeaPlusArgs(LPCSTR nmea, ULONG mode)
      : nmea(nmea),
        mode(mode) { }
    std::string nmea;
    ULONG mode;
  };

  static void NmeaPlusCallbackTrampoline(LPCSTR nmea, ULONG mode) {
    PostCallbackRequest(NmeaPlusCallback,
                        new NmeaPlusArgs(nmea, mode));
  }
  static gboolean NmeaPlusCallback(gpointer data);

  struct ActivationStatusArgs : public CallbackArgs {
    ActivationStatusArgs(ULONG activation_state)
      : activation_state(activation_state) { }
    ULONG activation_state;
  };

  static void ActivationStatusCallbackTrampoline(ULONG activation_state) {
      PostCallbackRequest(ActivationStatusCallback,
                          new ActivationStatusArgs(activation_state));
  }
  static gboolean ActivationStatusCallback(gpointer data);

  struct OmadmStateArgs : public CallbackArgs {
    OmadmStateArgs(ULONG session_state,
                   ULONG failure_reason)
      : session_state(session_state),
        failure_reason(failure_reason) { }
    ULONG session_state;
    ULONG failure_reason;
  };

  static void OmadmStateCallbackTrampoline(ULONG session_state,
                                           ULONG failure_reason) {
    PostCallbackRequest(OmadmStateCallback,
                        new OmadmStateArgs(session_state,
                                           failure_reason));
  }
  static gboolean OmadmStateCallback(gpointer data);

  struct SessionStateArgs : public CallbackArgs {
    SessionStateArgs(ULONG state,
                     ULONG session_end_reason)
      : state(state),
        session_end_reason(session_end_reason) { }
    ULONG state;
    ULONG session_end_reason;
  };

  static void SessionStateCallbackTrampoline(ULONG state,
                                             ULONG session_end_reason) {
    PostCallbackRequest(SessionStateCallback,
                        new SessionStateArgs(state,
                                             session_end_reason));
  }
  static gboolean SessionStateCallback(gpointer data);

  static void DataBearerCallbackTrampoline(ULONG data_bearer_technology) {
    // TODO(ers) remove following log statement after logging spew is resolved
    LOG(INFO) << "DataBearerCallback: " << data_bearer_technology;
    // ignore the supplied argument
    PostCallbackRequest(RegistrationStateCallback, new CallbackArgs());
  }

  static void RoamingIndicatorCallbackTrampoline(ULONG roaming) {
    // TODO(ers) remove following log statement after logging spew is resolved
    LOG(INFO) << "RoamingIndicatorCallback: " << roaming;
    // ignore the supplied argument
    PostCallbackRequest(RegistrationStateCallback, new CallbackArgs());
  }
  static gboolean RegistrationStateCallback(gpointer data);

  struct SignalStrengthArgs : public CallbackArgs {
    SignalStrengthArgs(INT8 signal_strength,
                       ULONG radio_interface)
      : signal_strength(signal_strength),
        radio_interface(radio_interface) { }
    INT8 signal_strength;
    ULONG radio_interface;
  };

  static void SignalStrengthCallbackTrampoline(INT8 signal_strength,
                                               ULONG radio_interface) {
    PostCallbackRequest(SignalStrengthCallback,
                        new SignalStrengthArgs(signal_strength,
                                               radio_interface));
  }
  static gboolean SignalStrengthCallback(gpointer data);

  static void *NMEAThreadTrampoline(void *arg) {
    if (connected_modem_)
      return connected_modem_->NMEAThread();
    else
      return NULL;
  }
  void *NMEAThread(void);

 private:
  static unsigned int QMIReasonToMMReason(unsigned int qmireason);
  void SetDeviceProperties();
  void SetModemProperties();

  void StartNMEAThread();
  // Handlers for events delivered as callbacks by the SDK. These
  // all run in the main thread.
  void RegistrationStateHandler();
  void SignalStrengthHandler(INT8 signal_strength, ULONG radio_interface);
  void SessionStateHandler(ULONG state, ULONG session_end_reason);

  static GobiModemHandler *handler_;
  // Wraps the Gobi SDK for dependency injection
  gobi::Sdk *sdk_;
  DEVICE_ELEMENT device_;
  int last_seen_;  // Updated every scan where the modem is present
  int nmea_fd_; // fifo to write NMEA data to

  pthread_t nmea_thread;

  ULONG activation_state_;
  ULONG session_state_;
  ULONG session_id_;

  struct mutex_wrapper_ {
    mutex_wrapper_() { pthread_mutex_init(&mutex_, NULL); }
    pthread_mutex_t mutex_;
  };
  static GobiModem *connected_modem_;
  static mutex_wrapper_ modem_mutex_;

  bool suspending_;

  friend class GobiModemTest;
  FRIEND_TEST(GobiModemTest, GetSignalStrengthDbmDisconnected);

  bool is_disconnected() { return session_id_ == 0; }

  bool StartExit();
  bool ExitOk();
  bool StartSuspend();
  bool SuspendOk();
  void RegisterStartSuspend(const std::string& name);

  std::string hooks_name_;

  friend bool StartExitTrampoline(void *arg);
  friend bool ExitOkTrampoline(void *arg);
  friend bool StartSuspendTrampoline(void *arg);
  friend bool SuspendOkTrampoline(void *arg);

  DISALLOW_COPY_AND_ASSIGN(GobiModem);
};

#endif  // PLUGIN_GOBI_MODEM_H_
