// Copyright 2009 Google Inc. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Author: vladl@google.com (Vlad Losev)
//
// The Google C++ Testing Framework (Google Test)
//
// This file verifies Google Test event listeners receive events at the
// right times.

#include "gtest/gtest.h"
#include <vector>

using ::testing::AddGlobalTestEnvironment;
using ::testing::Environment;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestCase;
using ::testing::TestEventListener;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;
using ::testing::internal::String;

// Used by tests to register their events.
std::vector<String>* g_events = NULL;

void PushEvent(const String& event) {
    printf("Event: %s\n", event.c_str());
    fflush(stdout);
    g_events->push_back(event);
}

namespace testing {
namespace internal {

class EventRecordingListener : public TestEventListener {
 public:
  EventRecordingListener(const char* name) : name_(name) {}

 protected:
  virtual void OnTestProgramStart(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnTestProgramStart"));
  }

  virtual void OnTestIterationStart(const UnitTest& /*unit_test*/,
                                    int iteration) {
    Message message;
    message << GetFullMethodName("OnTestIterationStart")
            << "(" << iteration << ")";
    PushEvent(message.GetString());
  }

  virtual void OnEnvironmentsSetUpStart(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnEnvironmentsSetUpStart"));
  }

  virtual void OnEnvironmentsSetUpEnd(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnEnvironmentsSetUpEnd"));
  }

  virtual void OnTestCaseStart(const TestCase& /*test_case*/) {
    PushEvent(GetFullMethodName("OnTestCaseStart"));
  }

  virtual void OnTestStart(const TestInfo& /*test_info*/) {
    PushEvent(GetFullMethodName("OnTestStart"));
  }

  virtual void OnTestPartResult(const TestPartResult& /*test_part_result*/) {
    PushEvent(GetFullMethodName("OnTestPartResult"));
  }

  virtual void OnTestEnd(const TestInfo& /*test_info*/) {
    PushEvent(GetFullMethodName("OnTestEnd"));
  }

  virtual void OnTestCaseEnd(const TestCase& /*test_case*/) {
    PushEvent(GetFullMethodName("OnTestCaseEnd"));
  }

  virtual void OnEnvironmentsTearDownStart(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnEnvironmentsTearDownStart"));
  }

  virtual void OnEnvironmentsTearDownEnd(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnEnvironmentsTearDownEnd"));
  }

  virtual void OnTestIterationEnd(const UnitTest& /*unit_test*/,
                                  int iteration) {
    Message message;
    message << GetFullMethodName("OnTestIterationEnd")
            << "("  << iteration << ")";
    PushEvent(message.GetString());
  }

  virtual void OnTestProgramEnd(const UnitTest& /*unit_test*/) {
    PushEvent(GetFullMethodName("OnTestProgramEnd"));
  }

 private:
  String GetFullMethodName(const char* name) {
    Message message;
    message << name_ << "." << name;
    return message.GetString();
  }

  String name_;
};

class EnvironmentInvocationCatcher : public Environment {
 protected:
  virtual void SetUp() {
    PushEvent(String("Environment::SetUp"));
  }

  virtual void TearDown() {
    PushEvent(String("Environment::TearDown"));
  }
};

class ListenerTest : public Test {
 protected:
  static void SetUpTestCase() {
    PushEvent(String("ListenerTest::SetUpTestCase"));
  }

  static void TearDownTestCase() {
    PushEvent(String("ListenerTest::TearDownTestCase"));
  }

  virtual void SetUp() {
    PushEvent(String("ListenerTest::SetUp"));
  }

  virtual void TearDown() {
    PushEvent(String("ListenerTest::TearDown"));
  }
};

TEST_F(ListenerTest, DoesFoo) {
  // Test execution order within a test case is not guaranteed so we are not
  // recording the test name.
  PushEvent(String("ListenerTest::* Test Body"));
  SUCCEED();  // Triggers OnTestPartResult.
}

TEST_F(ListenerTest, DoesBar) {
  PushEvent(String("ListenerTest::* Test Body"));
  SUCCEED();  // Triggers OnTestPartResult.
}

}  // namespace internal

}  // namespace testing

using ::testing::internal::EnvironmentInvocationCatcher;
using ::testing::internal::EventRecordingListener;

int main(int argc, char **argv) {
  std::vector<String> events;
  g_events = &events;
  InitGoogleTest(&argc, argv);

  UnitTest::GetInstance()->listeners().Append(
      new EventRecordingListener("1st"));
  UnitTest::GetInstance()->listeners().Append(
      new EventRecordingListener("2nd"));

  AddGlobalTestEnvironment(new EnvironmentInvocationCatcher);

  GTEST_CHECK_(events.size() == 0)
      << "AddGlobalTestEnvironment should not generate any events itself.";

  ::testing::GTEST_FLAG(repeat) = 2;
  return RUN_ALL_TESTS();
}
