#
# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# TODO: Fix the visibility on these shared libs.
# gyplint: disable=GypLintVisibilityFlags

{
  'target_defaults': {
    'variables': {
      'deps': [
        # This is a list of pkg-config dependencies
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'protobuf-lite',
      ],
    },
    'conditions': [
      ['USE_tpm2 == 1', {
        'defines': [ 'USE_TPM2' ],
      }],
    ],
  },
  'targets': [
    # A library for just the protobufs.
    {
      'target_name': 'proto_library',
      'type': 'static_library',
      # Use -fPIC so this code can be linked into a shared library.
      'cflags!': ['-fPIE'],
      'cflags': [
        '-fPIC',
        '-fvisibility=default',
      ],
      'variables': {
        'proto_in_dir': 'common',
        'proto_out_dir': 'include/attestation/common',
      },
      'sources': [
        '<(proto_in_dir)/attestation_ca.proto',
        '<(proto_in_dir)/database.proto',
        '<(proto_in_dir)/interface.proto',
        '<(proto_in_dir)/keystore.proto',
        'common/print_attestation_ca_proto.cc',
        'common/print_interface_proto.cc',
        'common/print_keystore_proto.cc',
      ],
      'includes': ['../common-mk/protoc.gypi'],
    },
    # A library for common code.
    {
      'target_name': 'common_library',
      'type': 'static_library',
      'sources': [
        'common/crypto_utility_impl.cc',
        'common/tpm_utility_factory.cc',
      ],
      'all_dependent_settings': {
        'variables': {
          'deps': [
            'openssl',
            'vboot_host',
          ],
        },
        'libraries': [
        ],
      },
      'dependencies': [
        'proto_library',
      ],
      'conditions': [
        ['USE_tpm2 == 1', {
          'sources': [
            'common/tpm_utility_v2.cc',
          ],
          'all_dependent_settings': {
            'libraries': [
              '-ltrunks',
              '-ltpm_manager',
            ],
          },
        }],
        ['USE_tpm2 == 0', {
          'sources': [
            'common/tpm_utility_v1.cc',
          ],
          'all_dependent_settings': {
            'libraries': [
              '-ltspi',
            ],
          },
        }],
      ],
    },
    # A library for client code.
    {
      'target_name': 'client_library',
      'type': 'static_library',
      # Use -fPIC so this code can be linked into a shared library.
      'cflags!': ['-fPIE'],
      'cflags': [
        '-fPIC',
        '-fvisibility=default',
      ],
      'sources': [
        'client/dbus_proxy.cc',
      ],
      'dependencies': [
        'proto_library',
      ],
    },
    # A shared library for clients.
    {
      'target_name': 'libattestation',
      'type': 'shared_library',
      'cflags': [
        '-fvisibility=default',
      ],
      'sources': [
      ],
      'dependencies': [
        'client_library',
        'proto_library',
      ],
    },
    # A client command line utility.
    {
      'target_name': 'attestation_client',
      'type': 'executable',
      'sources': [
        'client/main.cc',
      ],
      'dependencies': [
        'client_library',
        'common_library',
        'proto_library',
      ],
    },
    # A library for server code.
    {
      'target_name': 'server_library',
      'type': 'static_library',
      'sources': [
        'server/attestation_service.cc',
        'server/database_impl.cc',
        'server/dbus_service.cc',
        'server/pkcs11_key_store.cc',
      ],
      'all_dependent_settings': {
        'libraries': [
          '-lchaps',
        ],
      },
      'dependencies': [
        'proto_library',
      ],
    },
    # The attestation daemon.
    {
      'target_name': 'attestationd',
      'type': 'executable',
      'sources': [
        'server/main.cc',
      ],
      'variables': {
        'deps': [
          'libminijail',
        ],
      },
      'dependencies': [
        'common_library',
        'proto_library',
        'server_library',
      ],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'attestation_testrunner',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'variables': {
            'deps': [
              'libbrillo-test-<(libbase_ver)',
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'attestation_testrunner.cc',
            'client/dbus_proxy_test.cc',
            'common/crypto_utility_impl_test.cc',
            'common/mock_crypto_utility.cc',
            'common/mock_tpm_utility.cc',
            'server/attestation_service_test.cc',
            'server/database_impl_test.cc',
            'server/dbus_service_test.cc',
            'server/mock_database.cc',
            'server/mock_key_store.cc',
            'server/pkcs11_key_store_test.cc',
          ],
          'conditions': [
            ['USE_tpm2 == 1', {
              'sources': [
                'common/tpm_utility_v2_test.cc',
              ],
              'libraries': [
                '-ltpm_manager_test',
                '-ltrunks_test',
              ],
            }],
          ],
          'dependencies': [
            'common_library',
            'client_library',
            'proto_library',
            'server_library',
          ],
        },
      ],
    }],
  ],
}
