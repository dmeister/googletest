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
static const char kDefaultTestRunnerStyle[] = "fast";

GTEST_DEFINE_string_(
    test_runner_style,
    internal::StringFromGTestEnv("test_runner_style", kDefaultTestRunnerStyle),
    "Indicates how to run a test in a forked child process: "
    "\"threadsafe\" (child process re-executes the test binary "
    "from the beginning, running only the specific test) or "
    "\"fast\" (child process runs the test immediately "
    "after forking).");

GTEST_DEFINE_bool_(
    test_runner_use_fork,
    internal::BoolFromGTestEnv("test_runner_use_fork", false),
    "Instructs to use fork()/_exit() instead of clone() in tests. "
    "Ignored and always uses fork() on POSIX systems where clone() is not "
    "implemented. Useful when running under valgrind or similar tools if "
    "those do not support clone(). Valgrind 3.3.1 will just fail if "
    "it sees an unsupported combination of clone() flags. "
    "It is not recommended to use this flag w/o valgrind though it will "
    "work in 99% of the cases. Once valgrind is fixed, this flag will "
    "most likely be removed.");

namespace internal {
GTEST_DEFINE_string_(
    internal_test_runner, "",
    "Indicates the file, line number, temporal index of "
    "the single test to run, and a file descriptor to "
    "which a success code may be sent, all separated by "
    "colons.  This flag is specified if and only if the current "
    "process is a sub-process launched for running a thread-safe "
    "death test.  FOR INTERNAL USE ONLY.");
}  // namespace internal

