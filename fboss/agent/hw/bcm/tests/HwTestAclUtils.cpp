/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/test/HwTestAclUtils.h"

#include "fboss/agent/hw/bcm/BcmAclEntry.h"
#include "fboss/agent/hw/bcm/BcmAclTable.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmFieldProcessorUtils.h"
#include "fboss/agent/hw/bcm/BcmStatUpdater.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/hw/bcm/SocUtils.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

DECLARE_int32(acl_gid);

extern "C" {
#include <bcm/field.h>
}

namespace facebook::fboss::utility {
int getAclTableNumAclEntries(const HwSwitch* hwSwitch) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_field_group_t gid = FLAGS_acl_gid;
  int size;

  auto rv =
      bcm_field_entry_multi_get(bcmSwitch->getUnit(), gid, 0, nullptr, &size);
  bcmCheckError(
      rv,
      "failed to get field group entry count, gid=",
      folly::to<std::string>(gid));
  return size;
}

bool numAclTableNumAclEntriesMatch(
    const HwSwitch* hwSwitch,
    int expectedNumAclEntries) {
  return utility::getAclTableNumAclEntries(hwSwitch) == expectedNumAclEntries;
}

void checkSwHwAclMatch(
    const HwSwitch* hwSwitch,
    std::shared_ptr<SwitchState> state,
    const std::string& aclName) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  auto swAcl = state->getAcl(aclName);
  ASSERT_NE(nullptr, swAcl);
  auto hwAcl = bcmSwitch->getAclTable()->getAclIf(swAcl->getPriority());
  ASSERT_NE(nullptr, hwAcl);
  ASSERT_TRUE(BcmAclEntry::isStateSame(
      bcmSwitch, FLAGS_acl_gid, hwAcl->getHandle(), swAcl));
}

bool isAclTableEnabled(const HwSwitch* hwSwitch) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  bcm_field_group_t gid = FLAGS_acl_gid;
  int enable = -1;

  auto rv = bcm_field_group_enable_get(bcmSwitch->getUnit(), gid, &enable);
  bcmCheckError(rv, "failed to get field group enable status");
  CHECK(enable == 0 || enable == 1);
  return (enable == 1);
}

template bool isQualifierPresent<cfg::IpFragMatch>(
    const HwSwitch* hwSwitch,
    const std::shared_ptr<SwitchState>& state,
    const std::string& aclName);

template <typename T>
bool isQualifierPresent(
    const HwSwitch* hwSwitch,
    const std::shared_ptr<SwitchState>& state,
    const std::string& aclName) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  auto swAcl = state->getAcl(aclName);
  auto hwAcl = bcmSwitch->getAclTable()->getAclIf(swAcl->getPriority());

  bcm_field_IpFrag_t hwValueIpFrag{};
  auto ret = bcm_field_qualify_IpFrag_get(
      bcmSwitch->getUnit(), hwAcl->getHandle(), &hwValueIpFrag);

  return ret != BCM_E_INTERNAL;
}

void checkAclEntryAndStatCount(
    const HwSwitch* hwSwitch,
    int aclCount,
    int aclStatCount,
    int counterCount) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  const auto stats = bcmSwitch->getStatUpdater()->getHwTableStats();
  ASSERT_EQ(aclCount, *stats.acl_entries_used_ref());
  ASSERT_EQ(
      aclCount, fpGroupNumAclEntries(bcmSwitch->getUnit(), FLAGS_acl_gid));

  ASSERT_EQ(
      aclStatCount,
      fpGroupNumAclStatEntries(bcmSwitch->getUnit(), FLAGS_acl_gid));
  ASSERT_EQ(counterCount, bcmSwitch->getStatUpdater()->getCounterCount());
}

void checkAclStat(
    const HwSwitch* hwSwitch,
    std::shared_ptr<SwitchState> state,
    std::vector<std::string> acls,
    const std::string& statName,
    std::vector<cfg::CounterType> counterTypes) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);
  auto aclTable = bcmSwitch->getAclTable();

  // Check if the stat has been programmed
  auto hwStat = aclTable->getAclStat(statName);
  ASSERT_NE(nullptr, hwStat);

  // Check the ACL table refcount
  ASSERT_EQ(acls.size(), aclTable->getAclStatRefCount(statName));

  // Check that the SW and HW configs are the same
  for (const auto& aclName : acls) {
    auto swTrafficCounter = getAclTrafficCounter(state, aclName);
    ASSERT_TRUE(swTrafficCounter);
    ASSERT_EQ(statName, *swTrafficCounter->name_ref());
    ASSERT_EQ(counterTypes, *swTrafficCounter->types_ref());
    BcmAclStat::isStateSame(
        bcmSwitch, hwStat->getHandle(), swTrafficCounter.value());
  }

  // Check the Stat Updater
  for (auto type : counterTypes) {
    ASSERT_NE(
        nullptr,
        bcmSwitch->getStatUpdater()->getCounterIf(hwStat->getHandle(), type));
  }
}

void checkAclStatDeleted(
    const HwSwitch* hwSwitch,
    const std::string& statName) {
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  ASSERT_EQ(nullptr, bcmSwitch->getAclTable()->getAclStatIf(statName));
}

void checkAclStatSize(const HwSwitch* hwSwitch, const std::string& statName) {
  /*
   * Check the assumptions made in BcmAclStat::isStateSame()
   * See BcmAclStat.cpp for more info.
   *
   * Check that wedge40's asic is indeed programming *all* the types of
   * counters when asked to program only a subset.
   */
  auto bcmSwitch = static_cast<const BcmSwitch*>(hwSwitch);

  auto hwStat = bcmSwitch->getAclTable()->getAclStat(statName);
  int numCounters;
  auto rv = bcm_field_stat_size(
      bcmSwitch->getUnit(), hwStat->getHandle(), &numCounters);
  bcmCheckError(
      rv, "Unable to get stat size for acl stat=", hwStat->getHandle());

  // We only programmed a packet counter, but TD2 programmed both bytes and
  // packets counters.
  int expectedNumCounters = 1;
  if (SocUtils::isTrident2(bcmSwitch->getUnit()) &&
      bcmSwitch->getBootType() == BootType::WARM_BOOT) {
    expectedNumCounters = 2;
  }
  ASSERT_EQ(expectedNumCounters, numCounters);
}

} // namespace facebook::fboss::utility
