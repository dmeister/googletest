// Copyright 2005, Google Inc.
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
// Authors: wan@google.com (Zhanyong Wan), eefacm@gmail.com (Sean Mcafee)
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

// DeathTest is a class that hides much of the complexity of the
// GTEST_DEATH_TEST_ macro.  It is abstract; its static Create method
// returns a concrete class that depends on the prevailing death test
// style, as defined by the --gtest_death_test_style and/or
// --gtest_internal_run_death_test flags.

// In describing the results of death tests, these terms are used with
// the corresponding definitions:
//
// exit status:  The integer exit information in the format specified
//               by wait(2)
// exit code:    The integer code passed to exit(3), _exit(2), or
//               returned from main()
class TestRunner {
 public:
  // Create returns false if there was an error determining the
  // appropriate action to take for the current death test; for example,
  // if the gtest_death_test_style flag is set to an invalid value.
  // The LastMessage method will return a more detailed message in that
  // case.  Otherwise, the DeathTest pointer pointed to by the "test"
  // argument is set.  If the death test should be skipped, the pointer
  // is set to NULL; otherwise, it is set to the address of a new concrete
  // DeathTest object that controls the execution of the current test.
  static bool Create(TestRunner** test_runner);
  TestRunner();
  virtual ~TestRunner() { }

  // An enumeration of possible roles that may be taken when a death
  // test is encountered.  EXECUTE means that the death test logic should
  // be executed immediately.  OVERSEE means that the program should prepare
  // the appropriate environment for a child process to execute the death
  // test, then wait for it to complete.
  enum Role { OVERSEE_TEST, EXECUTE_TEST };

  // Assumes one of the above roles.
  virtual Role AssumeRole() = 0;

  // Waits for the test runner to finish.
  virtual int Wait() = 0;

  virtual bool ProcessOutcome() = 0;

  virtual void ReportTestPartResult(const TestPartResult& result) = 0;
  
  virtual void RecordProperty(const char* key, const char* value) = 0;

  virtual void SetUp() = 0;

  virtual void TearDown() = 0;
  
  /**
   * Used internally for testing purposes
   */
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

class TestRunnerTestPartResultReporter : public TestPartResultReporterInterface {
  public:
	inline TestRunnerTestPartResultReporter(TestPartResultReporterInterface* original_reporter,
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
