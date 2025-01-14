# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""ActiveMaster definition."""

from config_bootstrap import Master

class ChromiumFlaky(Master.Master1):
  project_name = 'Chromium Flaky'
  master_port = 8022
  slave_port = 8122
  master_port_alt = 8222
