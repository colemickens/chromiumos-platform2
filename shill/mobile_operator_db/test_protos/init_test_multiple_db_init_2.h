// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOBILE_OPERATOR_DB_TEST_PROTOS_INIT_TEST_MULTIPLE_DB_INIT_2_H_
#define SHILL_MOBILE_OPERATOR_DB_TEST_PROTOS_INIT_TEST_MULTIPLE_DB_INIT_2_H_

#ifndef IN_MOBILE_OPERATOR_INFO_UNITTEST_CC
  #error "Must be included only from mobile_operator_info_test.cc."
#endif

// Following is the binary protobuf for the following (text representation)
// protobuf:
// mvno {
//   data {
//     uuid: "teeheehee"
//     country: "us"
//     localized_name {
//       name: "Falkner"
//     }
//   }
// }
//
// The binary data for the protobuf in this file was generated by writing the
// prototxt file init_test_multiple_db_init_2.prototxt and then:
//   protoc --proto_path .. --encode "shill.mobile_operator_db.MobileOperatorDB"
//     ../mobile_operator_db.proto < init_test_multiple_db_init_2.prototxt
//     > init_test_multiple_db_init_2.h.pbf
//   cat init_test_multiple_db_init_2.h.pbf | xxd -i

namespace shill {
namespace mobile_operator_db {
static const unsigned char init_test_multiple_db_init_2[] {
  0x1a, 0x1c, 0x12, 0x1a, 0x0a, 0x09, 0x74, 0x65, 0x65, 0x68, 0x65, 0x65,
  0x68, 0x65, 0x65, 0x1a, 0x02, 0x75, 0x73, 0x22, 0x09, 0x0a, 0x07, 0x46,
  0x61, 0x6c, 0x6b, 0x6e, 0x65, 0x72
};
}  // namespace mobile_operator_db
}  // namespace shill

#endif  // SHILL_MOBILE_OPERATOR_DB_TEST_PROTOS_INIT_TEST_MULTIPLE_DB_INIT_2_H_
