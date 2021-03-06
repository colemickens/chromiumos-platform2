#!/usr/bin/env python2
# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Python wrapper of hammerd API."""

from __future__ import print_function

import time

import hammerd_api


def main():
  """Demonstrates FirmwareUpdater usage."""
  updater = hammerd_api.FirmwareUpdater(0x18d1, 0x5022, 1, 2)
  # Load EC image.
  with open('/lib/firmware/hammer.fw', 'rb') as f:
    ec_image = f.read()
  updater.LoadEcImage(ec_image)

  print('Connect to base EC.')
  updater.TryConnectUsb()
  updater.SendFirstPdu()
  updater.SendDone()

  print('EC information:')
  print('PDU Response: %s' % updater.GetFirstResponsePdu().contents)
  print('RO: %s' % updater.GetSectionVersion(hammerd_api.SectionName.RO))
  print('RW: %s' % updater.GetSectionVersion(hammerd_api.SectionName.RW))

  print('Assume EC already in RW, send pairing challenge.')
  pair_manager = hammerd_api.PairManager()
  challenge_status = pair_manager.PairChallenge(updater.object)
  print('Challenge status: %d' % challenge_status)

  print('Jump back to RO.')
  updater.SendSubcommand(hammerd_api.UpdateExtraCommand.ImmediateReset)
  updater.CloseUsb()

  print('Inject all-zero entropy.')
  time.sleep(0.5)
  updater.TryConnectUsb()
  updater.SendSubcommand(hammerd_api.UpdateExtraCommand.StayInRO)
  updater.InjectEntropyWithPayload('\x00' * hammerd_api.ENTROPY_SIZE)
  updater.CloseUsb()


if __name__ == '__main__':
  main()
