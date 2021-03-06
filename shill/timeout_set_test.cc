// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/timeout_set.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/logging.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/net/ip_address.h"

using testing::_;
using testing::AtLeast;
using testing::Test;

namespace shill {

// Expect that the provided elements are the exact elements that timeout.
// This macro will generally be called after calling IncrementTime, and must be
// called within a TYPED_TEST.
#define EXPECT_TIMEOUT(...)                                         \
  do {                                                              \
    std::vector<TypeParam> expected_elements{__VA_ARGS__};          \
    std::sort(expected_elements.begin(), expected_elements.end());  \
    this->SimulateTimeout();                                        \
    std::sort(this->timeout_elements_.begin(),                      \
              this->timeout_elements_.end());                       \
    EXPECT_EQ(expected_elements, this->timeout_elements_);          \
  } while (0)

template <typename T>
struct TestData {
  TestData() {
    data.push_back(1);
    data.push_back(2);
    data.push_back(3);
  }

  std::vector<T> data;
};

template <>
struct TestData<IPAddress> {
  TestData() {
    data.push_back(IPAddress("121.44.30.54"));
    data.push_back(IPAddress("192.144.30.54"));
    data.push_back(IPAddress("0.0.0.0"));
  }

  std::vector<IPAddress> data;
};

template <typename T>
class TimeoutSetTest : public Test {
 public:
  TimeoutSetTest()
      : current_time_(0),
        elements_(&current_time_, &dispatcher_) {
    elements_.SetInformCallback(base::Bind(&TimeoutSetTest::OnTimeout,
                                           base::Unretained(this)));
  }

 protected:
  class TestTimeoutSet : public TimeoutSet<T> {
   public:
    TestTimeoutSet(const int64_t* current_time, EventDispatcher* dispatcher)
        : TimeoutSet<T>(dispatcher),
          current_time_(current_time) {}

   private:
    base::TimeTicks TimeNow() const override {
      return base::TimeTicks::FromInternalValue(*current_time_);
    }

    const int64_t* current_time_;
  };

  void IncrementTime(int64_t amount_ms) { current_time_ += amount_ms*1000; }
  // Acts as though a timeout event occurred. EXPECT_TIMEOUT should generally be
  // used instead of this.
  void SimulateTimeout() { elements_.OnTimeout(); }
  void OnTimeout(std::vector<T> timeout_elements) {
    timeout_elements_ = std::move(timeout_elements);
  }

  int64_t current_time_;
  MockEventDispatcher dispatcher_;
  TestData<T> data_;
  TestTimeoutSet elements_;
  std::vector<T> timeout_elements_;
};

typedef ::testing::Types<char, int, float, IPAddress> TestTypes;
TYPED_TEST_CASE(TimeoutSetTest, TestTypes);

TYPED_TEST(TimeoutSetTest, EmptyInsertion) {
  EXPECT_TRUE(this->elements_.IsEmpty());

  EXPECT_CALL(this->dispatcher_, PostDelayedTask(_, _, _));
  this->elements_.Insert(this->data_.data[0],
                         base::TimeDelta::FromMilliseconds(10));
  EXPECT_FALSE(this->elements_.IsEmpty());
}

TYPED_TEST(TimeoutSetTest, SingleTimeout) {
  EXPECT_CALL(this->dispatcher_, PostDelayedTask(_, _, _))
      .Times(AtLeast(1));
  this->elements_.Insert(this->data_.data[0],
                         base::TimeDelta::FromMilliseconds(10));

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[0]);

  EXPECT_TRUE(this->elements_.IsEmpty());
}

TYPED_TEST(TimeoutSetTest, MultipleSequentialTimeouts) {
  EXPECT_CALL(this->dispatcher_, PostDelayedTask(_, _, _))
      .Times(AtLeast(1));
  this->elements_.Insert(this->data_.data[0],
                         base::TimeDelta::FromMilliseconds(10));
  this->elements_.Insert(this->data_.data[1],
                         base::TimeDelta::FromMilliseconds(20));

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[0]);

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[1]);

  EXPECT_TRUE(this->elements_.IsEmpty());
}

// Single timeout has multiple elements expiring.
TYPED_TEST(TimeoutSetTest, MultiTimeout) {
  EXPECT_CALL(this->dispatcher_, PostDelayedTask(_, _, _))
      .Times(AtLeast(1));
  this->elements_.Insert(this->data_.data[0],
                         base::TimeDelta::FromMilliseconds(10));
  this->elements_.Insert(this->data_.data[1],
                         base::TimeDelta::FromMilliseconds(10));

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[0], this->data_.data[1]);

  EXPECT_TRUE(this->elements_.IsEmpty());
}

TYPED_TEST(TimeoutSetTest, InsertResetTimeout) {
  EXPECT_CALL(this->dispatcher_, PostDelayedTask(_, _, _))
      .Times(AtLeast(1));
  this->elements_.Insert(this->data_.data[0],
                         base::TimeDelta::FromMilliseconds(20));
  this->elements_.Insert(this->data_.data[1],
                         base::TimeDelta::FromMilliseconds(10));

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[1]);

  this->IncrementTime(10);
  EXPECT_TIMEOUT(this->data_.data[0]);

  EXPECT_TRUE(this->elements_.IsEmpty());
}

}  // namespace shill
