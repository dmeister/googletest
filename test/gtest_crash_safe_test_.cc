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
// Author: dirkmeister@acm.org (Dirk Meister)
//
// Tests for death tests.

#include "gtest/gtest-death-test.h"
#include "gtest/gtest.h"
#include "gtest/internal/gtest-filepath.h"

using testing::internal::AlwaysFalse;
using testing::internal::AlwaysTrue;

# if GTEST_OS_WINDOWS
#  include <direct.h>          // For chdir().
# else
#  include <unistd.h>
#  include <sys/wait.h>        // For waitpid.
#  include <limits>            // For std::numeric_limits.
# endif  // GTEST_OS_WINDOWS

# include <limits.h>
# include <signal.h>
# include <stdio.h>

# include "gtest/gtest-spi.h"

// Indicates that this translation unit is part of Google Test's
// implementation.  It must come before gtest-internal-inl.h is
// included, or there will be a compiler error.  This trick is to
// prevent a user from accidentally including gtest-internal-inl.h in
// his code.
# define GTEST_IMPLEMENTATION_ 1
# include "src/gtest-internal-inl.h"
# undef GTEST_IMPLEMENTATION_

namespace posix = ::testing::internal::posix;

using testing::Message;
using testing::internal::DeathTest;
using testing::internal::DeathTestFactory;
using testing::internal::FilePath;
using testing::internal::GetLastErrnoDescription;
using testing::internal::GetUnitTestImpl;
using testing::internal::ParseNaturalNumber;
using testing::internal::String;

void DieWithMessage(const ::std::string& message) {
  fprintf(stderr, "%s", message.c_str());
  fflush(stderr);  // Make sure the text is printed before the process exits.

  // We call _exit() instead of exit(), as the former is a direct
  // system call and thus safer in the presence of threads.  exit()
  // will invoke user-defined exit-hooks, which may do dangerous
  // things that conflict with death tests.
  //
  // Some compilers can recognize that _exit() never returns and issue the
  // 'unreachable code' warning for code following this function, unless
  // fooled by a fake condition.
  if (AlwaysTrue())
    _exit(1);
}

void DieInside(const ::std::string& function) {
  DieWithMessage("death inside " + function + "().\n");
}

// Tests that crash safe testing works.
class TestForCrashSafeTesting : public testing::Test {
 protected:
  TestForCrashSafeTesting() : original_dir_(FilePath::GetCurrentDir()) {}

  virtual ~TestForCrashSafeTesting() {
    posix::ChDir(original_dir_.c_str());
  }

  // A static member function that's expected to die.
  static void StaticMemberFunction() { DieInside("StaticMemberFunction"); }

  // A method of the test fixture that may die.
  void MemberFunction() {
    DieInside("MemberFunction");
  }

  const FilePath original_dir_;
};

// A class with a member function that may die.
class MayDie {
 public:
  explicit MayDie(bool should_die) : should_die_(should_die) {}

  // A member function that may die.
  void MemberFunction() const {
    if (should_die_)
      DieInside("MayDie::MemberFunction");
  }

 private:
  // True iff MemberFunction() should die.
  bool should_die_;
};

// A global function that's expected to die.
void GlobalFunction() { DieInside("GlobalFunction"); }

// Tests that a segmentation fault doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, Segfault) {  
  int* p = 0;  
  while(true) {
    *p = 0;
    p++;
  }
}

// Tests that a math fault doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, Mathfault) {
  
  int a = 10;
  int b = 8 - 8; // necessary to avoid warnings from smart compiliers
  int c = a / b;
  a = c * b;
}

// Tests that a faulty static member function doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, StaticMemberFunction) {
  StaticMemberFunction();
}

// Tests that a faulty member member function doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, MemberFunction) {
  MemberFunction();
}

void ChangeToRootDir() { posix::ChDir(GTEST_PATH_SEP_); }

// Tests that crash-safe tests work even if the current directory has been
// changed.
TEST_F(TestForCrashSafeTesting, DieInChangedDir) {
  ChangeToRootDir();
  _exit(1);
}

// Tests that a faulty method of another class doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, MethodOfAnotherClass) {
  const MayDie x(true);
  x.MemberFunction();
}

// Tests that a faulty global function doesn't lead to a crash
TEST_F(TestForCrashSafeTesting, GlobalFunction) {
  GlobalFunction();
}

// Tests that crash safe testing works when SetUp dies
class TestForCrashSafeTestingSetUpDie : public testing::Test {
  protected:
  
  void SetUp() {
    DieInside("SetUp");
  }
};

TEST_F(TestForCrashSafeTestingSetUpDie, DoesNothing) {
  ASSERT_TRUE(false) << "Should never be reached";
}

// Tests that crash safe testing works when TearDown dies
class TestForCrashSafeTestingTearDownDie : public testing::Test {
  protected:
  
  void TearDown() {
    DieInside("TearDown");
  }
};

TEST_F(TestForCrashSafeTestingTearDownDie, DoesNothing) {
}

int main(int argc, char **argv) {
  testing::GTEST_FLAG(crash_safe) = true;

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}