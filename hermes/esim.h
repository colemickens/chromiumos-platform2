// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_ESIM_H_
#define HERMES_ESIM_H_

#include <libqrtr.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <base/message_loop/message_loop.h>

#include "hermes/qmi_constants.h"
#include "hermes/qmi_uim.h"

namespace hermes {

// EsimError will be the generic abstraction from specific protocol errors to an
// error that the interface can understand. This should contain a set of errors
// that are recoverable, meaning if they occur, the process can still complete
// successfully. This should also include errors that are fatal, and should be
// reported back to the user.
enum class EsimError {
  kEsimSuccess,       // success condition
  kEsimError,         // fatal
  kEsimNotConnected,  // non-fatal
};

// Provides the functionality to allow the LPD to communicate with the eUICC.
// This class is responsible for opening, maintaining, and closing the logical
// channel that will be opened to the chip.
class Esim : public base::MessageLoopForIO::Watcher {
 public:
  using ErrorCallback = base::Callback<void(EsimError error_data)>;
  using DataCallback =
      base::Callback<void(const std::vector<uint8_t>& esim_data)>;

  static std::unique_ptr<Esim> Create(std::string imei,
                                      std::string matching_id);
  static std::unique_ptr<Esim> CreateForTest(base::ScopedFD* sock,
                                             std::string imei,
                                             std::string matching_id);

  // Sets up FileDescriptorWatcher and transaction handling. Gets node and port
  // for UIM service from QRTR. Calls |success_callback| upon successful
  // intialization, |error_callback| on error.
  //
  // Parameters
  //  success_callback  - Closure to continue execution after eSIM is
  //                      initialized
  //  error_callack     - function to handle error if initialization fails
  void Initialize(const base::Closure& success_callback,
                  const ErrorCallback& error_callback);

  ~Esim() = default;

  // Makes eSIM API call to request the eSIM to open a logical channel to
  // communicate through. This will be the method through which all two way
  // communication will be dealt with the hardware. Calls |data_callback|
  // on successful open channel, or |error_callback| on error.
  //
  // Parameters
  //  data_callback   - function to call on successful channel opening
  //  error_callback  - function to handle error returned from eSIM
  void OpenLogicalChannel(const DataCallback& data_callback,
                          const ErrorCallback& error_callback);

  // Makes eSIM API call to request the eSIM to return either the info1 or
  // the info2 block of data to send to the SM-DP+ server to begin
  // Authentication. Calls |callback| with the newly returned data, or
  // |error_callback| on error.
  //
  // Parameters
  //  which          - specify either the info1 or info2 data
  //  callback       - function to call on successful return of specified eSIM
  //                   info data
  //  error_callback - function to handle error returned from eSIM
  void GetInfo(int which,
               const DataCallback& data_callback,
               const ErrorCallback& error_callback);
  // Makes eSIM API call to request the eSIM to return the eSIM Challenge,
  // which is the second parameter needed to begin Authentication with the
  // SM-DP+ server. Calls |callback| on returned challenge, or |error_callback|
  // on error.
  //
  // Parameters
  //  callback       - function to call on successful return of eSIM challenge
  //                   data blob
  //  error_callback - function to handle error returned from eSIM
  void GetChallenge(const DataCallback& data_callback,
                    const ErrorCallback& error_callback);
  // Makes eSIM API call to request eSIM to authenticate server's signature,
  // which is supplied in server_signature. On success, call |callback| with
  // the eSIM's response. If the authentication fails, call |error_callback|.
  //
  // Parameters
  //  server_signed1            - data that has been signed with
  //                              |server_signature| server_signature SM-DP+
  //                              encryption signature
  //  euicc_ci_pk_id_to_be_used - list of public keys for the eSIM to choose
  //  server_certificate        - SM-DP+ certificate
  //  data_callback             - function to call with the data returned from
  //                              the eSIM on successful authentication of
  //                              |server_data|
  //  error_callback            - function to call if |server_data| is
  //                              determined to be invalid by eSIM chip
  void AuthenticateServer(const std::vector<uint8_t>& server_signed1,
                          const std::vector<uint8_t>& server_signature,
                          const std::vector<uint8_t>& euicc_ci_pk_id_to_be_used,
                          const std::vector<uint8_t>& server_certificate,
                          const DataCallback& data_callback,
                          const ErrorCallback& error_callback);
  void PrepareDownloadRequest(const std::vector<uint8_t>& smdp_signed2,
                              const std::vector<uint8_t>& smdp_signature2,
                              const std::vector<uint8_t>& smdp_certificate,
                              const DataCallback& data_callback,
                              const ErrorCallback& error_callback);
  void LoadBoundProfilePackage(
      const std::vector<uint8_t>& bound_profile_package,
      const DataCallback& success_callback,
      const ErrorCallback& error_callback);

 private:
  // TODO(jruthe): IMEI is hardware specific and should be retrieved from the
  // hardware configuration
  Esim(uint8_t slot,
       const std::string& imei,
       const std::string& matching_id,
       base::ScopedFD fd);

  void CloseChannel();
  void SendApdu(const DataCallback& callback,
                const ErrorCallback& error_callback);

