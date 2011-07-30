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

"""Unit test for the gtest_listener_test module."""

__author__ = "dirkmeister@acm.org (Dirk Meister)"

import os
import gtest_test_utils

COMMAND = gtest_test_utils.GetTestExecutablePath('gtest_listener_test_')

expected_events = [
  "1st.OnTestProgramStart",
  "2nd.OnTestProgramStart",
  "1st.OnTestIterationStart(0)",
  "2nd.OnTestIterationStart(0)",
  "1st.OnEnvironmentsSetUpStart",
  "2nd.OnEnvironmentsSetUpStart",
  "Environment::SetUp",
  "2nd.OnEnvironmentsSetUpEnd",
  "1st.OnEnvironmentsSetUpEnd",
  "1st.OnTestCaseStart",
  "2nd.OnTestCaseStart",
  "ListenerTest::SetUpTestCase",
  "1st.OnTestStart",
  "2nd.OnTestStart",
  "ListenerTest::SetUp",
  "ListenerTest::* Test Body",
  "1st.OnTestPartResult",
  "2nd.OnTestPartResult",
  "ListenerTest::TearDown",
  "2nd.OnTestEnd",
  "1st.OnTestEnd",
  "1st.OnTestStart",
  "2nd.OnTestStart",
  "ListenerTest::SetUp",
  "ListenerTest::* Test Body",
  "1st.OnTestPartResult",
  "2nd.OnTestPartResult",
  "ListenerTest::TearDown",
  "2nd.OnTestEnd",
  "1st.OnTestEnd",
  "ListenerTest::TearDownTestCase",
  "2nd.OnTestCaseEnd",
  "1st.OnTestCaseEnd",
  "1st.OnEnvironmentsTearDownStart",
  "2nd.OnEnvironmentsTearDownStart",
  "Environment::TearDown",
  "2nd.OnEnvironmentsTearDownEnd",
  "1st.OnEnvironmentsTearDownEnd",
  "2nd.OnTestIterationEnd(0)",
  "1st.OnTestIterationEnd(0)",
  "1st.OnTestIterationStart(1)",
  "2nd.OnTestIterationStart(1)",
  "1st.OnEnvironmentsSetUpStart",
  "2nd.OnEnvironmentsSetUpStart",
  "Environment::SetUp",
  "2nd.OnEnvironmentsSetUpEnd",
  "1st.OnEnvironmentsSetUpEnd",
  "1st.OnTestCaseStart",
  "2nd.OnTestCaseStart",
  "ListenerTest::SetUpTestCase",
  "1st.OnTestStart",
  "2nd.OnTestStart",
  "ListenerTest::SetUp",
  "ListenerTest::* Test Body",
  "1st.OnTestPartResult",
  "2nd.OnTestPartResult",
  "ListenerTest::TearDown",
  "2nd.OnTestEnd",
  "1st.OnTestEnd",
  "1st.OnTestStart",
  "2nd.OnTestStart",
  "ListenerTest::SetUp",
  "ListenerTest::* Test Body",
  "1st.OnTestPartResult",
  "2nd.OnTestPartResult",
  "ListenerTest::TearDown",
  "2nd.OnTestEnd",
  "1st.OnTestEnd",
  "ListenerTest::TearDownTestCase",
  "2nd.OnTestCaseEnd",
  "1st.OnTestCaseEnd",
  "1st.OnEnvironmentsTearDownStart",
  "2nd.OnEnvironmentsTearDownStart",
  "Environment::TearDown",
  "2nd.OnEnvironmentsTearDownEnd",
  "1st.OnEnvironmentsTearDownEnd",
  "2nd.OnTestIterationEnd(1)",
  "1st.OnTestIterationEnd(1)",
  "2nd.OnTestProgramEnd",
  "1st.OnTestProgramEnd"];

class GTestListenerTest(gtest_test_utils.TestCase):
  """Unit test for Google Test's listener functionality."""

  def ExtractListenerEvents(self, output):
    """ Extract the listenver events written by the test's test bodies
        from the process output
    """
    return [line[2] for line in 
      filter(lambda line: line[1], (line.partition("Event: ") for line in 
        output.split("\n")))]
      
  def VerifyResults(self, actual_events):    
    for (expected_event, actual_event) in zip(expected_events, actual_events):
        self.assertEqual(expected_event, actual_event)    
    self.assertEqual(len(expected_events), len(actual_events))
    
  def testListener(self):
    command = [COMMAND]
    p = gtest_test_utils.Subprocess(command)
    self.assert_(p.exited)
    self.assertEquals(0, p.exit_code)
      
    actual_events = self.ExtractListenerEvents(p.output)
    self.VerifyResults(actual_events);

if __name__ == "__main__":
  gtest_test_utils.Main()
