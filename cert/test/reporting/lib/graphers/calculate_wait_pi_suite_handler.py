#
# Copyright 2020 The Android Open Source Project
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
"""A grapher for the calculate pi & wait operation.
"""

from math import ceil
from typing import Callable, Dict, List

import matplotlib.pyplot as plt
from matplotlib.ticker import FormatStrFormatter
import numpy as np

from lib.common import nanoseconds_to_seconds
from lib.graphers.suite_handler import SuiteHandler, SuiteSummarizer
from lib.report import Datum, SummaryContext, Suite
import lib.format_items as fmt


class ThreadChartData:
    """From the original report data, instances of this class segregate and
    enrich a slice corresponding to a specific Thread.
    This is because the suite handler below produces charts in a per-thread
    basis."""

    def __init__(self, normalize_fn: Callable[[int], float]):
        super().__init__()

        self.__normalize = normalize_fn

        self.x_timestamps = []
        self.x_widths = []
        self.y_iterations = []
        self.total_iterations = 0

    def append(self, datum: Datum) -> type(None):
        """Given a thread 'calculate Pi' iteration report, adapts data and
        appends it to the graph that corresponds to that thread"""
        t_0 = self.__normalize(datum.get_custom_field('t0'))
        t_1 = self.__normalize(datum.get_custom_field('t1'))

        self.x_timestamps.append(t_0)
        self.x_widths.append(t_1 - t_0)
        iterations = datum.get_custom_field('iterations')
        self.y_iterations.append(iterations)
        self.total_iterations += iterations


class TemperatureChartData:
    """Gets temperature-related data over time, to be graphed alongside the
    CPU-related one."""

    def __init__(self, normalize_fn: Callable[[int], float]):
        super().__init__()

        self.__normalize = normalize_fn

        self.x_timestamps = []
        self.y_temperatures = []

    def append(self, datum: Datum) -> type(None):
        """Enlists the pair (timestamp, temperature) to create the
        temperature-over-time chart."""
        self.x_timestamps.append(self.__normalize(datum.timestamp))
        self.y_temperatures.append(
            datum.get_custom_field_numeric(
                'temperature_info.max_cpu_temperature') / 1000)


class CalculateWaitPiSummarizer(SuiteSummarizer):
    """Suite summarizer for the calculate pi & wait operation."""
    @classmethod
    def default_handler(cls) -> SuiteHandler:
        return CalculateWaitPiSuiteHandler