namespace internal {

// Utilities needed for test runner.

static const char kTestRunnerTestPartResult = 'R';
static const char kTestRunnerExited = 'E';
static const char kTestRunnerInternalError = 'I';

// An enumeration describing all of the possible ways that a death test can
// conclude.  DIED means that the process died while executing the test
// code; LIVED means that process lived beyond the end of the test code;
// RETURNED means that the test statement attempted to execute a return
// statement, which is not allowed; THREW means that the test statement
// returned control by throwing an exception.  IN_PROGRESS means the test
// has not yet concluded.
// TODO(vladl@google.com): Unify names and possibly values for
// AbortReason, DeathTestOutcome, and flag characters above.
// TODO(dmeister): Unify with death test outcomes
enum TestRunnerOutcome { TEST_IN_PROGRESS, TEST_DIED, TEST_EXITED };

// Routine for aborting the program which is safe to call from an
// exec-style death test child process, in which case the error
// message is propagated back to the parent process.  Otherwise, the
// message is simply printed to stderr.  In either case, the program
// then exits with status 1.
void TestRunnerAbort(const String& message) {
  // On a POSIX system, this function may be called from a threadsafe-style
  // death test child process, which operates on a very small stack.  Use
  // the heap for any additional non-minuscule memory requirements.
  const InternalTestRunnerFlag* const flag = GetUnitTestImpl()->internal_test_runner_flag();
  if (flag != NULL) {
    FILE* parent = posix::FDOpen(flag->write_fd(), "w");
    fputc(kTestRunnerInternalError, parent);
    fprintf(parent, "%s", message.c_str());
    fflush(parent);
    _exit(1);
  } else {
    fprintf(stderr, "%s", message.c_str());
    fflush(stderr);
    posix::Abort();
  }
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

const char* TestRunner::LastMessage() {
  return last_test_message_.c_str();
}

void TestRunner::set_last_test_message(const String& message) {
  last_test_message_ = message;
}

String TestRunner::last_test_message_;

// Provides cross platform implementation for some test runner functionality.
class TestRunnerImpl : public TestRunner {
 protected:
  TestRunnerImpl() :
        spawned_(false),
        status_(-1),
        outcome_(TEST_IN_PROGRESS),
        read_fd_(-1),
        write_fd_(-1) {}

  // read_fd_ is expected to be closed and cleared by a derived class.
  ~TestRunnerImpl() { GTEST_TEST_RUNNER_CHECK_(read_fd_ == -1); }

  bool spawned() const { return spawned_; }
  void set_spawned(bool is_spawned) { spawned_ = is_spawned; }
  int status() const { return status_; }
  void set_status(int a_status) { status_ = a_status; }
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

  virtual void SetUp();
  virtual void TearDown();

 private:
  // True if the death test child process has been successfully spawned.
  bool spawned_;
  // The exit status of the child process.
  int status_;
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

void SerializeString(const char* str, string* data) {
	int32_t s = strlen(str); // TODO (dmeister) Use strnlen with constant for maximal path
	data->append(reinterpret_cast<char*>(&s), sizeof(s));
	data->append(str);
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

int ExtractStringFromStream(int read_fd, string* data) {
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
	data->assign(buffer);
	return bytes_read_size + bytes_read_string;
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
	string filename;
	bytes_read = ExtractStringFromStream(read_fd, &filename);
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	int line_number = 0;
	bytes_read = SafeRead(read_fd, reinterpret_cast<char*>(&line_number), sizeof(line_number));
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	string message;
	bytes_read = ExtractStringFromStream(read_fd, &message);
	if (bytes_read <= 0) {
		return bytes_read;
	}
	
	*result = new TestPartResult(type, filename.c_str(),
		line_number,
		message.c_str());
	
	return 1; // any positive integer is ok
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
    switch (flag) {
	  case kTestRunnerTestPartResult:
		bytes_read = ExtractTestPartResultFromStream(read_fd(), &result);
		if (bytes_read > 0) {
			impl->GetTestPartResultReporterForCurrentThread()->
		      ReportTestPartResult(*result);
		}
		break;
	  case kTestRunnerExited:
		  set_outcome(TEST_EXITED);
		  child_exited = true;
	    break;
      case kTestRunnerInternalError:
        FailFromInternalError(read_fd());  // Does not return.
        break;
      default:
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
}

bool TestRunnerImpl::ProcessOutcome() {
	if (!spawned())
	    return false;
	  switch (outcome()) {
		case TEST_EXITED:
			// everything is fine
		break;
	  case TEST_DIED:
		GTEST_LOG_(FATAL) << "Child process died";
		break;
	  default:
	GTEST_LOG_(FATAL) << "Unexpected child process outcome";
	break;
	}
	return true;
}

// Called in child process only
void TestRunnerImpl::ReportTestPartResult(const TestPartResult& result) {
	GTEST_TEST_RUNNER_CHECK_(write_fd() != -1);
	
	string data;
	data.append("R"); // for test part result
	SerializeTestPartResult(result, &data);

	// TODO (dmeister) Use a safe write method
	posix::Write(write_fd(), data.data(), data.size());
}

# if GTEST_OS_WINDOWS
// WindowsTestRunner implements a test runner on Windows. Due to the
// specifics of starting new processes on Windows, death tests there are
// always threadsafe, and Google Test considers the
// --gtest_test_runner_style=fast setting to be equivalent to
// --gtest_test_runner_style=threadsafe there.
//
// A few implementation notes:  Like the Linux version, the Windows
// implementation uses pipes for child-to-parent communication. But due to
// the specifics of pipes on Windows, some extra steps are required:
//
// 1. The parent creates a communication pipe and stores handles to both
//    ends of it.
// 2. The parent starts the child and provides it with the information
//    necessary to acquire the handle to the write end of the pipe.
// 3. The child acquires the write end of the pipe and signals the parent
//    using a Windows event.
// 4. Now the parent can release the write end of the pipe on its side. If
//    this is done before step 3, the object's reference count goes down to
//    0 and it is destroyed, preventing the child from acquiring it. The
//    parent now has to release it, or read operations on the read end of
//    the pipe will not return when the child terminates.
// 5. The parent reads child's output through the pipe (outcome code and
//    any possible error messages) from the pipe, and its stderr and then
//    determines whether to fail the test.
//
// Note: to distinguish Win32 API calls from the local method and function
// calls, the former are explicitly resolved in the global namespace.
//
class WindowsTestRunner : public TestRunnerImpl {
 public:
  WindowsTestRunner() {}

  // All of these virtual functions are inherited from TestRunner.
  virtual int Wait();
  virtual TestRole AssumeRole();

 private:
  // Handle to the write end of the pipe to the child process.
  AutoHandle write_handle_;
  // Child process handle.
  AutoHandle child_handle_;
  // Event the child process uses to signal the parent that it has
  // acquired the handle to the write end of the pipe. After seeing this
  // event the parent can release its own handles to make sure its
  // ReadFile() calls return when the child terminates.
  AutoHandle event_handle_;
};

// Waits for the child in a test runner to exit, returning its exit
// status, or 0 if no child process exists.  As a side effect, sets the
// outcome data member.
int WindowsTestRunner::Wait() {
  if (!spawned())
    return 0;

  // Wait until the child either signals that it has acquired the write end
  // of the pipe or it dies.
  const HANDLE wait_handles[2] = { child_handle_.Get(), event_handle_.Get() };
  switch (::WaitForMultipleObjects(2,
                                   wait_handles,
                                   FALSE,  // Waits for any of the handles.
                                   INFINITE)) {
    case WAIT_OBJECT_0:
    case WAIT_OBJECT_0 + 1:
      break;
    default:
      GTEST_TeST_RUNNER_CHECK_(false);  // Should not get here.
  }

  // The child has acquired the write end of the pipe or exited.
  // We release the handle on our side and continue.
  write_handle_.Reset();
  event_handle_.Reset();

  ReadAndInterpretStatusByte();

  // Waits for the child process to exit if it haven't already. This
  // returns immediately if the child has already exited, regardless of
  // whether previous calls to WaitForMultipleObjects synchronized on this
  // handle or not.
  GTEST_DEATH_TEST_CHECK_(
      WAIT_OBJECT_0 == ::WaitForSingleObject(child_handle_.Get(),
                                             INFINITE));
  DWORD status_code;
  GTEST_DEATH_TEST_CHECK_(
      ::GetExitCodeProcess(child_handle_.Get(), &status_code) != FALSE);
  child_handle_.Reset();
  set_status(static_cast<int>(status_code));
  return status();
}

// The AssumeRole process for a Windows death test.  It creates a child
// process with the same executable as the current process to run the
// death test.  The child process is given the --gtest_filter and
// --gtest_internal_run_death_test flags such that it knows to run the
// current death test only.
TestRunner::Role WindowsTestRunner::AssumeRole() {
  const UnitTestImpl* const impl = GetUnitTestImpl();
  const InternalTestRunnerFlag* const flag =
      impl->internal_run_test_runner_flag();
  const TestInfo* const info = impl->current_test_info();

  if (flag != NULL) {
    // ParseInternalRunTestRunnerFlag() has performed all the necessary
    // processing.
    set_write_fd(flag->write_fd());
    return EXECUTE_TEST;
  }

  // WindowsTestRunner uses an anonymous pipe to communicate results of
  // a death test.
  SECURITY_ATTRIBUTES handles_are_inheritable = {
    sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
  HANDLE read_handle, write_handle;
  GTEST_DEATH_TEST_CHECK_(
      ::CreatePipe(&read_handle, &write_handle, &handles_are_inheritable,
                   0)  // Default buffer size.
      != FALSE);
  set_read_fd(::_open_osfhandle(reinterpret_cast<intptr_t>(read_handle),
                                O_RDONLY));
  write_handle_.Reset(write_handle);
  event_handle_.Reset(::CreateEvent(
      &handles_are_inheritable,
      TRUE,    // The event will automatically reset to non-signaled state.
      FALSE,   // The initial state is non-signalled.
      NULL));  // The even is unnamed.
  GTEST_DEATH_TEST_CHECK_(event_handle_.Get() != NULL);
  const String filter_flag = String::Format("--%s%s=%s.%s",
                                            GTEST_FLAG_PREFIX_, kFilterFlag,
                                            info->test_case_name(),
                                            info->name());
  const String internal_flag = String::Format(
    "--%s%s=%s|%d|%u|%Iu|%Iu",
      GTEST_FLAG_PREFIX_,
      kInternalTestRunnerFlag,
      file_, line_,
      static_cast<unsigned int>(::GetCurrentProcessId()),
      // size_t has the same with as pointers on both 32-bit and 64-bit
      // Windows platforms.
      // See http://msdn.microsoft.com/en-us/library/tcxf1dw6.aspx.
      reinterpret_cast<size_t>(write_handle),
      reinterpret_cast<size_t>(event_handle_.Get()));

  char executable_path[_MAX_PATH + 1];  // NOLINT
  GTEST_DEATH_TEST_CHECK_(
      _MAX_PATH + 1 != ::GetModuleFileNameA(NULL,
                                            executable_path,
                                            _MAX_PATH));

  String command_line = String::Format("%s %s \"%s\"",
                                       ::GetCommandLineA(),
                                       filter_flag.c_str(),
                                       internal_flag.c_str());

  CaptureStderr();
  // Flush the log buffers since the log streams are shared with the child.
  FlushInfoLog();

  // The child process will share the standard handles with the parent.
  STARTUPINFOA startup_info;
  memset(&startup_info, 0, sizeof(STARTUPINFO));
  startup_info.dwFlags = STARTF_USESTDHANDLES;
  startup_info.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
  startup_info.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
  startup_info.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION process_info;
  GTEST_DEATH_TEST_CHECK_(::CreateProcessA(
      executable_path,
      const_cast<char*>(command_line.c_str()),
      NULL,   // Retuned process handle is not inheritable.
      NULL,   // Retuned thread handle is not inheritable.
      TRUE,   // Child inherits all inheritable handles (for write_handle_).
      0x0,    // Default creation flags.
      NULL,   // Inherit the parent's environment.
      UnitTest::GetInstance()->original_working_dir(),
      &startup_info,
      &process_info) != FALSE);
  child_handle_.Reset(process_info.hProcess);
  ::CloseHandle(process_info.hThread);
  set_spawned(true);
  return OVERSEE_TEST;
}
# else  // We are not on Windows.

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
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(waitpid(child_pid_, &status_value, 0));
  set_status(status_value);
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
  int pipe_fd[2];
  GTEST_TEST_RUNNER_CHECK_(pipe(pipe_fd) != -1);

  const pid_t child_pid = fork();
  GTEST_TEST_RUNNER_CHECK_(child_pid != -1);
  set_child_pid(child_pid);
  if (child_pid == 0) {
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(pipe_fd[0]));
    set_write_fd(pipe_fd[1]);
    // Redirects all logging to stderr in the child process to prevent
    // concurrent writes to the log files.  We capture stderr in the parent
    // process and append the child process' output to a log.
    LogToStderr();
    // Event forwarding to the listeners of event listener API mush be shut
    // down in death test subprocesses.
    GetUnitTestImpl()->listeners()->SuppressEventForwarding();
    return EXECUTE_TEST;
  } else {
    GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(pipe_fd[1]));
    set_read_fd(pipe_fd[0]);
    set_spawned(true);
    return OVERSEE_TEST;
  }
}

void NoExecTestRunner::TearDown() {
	TestRunnerImpl::TearDown();
	// the work of the subprocess is done.
	exit(0);
}

// A concrete death test class that forks and re-executes the main
// program from the beginning, with command-line flags set that cause
// only this specific death test to be run.
class ExecTestRunner : public ForkingTestRunner {
 public:
  ExecTestRunner() {}

  virtual Role AssumeRole();
 private:
};


// A struct that encompasses the arguments to the child process of a
// threadsafe-style test runner process.
struct ExecTestRunnerArgs {
  char* const* argv;  // Command-line arguments for the child's call to exec
  int close_fd;       // File descriptor to close; the read end of a pipe
};

#  if !GTEST_OS_QNX
// The main function for a threadsafe-style death test child process.
// This function is called in a clone()-ed process and thus must avoid
// any potentially unsafe operations like malloc or libc functions.
static int ExecTestRunnerChildMain(void* child_arg) {
  ExecTestRunnerArgs* const args = static_cast<ExecTestRunnerArgs*>(child_arg);
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(args->close_fd));

  // We need to execute the test program in the same environment where
  // it was originally invoked.  Therefore we change to the original
  // working directory first.
  const char* const original_dir =
      UnitTest::GetInstance()->original_working_dir();
  // We can safely call chdir() as it's a direct system call.
  if (chdir(original_dir) != 0) {
    TestRunnerAbort(String::Format("chdir(\"%s\") failed: %s",
                                  original_dir,
                                  GetLastErrnoDescription().c_str()));
    return EXIT_FAILURE;
  }

  // We can safely call execve() as it's a direct system call.  We
  // cannot use execvp() as it's a libc function and thus potentially
  // unsafe.  Since execve() doesn't search the PATH, the user must
  // invoke the test program via a valid path that contains at least
  // one path separator.
  execve(args->argv[0], args->argv, GetEnviron());
  TestRunnerAbort(String::Format("execve(%s, ...) in %s failed: %s",
                                args->argv[0],
                                original_dir,
                                GetLastErrnoDescription().c_str()));
  return EXIT_FAILURE;
}
#  endif  // !GTEST_OS_QNX

// Spawns a child process with the same executable as the current process in
// a thread-safe manner and instructs it to run the death test.  The
// implementation uses fork(2) + exec.  On systems where clone(2) is
// available, it is used instead, being slightly more thread-safe.  On QNX,
// fork supports only single-threaded environments, so this function uses
// spawn(2) there instead.  The function dies with an error message if
// anything goes wrong.
static pid_t ExecTestRunnerSpawnChild(char* const* argv, int close_fd) {
  ExecTestRunnerArgs args = { argv, close_fd };
  pid_t child_pid = -1;

#  if GTEST_OS_QNX
  // Obtains the current directory and sets it to be closed in the child
  // process.
  const int cwd_fd = open(".", O_RDONLY);
  GTEST_TEST_RUNNER_CHECK_(cwd_fd != -1);
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(fcntl(cwd_fd, F_SETFD, FD_CLOEXEC));
  // We need to execute the test program in the same environment where
  // it was originally invoked.  Therefore we change to the original
  // working directory first.
  const char* const original_dir =
      UnitTest::GetInstance()->original_working_dir();
  // We can safely call chdir() as it's a direct system call.
  if (chdir(original_dir) != 0) {
    TestRunnerAbort(String::Format("chdir(\"%s\") failed: %s",
                                  original_dir,
                                  GetLastErrnoDescription().c_str()));
    return EXIT_FAILURE;
  }

  int fd_flags;
  // Set close_fd to be closed after spawn.
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(fd_flags = fcntl(close_fd, F_GETFD));
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(fcntl(close_fd, F_SETFD,
                                        fd_flags | FD_CLOEXEC));
  struct inheritance inherit = {0};
  // spawn is a system call.
  child_pid = spawn(args.argv[0], 0, NULL, &inherit, args.argv, GetEnviron());
  // Restores the current working directory.
  GTEST_TEST_RUNNER_CHECK_(fchdir(cwd_fd) != -1);
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(cwd_fd));

#  else   // GTEST_OS_QNX

#   if GTEST_HAS_CLONE
  const bool use_fork = GTEST_FLAG(death_test_use_fork);

  if (!use_fork) {
    static const bool stack_grows_down = StackGrowsDown();
    const size_t stack_size = getpagesize();
    // MMAP_ANONYMOUS is not defined on Mac, so we use MAP_ANON instead.
    void* const stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                             MAP_ANON | MAP_PRIVATE, -1, 0);
    GTEST_TEST_RUNNER_CHECK_(stack != MAP_FAILED);
    void* const stack_top =
        static_cast<char*>(stack) + (stack_grows_down ? stack_size : 0);

    child_pid = clone(&ExecTestRunnerChildMain, stack_top, SIGCHLD, &args);

    GTEST_TEST_RUNNER_CHECK_(munmap(stack, stack_size) != -1);
  }
#   else
  const bool use_fork = true;
#   endif  // GTEST_HAS_CLONE

