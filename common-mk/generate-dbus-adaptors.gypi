# Caution!: GYP to GN migration is happening. If you update this file, please
# update common-mk/generate-dbus-adaptors.gni too accordingly.
{
  'variables': {
    'h_dir': '<(SHARED_INTERMEDIATE_DIR)/<(dbus_adaptors_out_dir)',
    'dbus_service_config%': '',
    'dbus_xml_extension%': 'xml',
    'new_fd_bindings%': 1,
    'generator': '<!(which generate-chromeos-dbus-bindings)',
  },
  'rules': [
    {
      'rule_name': 'generate_dbus_adaptors',
      'extension': '<(dbus_xml_extension)',
      'inputs': [
        '<(dbus_service_config)',
        '<(generator)',
      ],
      'outputs': [
        '<(h_dir)/<(RULE_INPUT_ROOT).h',
      ],
      'action': [
        '<(generator)',
        '<(RULE_INPUT_PATH)',
        '--service-config=<(dbus_service_config)',
        '--adaptor=<(h_dir)/<(RULE_INPUT_ROOT).h',
      ],
      'conditions': [
        ['new_fd_bindings==1', {
          'action+': [
            '--new-fd-bindings',
          ],
        }],
      ],
      'msvs_cygwin_shell': 0,
      'message': 'Generating DBus adaptor C++ header from <(RULE_INPUT_PATH)',
      'process_outputs_as_sources': 1,
    },
  ],
  # This target exports a hard dependency because it generates header
  # files.
  'hard_dependency': 1,
}
