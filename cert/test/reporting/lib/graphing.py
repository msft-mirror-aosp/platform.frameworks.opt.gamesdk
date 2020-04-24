#
# Copyright 2019 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Helper functions to load report JSON documents and generate
reports.
"""

import json
import platform
from typing import Dict, List

import matplotlib
import matplotlib.pyplot as plt

from lib.report import BuildInfo, Datum, Suite
from lib.graphers.loader import create_suite_handler

#-------------------------------------------------------------------------------

if platform.system() == "Linux":
    matplotlib.use('gtk3cairo')

SMALL_SIZE = 5
MEDIUM_SIZE = 7
BIGGER_SIZE = 10

plt.rc('font', size=SMALL_SIZE)  # controls default text sizes
plt.rc('axes', titlesize=SMALL_SIZE)  # fontsize of the axes title
plt.rc('axes', labelsize=MEDIUM_SIZE)  # fontsize of the x and y labels
plt.rc('xtick', labelsize=SMALL_SIZE)  # fontsize of the tick labels
plt.rc('ytick', labelsize=SMALL_SIZE)  # fontsize of the tick labels
plt.rc('legend', fontsize=SMALL_SIZE)  # legend fontsize
plt.rc('figure', titlesize=BIGGER_SIZE)  # fontsize of the figure title

# ------------------------------------------------------------------------------


class UnsupportedReportFormatError(Exception):
    """Exception raised when a requesting an unsupported format for
    saving the report.
    """


#-------------------------------------------------------------------------------


def load_suites(report_file) -> List[Suite]:
    """Load and configure suites from a given report JSON file
    Args:
        report_file: Report JSON file
    Returns:
        list of Suite instances for which a suitable SuiteHandler was found
    """
    build: BuildInfo = None
    suite_data: Dict[str, List[Datum]] = {}
    suites: List[Suite] = []

    with open(report_file) as file:
        for i, line in enumerate(file):
            try:
                line_dict = json.loads(line)
                # first line is the build info, every other is a report datum
                if i == 0:
                    build = BuildInfo.from_json(line_dict)
                else:
                    datum = Datum.from_json(line_dict)
                    suite_data.setdefault(datum.suite_id, []).append(datum)
            except json.decoder.JSONDecodeError as ex:
                print(f'Report file {report_file}, line {i}: skipping due to '
                      f'a JSON parsing error "{ex}":\n{line}')

    for suite_name in suite_data:
        data = suite_data[suite_name]
        suite = Suite(suite_name, build, data, report_file)

        suite.handler = create_suite_handler(suite)
        if suite.name and not suite.handler:
            print(
                f"[INFO]\tFound no handler for suite_id" \
                f" \"{suite.name}\" in \"{report_file}\""
            )

        suites.append(suite)

    return suites