  // APDUs have a payload size limit of 255 bytes. Thus payloads that are
  // greater than 255 bytes, must be fragmented into several APDUs that are
  // sent sequentially. Each time a payload of 255 bytes is constructed, it is
  // appended to |apdu_queue_|. If |apdu_payload| is smaller than 255 bytes, it
  // is simply given a header and appended to |apdu_queue_|.
  //
  // Parameters
  //  cla           - CLA byte as defined in ISO 7816 5.1.1
  //  ins           - INS byte as defined in ISO 7816 5.1.2
  //  apdu_payload  - Buffer containing an APDU payload
  void FragmentAndQueueApdu(uint8_t cla,
                            uint8_t ins,
                            const std::vector<uint8_t>& apdu_payload);

  // Messages sent between the Lpd and the eSIM chip are done through a series
  // of transactions. The Lpd can intiate transactions and the eSIM will
  // respond with a matching transaction number to the Lpd's request.
  //
  // This pair of functions map a set of callbacks data and error to the current
  // value of |current_transaction_|. The eSIM will respond after some amount of
  // time and the Lpd will read the bytes from the socket opened to the QRTR
  // socket. Finalize transaction will handle the reception of APDUs and map
  // them back to transaction numbers by doing lookups into
  // |response_callbacks_|.
  //
  // Parameters
  //  packet          - Encoded QRTR packet to send via qrtr_sendto.
  //  data_callback   - Callback to call once corresponding transaction has been
  //                    received and processed.
  //  error_callback  - Error handler after transaction has been received but
  //                    encounters an error during processing.
  void InitiateTransaction(const qrtr_packet& packet,
                           const DataCallback& data_callback,
                           const ErrorCallback& error_callback);
  // Parameters
  //  packet          - QRTR packet to decode and process.
  void FinalizeTransaction(const qrtr_packet& packet);

  // Build CtxParams object described in SGP.22 5.7.13 including matchingId,
  // deviceInfo and their respective parameters.
  //
  // Parameters
  //  ctx_params  - vector of bytes that will be filled with ctx_params endoded
  //                in ASN.1 format. ctx_params is not modified if the
  //                construction fails
  //
  // Returns  - True if ctx_params are constructed, false otherwise.
  bool ConstructCtxParams(std::vector<uint8_t>* ctx_params);

  uint16_t GetTransactionNumber(const qrtr_packet& packet) const;

  bool ResponseSuccess(uint16_t response) const;

  bool MorePayloadIncoming() const;

  uint8_t GetNextPayloadSize() const;

  bool StringToBcdBytes(const std::string& source, std::vector<uint8_t>* dest);

  void QueueStoreData(const std::vector<uint8_t>& payload);

  // base::MessageLoopForIO::Watcher overrides.
  void OnFileCanReadWithoutBlocking(int fd) override;
  void OnFileCanWriteWithoutBlocking(int fd) override;

  // FileDescriptorWatcher to watch the QRTR socket for reads.
  base::MessageLoopForIO::FileDescriptorWatcher watcher_;

  // Counter for each initiated transaction.
  uint16_t current_transaction_;

  // IMEI number
  std::string imei_;

  // Matching ID of profile to install
  std::string matching_id_;

  struct TransactionCallback {
    TransactionCallback() = default;
    TransactionCallback(const DataCallback& data_callback,
                        const ErrorCallback& error_callback);
    DataCallback data_callback;
    ErrorCallback error_callback;
  };

  // Mapping of transactions to callbacks from Lpd once the eSIM has responded
  // to a request.
  std::map<int, TransactionCallback> response_callbacks_;

  // Queue of completed packets to send to the eSIM, these will be sent one at
  // a time through Esim::SendApdu as success responses are received.
  std::deque<std::vector<uint8_t> > apdu_queue_;

  // The slot that the logical channel to the eSIM will be made. Initialized in
  // constructor, hardware specific.
  uint8_t slot_;

  // Buffer for storing data from the QRTR socket.
  std::vector<uint8_t> buffer_;

  // Node and port to pass to qrtr_sendto, returned from qrtr_new_lookup
  // response, which contains QRTR_TYPE_NEW_SERVER and the node and port number
  // to use in following qrtr_sendto calls.
  uint32_t node_;
  uint32_t port_;

  // Logical Channel that will be used to communicate with the chip, returned
  // from OPEN_LOGICAL_CHANNEL request sent once the QRTR socket has been
  // opened.
  uint8_t channel_;

  // Status returned as the last two bytes in APDU messages
  uint8_t sw1_;
  uint8_t sw2_;

  // All APDU bytes. sw1_ and sw2_ are extracted immediately after reception.
  std::vector<uint8_t> payload_;

  // Closure given to Esim::Initialize to call once the UIM service has been
  // exposed through QRTR and |node_| and |port_| have been set.
  base::Closure initialize_callback_;

  // ScopedFD to hold the qrtr socket file descriptor returned by qrtr_open.
  // Initialized in Create or CreateForTest, which will be passed to the
  // constructor, ScopedFD will be destructed with Esim object.
  base::ScopedFD qrtr_socket_fd_;
  base::WeakPtrFactory<Esim> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(Esim);
};

}  // namespace hermes

#endif  // HERMES_ESIM_H_
