#!/usr/bin/env python2
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

'''Verify flash protection

RO must not allow to boot RW with either ROLLBACK or RW unprotected
'''

from __future__ import print_function

import sys
import time

import common
import hammerd_api


FLASH_PROTECT_INIT = (common.EC_FLASH_PROTECT_RO_AT_BOOT |
                      common.EC_FLASH_PROTECT_RO_NOW |
                      common.EC_FLASH_PROTECT_ALL_NOW |
                      common.EC_FLASH_PROTECT_GPIO_ASSERTED |
                      common.EC_FLASH_PROTECT_ALL_AT_BOOT |
                      common.EC_FLASH_PROTECT_RW_AT_BOOT |
                      common.EC_FLASH_PROTECT_RW_NOW |
                      common.EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
                      common.EC_FLASH_PROTECT_ROLLBACK_NOW)

FLASH_PROTECT_NORW = (common.EC_FLASH_PROTECT_RO_AT_BOOT |
                      common.EC_FLASH_PROTECT_RO_NOW |
                      common.EC_FLASH_PROTECT_GPIO_ASSERTED |
                      common.EC_FLASH_PROTECT_ROLLBACK_AT_BOOT |
                      common.EC_FLASH_PROTECT_ROLLBACK_NOW)

FLASH_PROTECT_NORB = (common.EC_FLASH_PROTECT_RO_AT_BOOT |
                      common.EC_FLASH_PROTECT_RO_NOW |
                      common.EC_FLASH_PROTECT_GPIO_ASSERTED |
                      common.EC_FLASH_PROTECT_RW_AT_BOOT |
                      common.EC_FLASH_PROTECT_RW_NOW)

def main(argv):
  if len(argv) > 0:
    sys.exit('Test takes no args!')
  updater = hammerd_api.FirmwareUpdater(common.BASE_VENDOR_ID,
                                        common.BASE_PRODUCT_ID,
                                        common.BASE_BUS,
                                        common.BASE_PORT)
  # Load EC image.
  with open(common.IMAGE, 'rb') as f:
    ec_image = f.read()
  updater.LoadEcImage(ec_image)

  common.disable_hammerd()
  common.connect_usb(updater)
  print('PDU Response: %s' % updater.GetFirstResponsePdu().contents)
  print('Current running section: %s' % updater.CurrentSection())

  assert get_flash_protection(updater) == FLASH_PROTECT_INIT, \
  'Initial WP status error'
  unlock_rw(updater)
  common.sim_disconnect_connect(updater)
  # Catch it right after reset: RW is still unlocked and can be updated.
  common.connect_usb(updater)
  print('PDU Response: %s' % updater.GetFirstResponsePdu().contents)
  print('Current running section: %s' % updater.CurrentSection())
  assert get_flash_protection(updater) == FLASH_PROTECT_NORW, \
  'WP status after Unlock RW'
  updater.CloseUsb()
  time.sleep(2)
  # By now, hammer will have jumped to RW and locked the flash again
  common.connect_usb(updater)
  assert get_flash_protection(updater) == FLASH_PROTECT_INIT, \
  'WP status after jump RW'

  updater.SendSubcommand(hammerd_api.UpdateExtraCommand.UnlockRollback)
  common.sim_disconnect_connect(updater)
  common.connect_usb(updater)
  print('PDU Response: %s' % updater.GetFirstResponsePdu().contents)
  print('Current running section: %s' % updater.CurrentSection())
  assert get_flash_protection(updater) == FLASH_PROTECT_NORB, \
  'WP status after Unlock RB'
  updater.CloseUsb()
  time.sleep(2)
  # By now, hammer will have jumped to RW and locked the flash again
  common.connect_usb(updater)
  assert get_flash_protection(updater) == FLASH_PROTECT_INIT, \
  'WP status after jump RW'


def get_flash_protection(updater):
  pdu_resp = updater.GetFirstResponsePdu().contents
  return pdu_resp.flash_protection


def unlock_rw(updater):
  # Check if RW is locked and unlock if needed
  wp_rw = (get_flash_protection(updater) & common.EC_FLASH_PROTECT_RW_NOW) > 0
  print('WP status:  %s' %str(wp_rw))
  if wp_rw:
    print('Need to unlock RW')
    unlocked = updater.UnlockRW()
    assert unlocked == 1, 'Failed to unlock RW'

if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
