/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/lib/usb/Minipack16QTransceiverApi.h"
#include "fboss/agent/Utils.h"
#include "fboss/lib/fpga/MinipackFpga.h"

namespace {

constexpr uint32_t kPortsPerPim = 16;

inline uint8_t getPim(int module) {
  return 1 + (module - 1) / kPortsPerPim;
}

inline uint8_t getQsfpPimPort(int module) {
  return (module - 1) % kPortsPerPim;
}

} // namespace

namespace facebook::fboss {
/* Trigger the QSFP hard reset for a given QSFP module in the minipack chassis
 * switch. For that module, this function getsthe PIM module id, PIM port id
 * and then call FPGA function to do QSFP reset
 */
void Minipack16QTransceiverApi::triggerQsfpHardReset(unsigned int module) {
  auto pim = getPim(module);
  auto port = getQsfpPimPort(module);

  MinipackFpga::getInstance()->triggerQsfpHardReset(pim, port);
}

} // namespace facebook::fboss
