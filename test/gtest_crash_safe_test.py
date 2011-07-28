#!/usr/bin/env python
#
# Copyright 2008, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Unit test for the crash-safe test execution."""

__author__ = "dirkmeister@acm.org (Dirk Meister)"

import os
import re
import gtest_test_utils

COMMAND = gtest_test_utils.GetTestExecutablePath('gtest_crash_safe_test_')

tests_to_die = [
  "TestForCrashSafeTesting.Segfault",
  "TestForCrashSafeTesting.Mathfault",
  "TestForCrashSafeTesting.StaticMemberFunction",
  "TestForCrashSafeTesting.MemberFunction",
  "TestForCrashSafeTesting.DieInChangedDir",
  "TestForCrashSafeTesting.MethodOfAnotherClass",
  "TestForCrashSafeTesting.GlobalFunction",
  "TestForCrashSafeTestingSetUpDie.DoesNothing",
  "TestForCrashSafeTestingTearDownDie.DoesNothing"
  ]

class GTestCrashSafeTest(gtest_test_utils.TestCase):
  """Unit test for Google Test's crash safe test execution functionality."""
  
  def VerifyCrashes(self, output):
    for test_to_die in tests_to_die:
      self.assertTrue(re.search("\[  FAILED  \] %s" % test_to_die, output))
      self.assertTrue(re.search("\[  FAILED  \] %s \(\d* ms\)" % test_to_die, output))
      self.assertTrue(re.search("Test process died while executing %s" % test_to_die, output))
                
  def testCrashSafe(self):
      command = [COMMAND]
      p = gtest_test_utils.Subprocess(command)
      self.assert_(p.exited)
      self.assertEquals(1, p.exit_code)
      
      if p.output.find("Crash safe test execution is not available. Test skipped") >= 0:
        # Test skipped
        return
      
      self.VerifyCrashes(p.output)

if __name__ == "__main__":
  gtest_test_utils.Main()
