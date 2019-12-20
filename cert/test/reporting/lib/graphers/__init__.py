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

from .affinity_test_suite_handler import AffinityTestSuiteHandler
from .file_performance_suite_handler import FilePerformanceSuiteHandler
from .half_float_precision import HalfFloatPrecisionSuiteHandler
from .memory_access_suite_handler import MemoryAccessSuiteHandler
from .memory_allocation_suite_handler import MemoryAllocationSuiteHandler
from .marching_cubes_suite_handler import MarchingCubesSuiteHandler
from .mprotect_suite_handler import MProtectSuiteHandler
"""
List containing all enabled handlers to render charts.
"""
HANDLERS = [
    AffinityTestSuiteHandler, FilePerformanceSuiteHandler,
    HalfFloatPrecisionSuiteHandler, MemoryAccessSuiteHandler,
    MemoryAllocationSuiteHandler, MarchingCubesSuiteHandler,
    MProtectSuiteHandler
]
