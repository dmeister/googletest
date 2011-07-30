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
// Author: wan@google.com (Zhanyong Wan), vladl@google.com (Vlad Losev)
//
// This file implements tests executes in a subprocess

#include "gtest/internal/gtest-test-runner.h"
#include "gtest/internal/gtest-port.h"

# if GTEST_OS_MAC
#  include <crt_externs.h>
# endif  // GTEST_OS_MAC

# include <errno.h>
# include <fcntl.h>
# include <limits.h>
# include <stdarg.h>

# if GTEST_OS_WINDOWS
#  include <windows.h>
# else
#  include <sys/mman.h>
#  include <sys/wait.h>
# endif  // GTEST_OS_WINDOWS

# if GTEST_OS_QNX
#  include <spawn.h>
# endif  // GTEST_OS_QNX

#include "gtest/gtest-message.h"
#include "gtest/internal/gtest-string.h"

// Indicates that this translation unit is part of Google Test's
// implementation.  It must come before gtest-internal-inl.h is
// included, or there will be a compiler error.  This trick is to
// prevent a user from accidentally including gtest-internal-inl.h in
// his code.
#define GTEST_IMPLEMENTATION_ 1
#include "src/gtest-internal-inl.h"
#undef GTEST_IMPLEMENTATION_

namespace testing {

// Constants.

// The default test runner style.
static const char kDefaultCrashSafeTestRunnerStyle[] = "fast";

GTEST_DEFINE_bool_(
    crash_safe,
    internal::BoolFromGTestEnv("crash_safe", false),
    "TODO");

namespace internal {

// Utilities needed for test runner.

static const char kTestRunnerTestPartResult = 'R';
static const char kTestRunnerTestProperty = 'P';
static const char kTestRunnerExited = 'E';
static const char kTestRunnerClearTestResult = 'C';

// An enumeration describing all of the possible ways that a death test can
// conclude.  DIED means that the process died while executing the test
// code; LIVED means that process lived beyond the end of the test code;
// RETURNED means that the test statement attempted to execute a return
// statement, which is not allowed; THREW means that the test statement
// returned control by throwing an exception.  IN_PROGRESS means the test
// has not yet concluded.
// TODO(vladl@google.com): Unify names and possibly values for
// AbortReason, DeathTestOutcome, and flag characters above.
enum TestRunnerOutcome { TEST_IN_PROGRESS, TEST_DIED, TEST_EXITED };

// Routine for aborting the program from either the test runner
// parent or child process.
void TestRunnerAbort(const String& message) {
  fprintf(stderr, "%s", message.c_str());
  fflush(stderr);
  posix::Abort();
}

// A replacement for CHECK that calls TestRunnerAbort if the assertion
// fails.
# define GTEST_TEST_RUNNER_CHECK_(expression) \
  do { \
    if (!::testing::internal::IsTrue(expression)) { \
      TestRunnerAbort(::testing::internal::String::Format( \
          "CHECK failed: File %s, line %d: %s", \
          __FILE__, __LINE__, #expression)); \
    } \
  } while (::testing::internal::AlwaysFalse())

// This macro is similar to GTEST_TEST_RUNNER_CHECK_, but it is meant for
// evaluating any system call that fulfills two conditions: it must return
// -1 on failure, and set errno to EINTR when it is interrupted and
// should be tried again.  The macro expands to a loop that repeatedly
// evaluates the expression as long as it evaluates to -1 and sets
// errno to EINTR.  If the expression evaluates to -1 but errno is
// something other than EINTR, TestRunnerAbort is called.
# define GTEST_TEST_RUNNER_CHECK_SYSCALL_(expression) \
  do { \
    int gtest_retval; \
    do { \
      gtest_retval = (expression); \
    } while (gtest_retval == -1 && errno == EINTR); \
    if (gtest_retval == -1) { \
      TestRunnerAbort(::testing::internal::String::Format( \
          "CHECK failed: File %s, line %d: %s != -1", \
          __FILE__, __LINE__, #expression)); \
    } \
  } while (::testing::internal::AlwaysFalse())

// Test Runner constructor.
TestRunner::TestRunner() {
}

// Creates and returns a test runner by dispatching to the current
// test runner factory.
bool TestRunner::Create(TestRunner** test_runner) {
  return GetUnitTestImpl()->test_runner_factory()->Create(test_runner);
}

// Basic direct (non crash safe) test runner implementation
class DirectTestRunner : public TestRunner {
public:
  virtual Role AssumeRole() {
	// There is no overseeing in the direct test runner
	return EXECUTE_TEST;
  }

  virtual int Wait() {
    TestRunnerAbort("Should never be called");
	  return 0;
  }

  virtual bool ProcessOutcome() {
	  return true;
  }

  virtual void ReportTestPartResult(const TestPartResult& /*result*/) {
    // There is no parent process we have to forward the test part result
  }

  virtual void RecordProperty(const char* /*key*/, const char* /*value*/) {
    // There is no parent process we have to forward the test property
  }

  virtual void SetUp() {
  }

  virtual void TearDown() {
  }
  
  virtual void ClearCurrentTestPartResults() {
  }
};

# if GTEST_HAS_CRASH_SAFE_TEST_RUNNER

// Provides cross platform implementation for some test runner functionality.
class TestRunnerImpl : public TestRunner {
 protected:
  TestRunnerImpl() :
        spawned_(false),
        outcome_(TEST_IN_PROGRESS),
        read_fd_(-1),
        write_fd_(-1) {}

  // read_fd_ is expected to be closed and cleared by a derived class.
  ~TestRunnerImpl() { GTEST_TEST_RUNNER_CHECK_(read_fd_ == -1); }

  bool spawned() const { return spawned_; }
  void set_spawned(bool is_spawned) { spawned_ = is_spawned; }
  TestRunnerOutcome outcome() const { return outcome_; }
  void set_outcome(TestRunnerOutcome an_outcome) { outcome_ = an_outcome; }
  int read_fd() const { return read_fd_; }
  void set_read_fd(int fd) { read_fd_ = fd; }
  int write_fd() const { return write_fd_; }
  void set_write_fd(int fd) { write_fd_ = fd; }

  // Called in the parent process only. Reads the result code of the death
  // test child process via a pipe, interprets it to set the outcome_
  // member, and closes read_fd_.  Outputs diagnostics and terminates in
  // case of unexpected codes.
  void ReadAndInterpretStatusByte();

  virtual bool ProcessOutcome();

  virtual void ReportTestPartResult(const TestPartResult& result);
  
  virtual void RecordProperty(const char* key, const char* value);

  virtual void SetUp();
  virtual void TearDown();

  virtual void ClearCurrentTestPartResults();

 private:
  // True if the death test child process has been successfully spawned.
  bool spawned_;
  // How the test runner concluded.
  TestRunnerOutcome outcome_;
  // Descriptor to the read end of the pipe to the child process.  It is
  // always -1 in the child process.  The child keeps its write end of the
  // pipe in write_fd_.
  int read_fd_;
  // Descriptor to the child's write end of the pipe to the parent process.
  // It is always -1 in the parent process.  The parent keeps its end of the
  // pipe in read_fd_.
  int write_fd_;
};

void TestRunnerImpl::SetUp() {
}

void TestRunnerImpl::TearDown() {
	posix::Write(write_fd(), "E", 1);
}

// str can be NULL
void SerializeString(const char* str, string* data) {
  if(str != NULL) {
    char null_flag = 1;
    data->append(&null_flag, 1);
    
	  int32_t s = strlen(str); // TODO (dmeister) Use strnlen with constant for maximal path
	  data->append(reinterpret_cast<char*>(&s), sizeof(s));
	  data->append(str);
  } else {
    char null_flag = 0;
    data->append(&null_flag, 1);
  }
}

// Serializes a test part result into a string.
// We use a string length, string contents based approach, we any delimiter-style
// serializtion would be difficult as the delimiter can also be in the result
// message
void SerializeTestPartResult(const TestPartResult& result, string* data) {
	// result type indicator
	if(result.type() == TestPartResult::kSuccess) {
		data->append("S");
	} else if (result.type() == TestPartResult::kNonFatalFailure) {
		data->append("N");
	} else if (result.type() == TestPartResult::kFatalFailure) {
		data->append("F");
	}
	SerializeString(result.file_name(), data);
	int line_number = result.line_number();
	data->append(reinterpret_cast<char*>(&line_number), sizeof(line_number));
	SerializeString(result.message(), data);
}

// Returns -1 in case of error
int SafeRead(int read_fd, char* output, int bytes_to_read) {
	int total_bytes_read = 0;
	int bytes_read = 0;
	do {
		bytes_read = posix::Read(read_fd, output, bytes_to_read);
		if (bytes_read > 0) {
			total_bytes_read += bytes_read;
			output += bytes_read;
			bytes_to_read -= bytes_read;
		}
    } while (bytes_read == -1 && errno == EINTR);
	return total_bytes_read;
}

int ExtractStringFromStream(int read_fd, String* data) {
  char null_flag;
  int bytes_null_flag_size = SafeRead(read_fd, &null_flag, 1);
	if (bytes_null_flag_size <= 0) {
		return bytes_null_flag_size;
	}
	if (null_flag == 0) {
	  // str is NULL
    *data = String(); // constructs a NULL string
    return bytes_null_flag_size;
	}
	uint32_t size;
	int bytes_read_size = SafeRead(read_fd, reinterpret_cast<char*>(&size), sizeof(size));
	if (bytes_read_size <= 0) {
		return bytes_read_size;
	}
	char* buffer = new char[size + 1];
	internal::scoped_ptr<char> scoped_buffer(buffer);
	int bytes_read_string = SafeRead(read_fd, buffer, size);
	if (bytes_read_string <= 0) {
		return bytes_read_string;
	}
	buffer[size] = 0;
  *data = String(buffer);
	return bytes_null_flag_size + bytes_read_size + bytes_read_string;
}

// returns -1 in case of an error, 0 if EOF is reached (unexpected), any positive integer
// otherwise
int ExtractTestPartResultFromStream(int read_fd, TestPartResult** result) {
	char type_flag;
	int bytes_read = SafeRead(read_fd, &type_flag, 1);
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	TestPartResult::Type type;
	if (type_flag == 'S') {
		type = TestPartResult::kSuccess;
	} else if(type_flag == 'N') {
		type = TestPartResult::kNonFatalFailure;
	} else if (type_flag == 'F') {
		type = TestPartResult::kFatalFailure;
	}
	String filename;
	bytes_read = ExtractStringFromStream(read_fd, &filename);
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	int line_number = 0;
	bytes_read = SafeRead(read_fd, reinterpret_cast<char*>(&line_number), sizeof(line_number));
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	String message;
	bytes_read = ExtractStringFromStream(read_fd, &message);
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	*result = new TestPartResult(type, filename.c_str(),
		line_number,
		message.c_str());
	
	return 1; // any positive integer is ok
}

void WriteAcknowledge(int write_fd) {
    posix::Write(write_fd, "A", 1);
}

// Called in the parent process only. Reads the result code of the death
// test child process via a pipe, interprets it to set the outcome_
// member, and closes read_fd_.  Outputs diagnostics and terminates in
// case of unexpected codes.
// TODO (dmeister) Change name
void TestRunnerImpl::ReadAndInterpretStatusByte() {
  char flag;
  int bytes_read;
  TestPartResult* result = NULL;
  UnitTestImpl* impl = GetUnitTestImpl();
			
  bool child_exited = false;
  while (!child_exited) {
  do {
    bytes_read = posix::Read(read_fd(), &flag, 1);
  } while (bytes_read == -1 && errno == EINTR);
  
  if (bytes_read == 0) {	
    set_outcome(TEST_DIED);
	  child_exited = true;
  } else if (bytes_read == 1) {
    if (flag == kTestRunnerTestPartResult) {
		  bytes_read = ExtractTestPartResultFromStream(read_fd(), &result);
      GTEST_TEST_RUNNER_CHECK_(bytes_read > 0);
			
			impl->GetGlobalTestPartResultReporter()->
		        ReportTestPartResult(*result);
		  WriteAcknowledge(write_fd());
		} else if (flag == kTestRunnerTestProperty) {
      String key;
      bytes_read = ExtractStringFromStream(read_fd(), &key);
      GTEST_TEST_RUNNER_CHECK_(bytes_read > 0);
      String value;
      bytes_read = ExtractStringFromStream(read_fd(), &value);
      GTEST_TEST_RUNNER_CHECK_(bytes_read > 0);
        
      // this is no infinite loop because current_test_runner() is always only set in the
      // subprocess
      impl->current_test_result()->RecordProperty(TestProperty(key.c_str(), value.c_str()));
        
      WriteAcknowledge(write_fd());  
    } else if (flag == kTestRunnerClearTestResult) {
	    TestResultAccessor::ClearTestPartResults(
             GetUnitTestImpl()->current_test_result());
      WriteAcknowledge(write_fd());
    } else if (flag == kTestRunnerExited) {
		  // We use an explicit exit marker message instead of the exit code of the test
		  // to avoid counting exit(0) calls in the user test as successful tests
		  set_outcome(TEST_EXITED);
		  child_exited = true;
	  } else {
	    child_exited = true;
      GTEST_LOG_(FATAL) << "Test child process reported "
                        << "unexpected status byte ("
                        << static_cast<unsigned int>(flag) << ")";
      }
    } else {
      GTEST_LOG_(FATAL) << "Read from death test child process failed: "
                      << GetLastErrnoDescription();
	    child_exited = true;
    }
  }
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(posix::Close(read_fd()));
  set_read_fd(-1);
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(posix::Close(write_fd()));
  set_write_fd(-1);
}

bool TestRunnerImpl::ProcessOutcome() {
	if (!spawned()) {
    return false;
  }
    
  const String error_message = GetCapturedStderr();
  fprintf(stderr, "%s", error_message.c_str());
  if (outcome() == TEST_EXITED) {
    // everything is fine
  } else if (outcome() == TEST_DIED) {
    const TestInfo* test_info = UnitTest::GetInstance()->current_test_info();
    Message msg;
    msg << "Test process died while executing " << 
      test_info->test_case_name() << "." << test_info->name();
	  ReportFailureInUnknownLocation(TestPartResult::kFatalFailure,
                          msg.GetString());
  } else {
	    GTEST_LOG_(FATAL) << "Unexpected child process outcome";
	}
	return true;
}

void WaitForAcknowledge(int read_fd) {
  char flag;
  int read_bytes = posix::Read(read_fd, &flag, 1);
  GTEST_TEST_RUNNER_CHECK_(read_bytes == 1);
  GTEST_TEST_RUNNER_CHECK_(flag == 'A');
}

// Called in child process only
void TestRunnerImpl::ClearCurrentTestPartResults() {
  GTEST_TEST_RUNNER_CHECK_(write_fd() != -1);

	// TODO (dmeister) Use a safe write method
	posix::Write(write_fd(), "C", 1);
  WaitForAcknowledge(read_fd());
}

// Called in child process only
void TestRunnerImpl::ReportTestPartResult(const TestPartResult& result) {
	GTEST_TEST_RUNNER_CHECK_(write_fd() != -1);
	
	string data;
	data.append("R"); // for test part result
	SerializeTestPartResult(result, &data);

  fflush(stdout); // ensure ordering in output
  fflush(stderr);
  
	posix::Write(write_fd(), data.data(), data.size());
  WaitForAcknowledge(read_fd());
}

void TestRunnerImpl::RecordProperty(const char* key, const char* value) {
  GTEST_TEST_RUNNER_CHECK_(write_fd() != -1);
  	
  string data;
  data.append(&kTestRunnerTestProperty, 1);
  SerializeString(key, &data);
  SerializeString(value, &data);

  fflush(stdout); // ensure ordering in output
  fflush(stderr);
  
  posix::Write(write_fd(), data.data(), data.size());
  WaitForAcknowledge(read_fd());
}

// ForkingTestRunner provides implementations for most of the abstract
// methods of the TestRunner interface.  Only the AssumeRole method is
// left undefined.
class ForkingTestRunner : public TestRunnerImpl {
 public:
  ForkingTestRunner();

  // All of these virtual functions are inherited from TestRunner.
  virtual int Wait();

 protected:
  void set_child_pid(pid_t child_pid) { child_pid_ = child_pid; }

 private:
  // PID of child process during death test; 0 in the child process itself.
  pid_t child_pid_;
};

// Constructs a ForkingTestRunner.
ForkingTestRunner::ForkingTestRunner() {}

// Waits for the child in a test runner to exit, returning its exit
// status, or 0 if no child process exists.  As a side effect, sets the
// outcome data member.
int ForkingTestRunner::Wait() {
  if (!spawned())
    return 0;

  ReadAndInterpretStatusByte();

  int status_value;
  GTEST_DEATH_TEST_CHECK_SYSCALL_(waitpid(child_pid_, &status_value, 0));
  return status_value;
}

// A concrete death test class that forks, then immediately runs the test
// in the child process.
class NoExecTestRunner : public ForkingTestRunner {
 public:
  NoExecTestRunner() { }
  virtual Role AssumeRole();

  virtual void TearDown();
};

// The AssumeRole process for a fork-and-run death test.  It implements a
// straightforward fork, with a simple pipe to transmit the status byte.
TestRunner::Role NoExecTestRunner::AssumeRole() {
  int in_pipe_fd[2];
  GTEST_TEST_RUNNER_CHECK_(pipe(in_pipe_fd) != -1);
  int out_pipe_fd[2];
  GTEST_TEST_RUNNER_CHECK_(pipe(out_pipe_fd) != -1);

  CaptureStderr();
  // When we fork the process below, the log file buffers are copied, but the
  // file descriptors are shared.  We flush all log files here so that closing
  // the file descriptors in the child process doesn't throw off the
  // synchronization between descriptors and buffers in the parent process.
  // This is as close to the fork as possible to avoid a race condition in case
  // there are multiple threads running before the death test, and another
  // thread writes to the log file.
  FlushInfoLog();

  const pid_t child_pid = fork();
  GTEST_TEST_RUNNER_CHECK_(child_pid != -1);
  set_child_pid(child_pid);
  if (child_pid == 0) {	
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(out_pipe_fd[0]));
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(in_pipe_fd[1]));
    set_write_fd(out_pipe_fd[1]);
    set_read_fd(in_pipe_fd[0]);
    // Redirects all logging to stderr in the child process to prevent
    // concurrent writes to the log files.  We capture stderr in the parent
    // process and append the child process' output to a log.
    LogToStderr();
    // Event forwarding to the listeners of event listener API mush be shut
    // down in death test subprocesses.
    GetUnitTestImpl()->listeners()->SuppressEventForwarding();
    return EXECUTE_TEST;
  } else {
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(out_pipe_fd[1]));
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(in_pipe_fd[0]));
    set_read_fd(out_pipe_fd[0]);
    set_write_fd(in_pipe_fd[1]);
    set_spawned(true);
    return OVERSEE_TEST;
  }
}

void NoExecTestRunner::TearDown() {
	TestRunnerImpl::TearDown();
	// the work of the subprocess is done.
	exit(0);
}

#endif // GTEST_HAS_CRASH_SAFE_TEST_RUNNER

// Creates a concrete TestRunner-derived class that depends on the
// --gtest_test_runner_style flag, and sets the pointer pointed to
// by the "test" argument to its address.  If the test should be
// skipped, sets that pointer to NULL.  Returns true, unless the
// flag is set to an invalid value.
bool DefaultTestRunnerFactory::Create(TestRunner** test_runner) {
  if (!GTEST_FLAG(crash_safe)) {
    *test_runner = new DirectTestRunner();
    return true;
  }
# if GTEST_HAS_CRASH_SAFE_TEST_RUNNER
  *test_runner = new NoExecTestRunner();
# else
  GTEST_LOG_(FATAL) << "Crash safe test execution is currently not supported on this platform.";
  return false;
# endif  // GTEST_HAS_CRASH_SAFE_TEST_RUNNER
  return true;
}

void TestRunnerTestPartResultReporter::ReportTestPartResult(const TestPartResult& result) {
	original_reporter_->ReportTestPartResult(result);
	test_runner_->ReportTestPartResult(result);
}


}  // namespace internal

}  // namespace testing