  if (use_fork && (child_pid = fork()) == 0) {
      ExecTestRunnerChildMain(&args);
      _exit(0);
  }
#  endif  // GTEST_OS_QNX

  GTEST_TEST_RUNNER_CHECK_(child_pid != -1);
  return child_pid;
}

// The AssumeRole process for a fork-and-exec death test.  It re-executes the
// main program from the beginning, setting the --gtest_filter
// and --gtest_internal_run_death_test flags to cause only the current
// death test to be re-run.
TestRunner::Role ExecTestRunner::AssumeRole() {
  const UnitTestImpl* const impl = GetUnitTestImpl();
  const InternalTestRunnerFlag* const flag =
      impl->internal_test_runner_flag();
  const TestInfo* const info = impl->current_test_info();

  if (flag != NULL) {
    set_write_fd(flag->write_fd());
    return EXECUTE_TEST;
  }

  int pipe_fd[2];
  GTEST_TEST_RUNNER_CHECK_(pipe(pipe_fd) != -1);
  // Clear the close-on-exec flag on the write end of the pipe, lest
  // it be closed when the child process does an exec:
  GTEST_TEST_RUNNER_CHECK_(fcntl(pipe_fd[1], F_SETFD, 0) != -1);

  const String filter_flag =
      String::Format("--%s%s=%s.%s",
                     GTEST_FLAG_PREFIX_, kFilterFlag,
                     info->test_case_name(), info->name());
  const String internal_flag =
      String::Format("--%s%s=%d",
                     GTEST_FLAG_PREFIX_, kInternalTestRunnerFlag, pipe_fd[1]);

  Arguments args;
  args.AddArguments(GetArgvs());
  args.AddArgument(filter_flag.c_str());
  args.AddArgument(internal_flag.c_str());

  CaptureStderr();
  // See the comment in NoExecTestRunner::AssumeRole for why the next line
  // is necessary.
  FlushInfoLog();

  const pid_t child_pid = ExecTestRunnerSpawnChild(args.Argv(), pipe_fd[0]);
  GTEST_TEST_RUNNER_CHECK_SYSCALL_(close(pipe_fd[1]));
  set_child_pid(child_pid);
  set_read_fd(pipe_fd[0]);
  set_spawned(true);
  return OVERSEE_TEST;
}

