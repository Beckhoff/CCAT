#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (C) Beckhoff Automation GmbH & Co. KG

set -e
set -u

. "$(shlib.sh get-path)/log.sh"

script_path="$(cd "$(dirname "${0}")" && pwd)"
readonly script_path

jitlab shallow-clone test_stage "${BHF_CI_TEST_STAGE_GIT_REF:-master}"
