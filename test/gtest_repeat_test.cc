// Copyright 2008, Google Inc.
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
// Author: wan@google.com (Zhanyong Wan)

// Tests the --gtest_repeat=number flag.

#include <stdlib.h>
#include <iostream>
#include "gtest/gtest.h"

// Indicates that this translation unit is part of Google Test's
// implementation.  It must come before gtest-internal-inl.h is
// included, or there will be a compiler error.  This trick is to
// prevent a user from accidentally including gtest-internal-inl.h in
// his code.
#define GTEST_IMPLEMENTATION_ 1
#include "src/gtest-internal-inl.h"
#undef GTEST_IMPLEMENTATION_
# include "gtest/internal/gtest-port.h"
# include "gtest/internal/gtest-internal.h"

// file opened at start
// contains information produced by test cases (that may run in subprocesses) and
// verified after all test are finished.
static FILE* log_file = NULL;

namespace testing {

GTEST_DECLARE_string_(death_test_style);
GTEST_DECLARE_string_(filter);
GTEST_DECLARE_int32_(repeat);

}  // namespace testing

using testing::GTEST_FLAG(death_test_style);
using testing::GTEST_FLAG(filter);
using testing::GTEST_FLAG(repeat);

namespace {

// We need this when we are testing Google Test itself and therefore
// cannot use Google Test assertions.
#define GTEST_CHECK_INT_EQ_(expected, actual) \
  do {\
    const int expected_val = (expected);\
    const int actual_val = (actual);\
    if (::testing::internal::IsTrue(expected_val != actual_val)) {\
      ::std::cout << "Value of: " #actual "\n"\
                  << "  Actual: " << actual_val << "\n"\
                  << "Expected: " #expected "\n"\
                  << "Which is: " << expected_val << "\n";\
      ::testing::internal::posix::Abort();\
    }\
  } while(::testing::internal::AlwaysFalse())


// Used for verifying that global environment set-up and tear-down are
// inside the gtest_repeat loop.

class MyEnvironment : public testing::Environment {
 public:
  MyEnvironment() {}
  virtual void SetUp() { 
  fprintf(log_file, "environment_set_up_count\n");
  }
  virtual void TearDown() { 
    fprintf(log_file, "environment_tear_down_count\n");
  }
};

// A test that should fail.

TEST(FooTest, ShouldFail) {
  fprintf(log_file, "should_fail_count\n");
  EXPECT_EQ(0, 1) << "Expected failure.";
}

// A test that should pass.

TEST(FooTest, ShouldPass) {
  fprintf(log_file, "should_pass_count\n");
}

// A test that contains a thread-safe death test and a fast death
// test.  It should pass.

TEST(BarDeathTest, ThreadSafeAndFast) {
  fprintf(log_file, "death_test_count\n");

  GTEST_FLAG(death_test_style) = "threadsafe";
  EXPECT_DEATH_IF_SUPPORTED(::testing::internal::posix::Abort(), "");

  GTEST_FLAG(death_test_style) = "fast";
  EXPECT_DEATH_IF_SUPPORTED(::testing::internal::posix::Abort(), "");
}

#if GTEST_HAS_PARAM_TEST

const int kNumberOfParamTests = 10;

class MyParamTest : public testing::TestWithParam<int> {};

TEST_P(MyParamTest, ShouldPass) {
  // TODO(vladl@google.com): Make parameter value checking robust
  //                         WRT order of tests.
  fprintf(log_file, "param_test_count\n");
}
INSTANTIATE_TEST_CASE_P(MyParamSequence,
                        MyParamTest,
                        testing::Range(0, kNumberOfParamTests));
#endif  // GTEST_HAS_PARAM_TEST

// Resets the count for each test.
void ResetCounts() {
  GTEST_CHECK_(log_file == NULL);
  
  char name_template[] = "/tmp/gtest_log.XXXXXX";
  int log_fd = mkstemp(name_template);
  log_file = testing::internal::posix::FDOpen(log_fd, "w+");
}

testing::internal::String ReadEntireFile(FILE* file) {    
  fseek(file, 0, SEEK_END);
  const size_t file_size = static_cast<size_t>(ftell(file));
  
  char* const buffer = new char[file_size];

  size_t bytes_last_read = 0;  // # of bytes read in the last fread()
  size_t bytes_read = 0;       // # of bytes read so far

  fseek(file, 0, SEEK_SET);

  // Keeps reading the file until we cannot read further or the
  // pre-determined file size is reached.
  do {
    bytes_last_read = fread(buffer+bytes_read, 1, file_size-bytes_read, file);
    bytes_read += bytes_last_read;
  } while (bytes_last_read > 0 && bytes_read < file_size);

  const testing::internal::String content(buffer, bytes_read);
  delete[] buffer;
  return content;
}

int CountOccurences(const char* contents, const char* pattern) {
  int occurences = 0;
  const char* pos = strstr(contents, pattern);
  while (pos != NULL) {
    occurences++;
    pos = strstr(pos + strlen(pattern), pattern);
  }
  return occurences;
}

// Checks that the count for each test is expected.
void CheckCounts(int expected) {
  testing::internal::String contents = ReadEntireFile(log_file);    
  GTEST_CHECK_INT_EQ_(expected, 
    CountOccurences(contents.c_str(), "environment_set_up_count"));
  GTEST_CHECK_INT_EQ_(expected, 
    CountOccurences(contents.c_str(), "environment_tear_down_count"));
  GTEST_CHECK_INT_EQ_(expected, 
    CountOccurences(contents.c_str(), "should_fail_count"));
  GTEST_CHECK_INT_EQ_(expected, 
    CountOccurences(contents.c_str(), "should_pass_count"));
  GTEST_CHECK_INT_EQ_(expected, 
    CountOccurences(contents.c_str(), "death_test_count"));
#if GTEST_HAS_PARAM_TEST
  GTEST_CHECK_INT_EQ_(expected * kNumberOfParamTests, 
    CountOccurences(contents.c_str(), "param_test_count"));
#endif  // GTEST_HAS_PARAM_TEST

  testing::internal::posix::FClose(log_file);
  log_file = NULL;
}

// Tests the behavior of Google Test when --gtest_repeat is not specified.
void TestRepeatUnspecified() {
  ResetCounts();
  GTEST_CHECK_INT_EQ_(1, RUN_ALL_TESTS());
  CheckCounts(1);
}

// Tests the behavior of Google Test when --gtest_repeat has the given value.
void TestRepeat(int repeat) {
  GTEST_FLAG(repeat) = repeat;

  ResetCounts();
  GTEST_CHECK_INT_EQ_(repeat > 0 ? 1 : 0, RUN_ALL_TESTS());
  CheckCounts(repeat);
}

// Tests using --gtest_repeat when --gtest_filter specifies an empty
// set of tests.
void TestRepeatWithEmptyFilter(int repeat) {
  GTEST_FLAG(repeat) = repeat;
  GTEST_FLAG(filter) = "None";

  ResetCounts();
  GTEST_CHECK_INT_EQ_(0, RUN_ALL_TESTS());
  CheckCounts(0);
}

// Tests using --gtest_repeat when --gtest_filter specifies a set of
// successful tests.
void TestRepeatWithFilterForSuccessfulTests(int repeat) {
  GTEST_FLAG(repeat) = repeat;
  GTEST_FLAG(filter) = "*-*ShouldFail";

  ResetCounts();
  GTEST_CHECK_INT_EQ_(0, RUN_ALL_TESTS());
  
  testing::internal::String contents = ReadEntireFile(log_file);   
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "environment_set_up_count"));
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "environment_tear_down_count"));
   GTEST_CHECK_INT_EQ_(0, 
     CountOccurences(contents.c_str(), "should_fail_count"));
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "should_pass_count"));
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "death_test_count"));
#if GTEST_HAS_PARAM_TEST
   GTEST_CHECK_INT_EQ_(repeat * kNumberOfParamTests, 
     CountOccurences(contents.c_str(), "param_test_count"));