# endif  // !GTEST_OS_WINDOWS

// Creates a concrete TestRunner-derived class that depends on the
// --gtest_test_runner_style flag, and sets the pointer pointed to
// by the "test" argument to its address.  If the test should be
// skipped, sets that pointer to NULL.  Returns true, unless the
// flag is set to an invalid value.
bool DefaultTestRunnerFactory::Create(TestRunner** test) {
# if GTEST_OS_WINDOWS

  if (GTEST_FLAG(test_runner_style) == "threadsafe" ||
      GTEST_FLAG(test_runner_style) == "fast") {
    *test = new WindowsTestRunner();
  }

# else

  if (GTEST_FLAG(test_runner_style) == "threadsafe") {
    *test = new ExecTestRunner();
  } else if (GTEST_FLAG(test_runner_style) == "fast") {
    *test = new NoExecTestRunner();
  }

# endif  // GTEST_OS_WINDOWS

  else {  // NOLINT - this is more readable than unbalanced brackets inside #if.	
	// TODO (dmeister): Error handling here
    return false;
  }

  return true;
}

# if GTEST_OS_WINDOWS
// Recreates the pipe and event handles from the provided parameters,
// signals the event, and returns a file descriptor wrapped around the pipe
// handle. This function is called in the child process only.
int GetTestRunnerStatusFileDescriptor(unsigned int parent_process_id,
                            size_t write_handle_as_size_t,
                            size_t event_handle_as_size_t) {
  AutoHandle parent_process_handle(::OpenProcess(PROCESS_DUP_HANDLE,
                                                   FALSE,  // Non-inheritable.
                                                   parent_process_id));
  if (parent_process_handle.Get() == INVALID_HANDLE_VALUE) {
    TestRunnerAbort(String::Format("Unable to open parent process %u",
                                  parent_process_id));
  }

  // TODO(vladl@google.com): Replace the following check with a
  // compile-time assertion when available.
  GTEST_CHECK_(sizeof(HANDLE) <= sizeof(size_t));

  const HANDLE write_handle =
      reinterpret_cast<HANDLE>(write_handle_as_size_t);
  HANDLE dup_write_handle;

  // The newly initialized handle is accessible only in in the parent
  // process. To obtain one accessible within the child, we need to use
  // DuplicateHandle.
  if (!::DuplicateHandle(parent_process_handle.Get(), write_handle,
                         ::GetCurrentProcess(), &dup_write_handle,
                         0x0,    // Requested privileges ignored since
                                 // DUPLICATE_SAME_ACCESS is used.
                         FALSE,  // Request non-inheritable handler.
                         DUPLICATE_SAME_ACCESS)) {
    TestRunnerAbort(String::Format(
        "Unable to duplicate the pipe handle %Iu from the parent process %u",
        write_handle_as_size_t, parent_process_id));
  }

  const HANDLE event_handle = reinterpret_cast<HANDLE>(event_handle_as_size_t);
  HANDLE dup_event_handle;

  if (!::DuplicateHandle(parent_process_handle.Get(), event_handle,
                         ::GetCurrentProcess(), &dup_event_handle,
                         0x0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS)) {
    TestRunnerAbort(String::Format(
        "Unable to duplicate the event handle %Iu from the parent process %u",
        event_handle_as_size_t, parent_process_id));
  }

  const int write_fd =
      ::_open_osfhandle(reinterpret_cast<intptr_t>(dup_write_handle), O_APPEND);
  if (write_fd == -1) {
    TestRunnerAbort(String::Format(
        "Unable to convert pipe handle %Iu to a file descriptor",
        write_handle_as_size_t));
  }

  // Signals the parent that the write end of the pipe has been acquired
  // so the parent can release its own write end.
  ::SetEvent(dup_event_handle);

  return write_fd;
}
# endif  // GTEST_OS_WINDOWS

