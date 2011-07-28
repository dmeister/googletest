// Copyright 2011, Google Inc.
// All rights reserved.
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
// Authors: wan@google.com (Zhanyong Wan), eefacm@gmail.com (Sean Mcafee),
//          dirkmeister@acm.org (Dirk Meister)
//
// The Google C++ Testing Framework (Google Test)
//
// This header file defines internal utilities needed for implementing
// death tests.  They are subject to change without notice.

#ifndef GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TEST_RUNNER_INTERNAL_H_
#define GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TEST_RUNNER_INTERNAL_H_

#include "gtest/internal/gtest-internal.h"

#include <stdio.h>

namespace testing {

GTEST_DECLARE_bool_(crash_safe);

namespace internal {

// Names of the flags (needed for parsing Google Test flags).
const char kCrashSafeFlag[] = "crash_safe";

// TestRunner is a class that is responsible for running the actual test.
// It may or may not create a isolated subprocess in which the actual test is 
// than executed. It is abstract; its static Create method
// returns a concrete class that depends on the --gtest_crash_safe flag.
// By default or --gtest_crash_safe=false a direct test runner is used that
// executed the test in the same process. With --gtest_crash_safe=true, a
// subprocess is created so that crashes of the test do not effect the
// execution of other tests.
//
// Currently crash safe test execution is only available on OS X and Linux.
// Other platforms and test runner implementations may follow.

class TestRunner {
 public:
  // Create returns false if there was an error determining the
  // appropriate action to take for the current test runner;
  // Otherwise, the test_runner pointer should be set to 
  // a new concrete TestRunner that controls the execution of 
  // the current test.
  static bool Create(TestRunner** test_runner);
  
  TestRunner();
  virtual ~TestRunner() { }

  // An enumeration of possible roles that may be taken when a test
  // runner is encountered.  EXECUTE_TEST means that the test runner logic should
  // be executed immediately.  OVERSEE_TEST means that the program should prepare
  // the appropriate environment for a child process to execute the
  // test, then wait for it to complete.
  enum Role { OVERSEE_TEST, EXECUTE_TEST };

  // Assumes one of the above roles.
  virtual Role AssumeRole() = 0;

  // Waits for the test runner to finish.
  // Called only when role is OVERSEE_TEST
  virtual int Wait() = 0;

  // Processes the outcome of the test
  // Called only when role is OVERSEE_TEST
  virtual bool ProcessOutcome() = 0;

  // Reports a test part result to the parent process if appropriate
  // Called only when role is EXECUTE_TEST
  virtual void ReportTestPartResult(const TestPartResult& result) = 0;
  
  // Reports a test property to the parent process if appropriate
  // Called only when role is EXECUTE_TEST
  virtual void RecordProperty(const char* key, const char* value) = 0;

  // Called only when role is EXECUTE_TEST
  virtual void SetUp() = 0;

  // Called only when role is EXECUTE_TEST
  virtual void TearDown() = 0;
  
  // Used internally for testing purposes
  // Called only when role is EXECUTE_TEST 
  virtual void ClearCurrentTestPartResults() = 0;

 private:

  GTEST_DISALLOW_COPY_AND_ASSIGN_(TestRunner);
};

// Factory interface for test runner.  May be mocked out for testing.
class TestRunnerFactory {
 public:
  virtual ~TestRunnerFactory() { }
  virtual bool Create(TestRunner** test_runner) = 0;
};

// A concrete TestRunnerFactory implementation for normal use.
class DefaultTestRunnerFactory : public TestRunnerFactory {
 public:
  virtual bool Create(TestRunner** test_runner);
};

// Result reporter used in the child process to redirect all
// test part results first to the test runner. The
// test runner than may forward the result to the parent process,
// but is always forwards the result to the original reporter to that
// the state of the child process and the state of the parent process are 
// the same
class TestRunnerTestPartResultReporter : public TestPartResultReporterInterface {
  public:
	inline TestRunnerTestPartResultReporter(
	  TestPartResultReporterInterface* original_reporter,
		TestRunner* test_runner);
	
	virtual void ReportTestPartResult(const TestPartResult& result);
  private:
	TestPartResultReporterInterface* original_reporter_;
	TestRunner* test_runner_;
		
	// Copy and assign is ok
};

TestRunnerTestPartResultReporter::TestRunnerTestPartResultReporter(
	TestPartResultReporterInterface* original_reporter,TestRunner* test_runner) 
 : original_reporter_(original_reporter), test_runner_(test_runner) {
}

}  // namespace internal
}  // namespace testing

#endif  // GTEST_INCLUDE_GTEST_INTERNAL_GTEST_TEST_RUNNER_INTERNAL_H_