class CalculateWaitPiSuiteHandler(SuiteHandler):
    """Grapher for the calculate pi & wait operation."""

    def __init__(self, suite: Suite):
        super().__init__(suite)

        self.__data_by_thread: Dict[int, ThreadChartData] = {}
        self.__temperature_data = None
        self.__max_iterations = 0
        self.__max_timestamp = 0
        self.__normalize = None

        self.__classify_data_by_chart()

    @classmethod
    def can_handle_datum(cls, datum: Datum):
        return "WaitForPI" in datum.suite_id

    def __classify_data_by_chart(self) -> type(None):
        """Visits all reported data to enrich some details for the graph."""
        is_first_datum = True
        first_timestamp = 0

        for datum in self.data:
            if is_first_datum:
                self.__runtime_params = datum.custom
                first_timestamp = datum.timestamp
                self.__normalize = lambda t: \
                    nanoseconds_to_seconds(t - first_timestamp)
                is_first_datum = False
            else:
                self.__classify_datum_by_chart(datum)

    def __classify_datum_by_chart(self, datum: Datum) -> type(None):
        """Given a single report line (aka, Datum), normalize absolute
        timestamps as relative seconds. It also gathers maximums x and y for
        the chart axes."""
        self.__max_timestamp = self.__normalize(datum.timestamp)
        if datum.operation_id == 'CalculateWaitPIOperation':
            self.__classify_thread_datum(datum)
            self.__max_iterations = max(self.__max_iterations,
                                        datum.get_custom_field('iterations'))
        elif datum.operation_id == 'MonitorOperation':
            self.__classify_temperature_datum(datum)

    def __classify_thread_datum(self, datum: Datum) -> type(None):
        """Appends the datum to its corresponding thread bucket."""
        thread_id = datum.thread_id
        thread_data = self.__data_by_thread.get(thread_id)
        if thread_data is None:
            thread_data = ThreadChartData(self.__normalize)
            self.__data_by_thread[thread_id] = thread_data
        thread_data.append(datum)

    def __classify_temperature_datum(self, datum: Datum) -> type(None):
        """Appends the datum to the temperature bucket."""
        if self.__temperature_data is None:
            self.__temperature_data = \
                TemperatureChartData(self.__normalize)
        self.__temperature_data.append(datum)

    def render(self, ctx: SummaryContext) -> List[fmt.Item]:

        def graph():
            rows, cols = ceil((len(self.__data_by_thread) + 1) / 2), 2
            fig, axes = plt.subplots(rows, cols)
            params = self.__runtime_params
            fig.suptitle(
                f"{params['wait_method']} "
                f"({'with' if params['affinity'] else 'without'} affinity)")
            fig.subplots_adjust(hspace=0.4)
            charts = axes.flatten()
            for index, chart_data in enumerate(self.__data_by_thread.values()):
                self.__render_thread_chart(index, charts[index], chart_data)

            self.__render_temperature_chart(charts[len(self.__data_by_thread)],
                                            self.__temperature_data)
            self.__render_summary_chart(charts[len(charts) - 1],
                                        self.__data_by_thread.values())

        image_path = self.plot(ctx, graph)
        image = fmt.Image(image_path, self.device())
        return [image]

    def __render_thread_chart(self, index, chart, chart_data) -> type(None):
        """Format axis, their limits in order to produce a bar chart for the
        thread iteration performance."""
        chart_data.x_timestamps = np.array(chart_data.x_timestamps)
        chart_data.x_widths = np.array(chart_data.x_widths)
        chart_data.y_iterations = np.array(chart_data.y_iterations)

        chart.set_title(f"Thread #{index}")
        chart.set_ylim(0, self.__max_iterations)
        chart.set_xlim(0, self.__max_timestamp)
        chart.get_xaxis().set_visible(False)
        chart.get_yaxis().set_visible(False)

        chart.bar(chart_data.x_timestamps,
                  chart_data.y_iterations,
                  chart_data.x_widths,
                  align='edge')
        chart.text(self.__max_timestamp / 2,
                   self.__max_iterations / 2,
                   f'{chart_data.total_iterations/1e6:,.0f}M iterations',
                   size=10,
                   ha="center",
                   va="center")

    def __render_temperature_chart(self, chart, chart_data) -> type(None):
        """Plots the device temperature curve over time."""
        chart_data.x_timestamps = np.array(chart_data.x_timestamps)
        chart_data.y_temperatures = np.array(chart_data.y_temperatures)

        chart.set_title('Temperature')
        chart.set_xlim(0, self.__max_timestamp)
        chart.get_xaxis().set_major_formatter(FormatStrFormatter('%ds'))
        chart.get_yaxis().set_major_formatter(FormatStrFormatter('%d°C'))

        chart.plot(chart_data.x_timestamps, chart_data.y_temperatures)

    def __render_summary_chart(self, chart, threads_data) -> type(None):
        """Prints the total number of iterations for the wait method and
        affinity."""
        grand_total_iterations = 0

        for thread_data in threads_data:
            grand_total_iterations += thread_data.total_iterations

        chart.set_title('Total Iterations')
        chart.get_xaxis().set_visible(False)
        chart.get_yaxis().set_visible(False)
        chart.text(0.5,
                   0.5,
                   f'{grand_total_iterations/1e6:,.0f}M iterations',
                   size=10,
                   ha="center",
                   va="center")