// Returns a newly created InternalTestRunnerFlag object with fields
// initialized from the GTEST_FLAG(internal_test_runner) flag if
// the flag is specified; otherwise returns NULL.
InternalTestRunnerFlag* ParseInternalTestRunnerFlag() {
  if (GTEST_FLAG(internal_test_runner) == "") return NULL;

  // GTEST_HAS_DEATH_TEST implies that we have ::std::string, so we
  // can use it here.
  ::std::vector< ::std::string> fields;
  SplitString(GTEST_FLAG(internal_test_runner).c_str(), '|', &fields);
  int write_fd = -1;

# if GTEST_OS_WINDOWS

  unsigned int parent_process_id = 0;
  size_t write_handle_as_size_t = 0;
  size_t event_handle_as_size_t = 0;

  if (fields.size() != 4
      || !ParseNaturalNumber(fields[1], &parent_process_id)
      || !ParseNaturalNumber(fields[2], &write_handle_as_size_t)
      || !ParseNaturalNumber(fields[3], &event_handle_as_size_t)) {
    TestRunnerAbort(String::Format(
        "Bad --gtest_internal_test_runner flag: %s",
        GTEST_FLAG(internal_test_runner_test).c_str()));
  }
  write_fd = GetStatusFileDescriptor(parent_process_id,
                                     write_handle_as_size_t,
                                     event_handle_as_size_t);
# else

  if (fields.size() != 2
      || !ParseNaturalNumber(fields[1], &write_fd)) {
    TestRunnerAbort(String::Format(
        "Bad --gtest_internal_test_runner flag: %s",
        GTEST_FLAG(internal_test_runner).c_str()));
  }

# endif  // GTEST_OS_WINDOWS

  return new InternalTestRunnerFlag(write_fd);
}

void TestRunnerTestPartResultReporter::ReportTestPartResult(const TestPartResult& result) {
	test_runner_->ReportTestPartResult(result);
}


}  // namespace internal

}  // namespace testing