#endif  // GTEST_HAS_PARAM_TEST
  testing::internal::posix::FClose(log_file);
  log_file = NULL;
}

// Tests using --gtest_repeat when --gtest_filter specifies a set of
// failed tests.
void TestRepeatWithFilterForFailedTests(int repeat) {
  GTEST_FLAG(repeat) = repeat;
  GTEST_FLAG(filter) = "*ShouldFail";

  ResetCounts();
  GTEST_CHECK_INT_EQ_(1, RUN_ALL_TESTS());
  
  testing::internal::String contents = ReadEntireFile(log_file);   
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "environment_set_up_count"));
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "environment_tear_down_count"));
   GTEST_CHECK_INT_EQ_(repeat, 
     CountOccurences(contents.c_str(), "should_fail_count"));
   GTEST_CHECK_INT_EQ_(0, 
     CountOccurences(contents.c_str(), "should_pass_count"));
   GTEST_CHECK_INT_EQ_(0, 
     CountOccurences(contents.c_str(), "death_test_count"));
#if GTEST_HAS_PARAM_TEST
   GTEST_CHECK_INT_EQ_(0, 
     CountOccurences(contents.c_str(), "param_test_count"));
#endif  // GTEST_HAS_PARAM_TEST
   testing::internal::posix::FClose(log_file);
   log_file = NULL;
}

}  // namespace

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  testing::AddGlobalTestEnvironment(new MyEnvironment);

  TestRepeatUnspecified();
  TestRepeat(0);
  TestRepeat(1);
  TestRepeat(5);

  TestRepeatWithEmptyFilter(2);
  TestRepeatWithEmptyFilter(3);

  TestRepeatWithFilterForSuccessfulTests(3);

  TestRepeatWithFilterForFailedTests(4);

  // It would be nice to verify that the tests indeed loop forever
  // when GTEST_FLAG(repeat) is negative, but this test will be quite
  // complicated to write.  Since this flag is for interactive
  // debugging only and doesn't affect the normal test result, such a
  // test would be an overkill.

  printf("PASS\n");
  return 0;
}
