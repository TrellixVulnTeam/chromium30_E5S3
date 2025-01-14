# Copyright (c) 2011 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Top-level presubmit script for commit-queue.

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts for
details on the presubmit API built into gcl.
"""


def CommonChecks(input_api, output_api):
  import sys
  def join(*args):
    return input_api.os_path.join(input_api.PresubmitLocalPath(), *args)

  output = []

  sys_path_backup = sys.path
  try:
    # Note that this won't work on the commit queue.
    sys.path = [join('..', 'commit-queue-internal')] + sys.path
    black_list = list(input_api.DEFAULT_BLACK_LIST) + [
        r'^workdir/.*',
        r'^tests/.+',
    ]
    output.extend(input_api.canned_checks.RunPylint(
        input_api, output_api, black_list=black_list))

    sys.path = [join('tests')] + sys.path
    black_list = list(input_api.DEFAULT_BLACK_LIST) + [
        r'^workdir/.*',
    ]
    white_list = [ r'tests/.+\.py$' ]
    output.extend(input_api.canned_checks.RunPylint(
        input_api, output_api, black_list=black_list, white_list=white_list))
  finally:
    sys.path = sys_path_backup

  output.extend(input_api.canned_checks.RunUnitTestsInDirectory(
    input_api, output_api, 'tests', whitelist=[r'.*_test\.py$']))
  return output


def CheckChangeOnUpload(input_api, output_api):
  return CommonChecks(input_api, output_api)


def CheckChangeOnCommit(input_api, output_api):
  return CommonChecks(input_api, output_api)
