// Copyright 2015 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "benchmark/benchmark.h"
#include "check.h"
#include "colorprint.h"
#include "commandlineflags.h"
#include "complexity.h"
#include "counter.h"
#include "internal_macros.h"
#include "string_util.h"
#include "timers.h"

namespace benchmark {

bool ConsoleReporter::ReportContext(const Context& context) {
  name_field_width_ = context.name_field_width;
  printed_header_ = false;
  prev_counters_.clear();

  PrintBasicContext(&GetErrorStream(), context);

#ifdef BENCHMARK_OS_WINDOWS
  if ((output_options_ & OO_Color) && &std::cout != &GetOutputStream()) {
    GetErrorStream()
        << "Color printing is only supported for stdout on windows."
           " Disabling color printing\n";
    output_options_ = static_cast<OutputOptions>(output_options_ & ~OO_Color);
  }
#endif

  return true;
}

void ConsoleReporter::PrintHeader(const Run& run) {
  std::string str =
      FormatString("%-*s %13s %15s %12s", static_cast<int>(name_field_width_),
                   "Benchmark", "Time", "CPU", "Iterations");
  if (!run.counters.empty()) {
    if (output_options_ & OO_Tabular) {
      for (auto const& c : run.counters) {
        str += FormatString(" %10s", c.first.c_str());
      }
    } else {
      str += " UserCounters...";
    }
  }
  std::string line = std::string(str.length(), '-');
  GetOutputStream() << line << "\n" << str << "\n" << line << "\n";
}

void ConsoleReporter::ReportRuns(const std::vector<Run>& reports) {
  for (const auto& run : reports) {
    // print the header:
    // --- if none was printed yet
    bool print_header = !printed_header_;
    // --- or if the format is tabular and this run
    //     has different fields from the prev header
    print_header |= (output_options_ & OO_Tabular) &&
                    (!internal::SameNames(run.counters, prev_counters_));
    if (print_header) {
      printed_header_ = true;
      prev_counters_ = run.counters;
      PrintHeader(run);
    }
    // As an alternative to printing the headers like this, we could sort
    // the benchmarks by header and then print. But this would require
    // waiting for the full results before printing, or printing twice.
    PrintRunData(run);
  }
}

static void IgnoreColorPrint(std::ostream& out, LogColor, const char* fmt,
                             ...) {
  va_list args;
  va_start(args, fmt);
  out << FormatString(fmt, args);
  va_end(args);
}

static std::string FormatTime(double time) {
  // Align decimal places...
  if (time < 1.0) {
    return FormatString("%10.3f", time);
  }
  if (time < 10.0) {
    return FormatString("%10.2f", time);
  }
  if (time < 100.0) {
    return FormatString("%10.1f", time);
  }
  return FormatString("%10.0f", time);
}

void ConsoleReporter::PrintRunData(const Run& result) {
  typedef void(PrinterFn)(std::ostream&, LogColor, const char*, ...);
  auto& Out = GetOutputStream();
  PrinterFn* printer = (output_options_ & OO_Color)
                           ? static_cast<PrinterFn*>(ColorPrintf)
                           : IgnoreColorPrint;
  auto name_color =
      (result.report_big_o || result.report_rms) ? COLOR_BLUE : COLOR_GREEN;
  printer(Out, name_color, "%-*s ", name_field_width_,
          result.benchmark_name().c_str());

  if (result.error_occurred) {
    printer(Out, COLOR_RED, "ERROR OCCURRED: \'%s\'",
            result.error_message.c_str());
    printer(Out, COLOR_DEFAULT, "\n");
    return;
  }

  const double real_time = result.GetAdjustedRealTime();
  const double cpu_time = result.GetAdjustedCPUTime();
  const std::string real_time_str = FormatTime(real_time);
  const std::string cpu_time_str = FormatTime(cpu_time);

  if (result.report_big_o) {
    std::string big_o = GetBigOString(result.complexity);
    printer(Out, COLOR_YELLOW, "%10.2f %-4s %10.2f %-4s ", real_time,
            big_o.c_str(), cpu_time, big_o.c_str());
  } else if (result.report_rms) {
    printer(Out, COLOR_YELLOW, "%10.0f %-4s %10.0f %-4s ", real_time * 100, "%",
            cpu_time * 100, "%");
  } else if (result.run_type != Run::RT_Aggregate ||
             result.aggregate_unit == StatisticUnit::kTime) {
    const char* timeLabel = GetTimeUnitString(result.time_unit);
    printer(Out, COLOR_YELLOW, "%s %-4s %s %-4s ", real_time_str.c_str(),
            timeLabel, cpu_time_str.c_str(), timeLabel);
  } else {
    assert(result.aggregate_unit == StatisticUnit::kPercentage);
    printer(Out, COLOR_YELLOW, "%10.2f %-4s %10.2f %-4s ",
            (100. * result.real_accumulated_time), "%",
            (100. * result.cpu_accumulated_time), "%");
  }

  if (!result.report_big_o && !result.report_rms) {
    printer(Out, COLOR_CYAN, "%10lld", result.iterations);
  }

  for (auto& c : result.counters) {
    const std::size_t cNameLen =
        std::max(std::string::size_type(10), c.first.length());
    std::string s;
    const char* unit = "";
    if (result.run_type == Run::RT_Aggregate &&
        result.aggregate_unit == StatisticUnit::kPercentage) {
      s = StrFormat("%.2f", 100. * c.second.value);
      unit = "%";
    } else {
      s = HumanReadableNumber(c.second.value, c.second.oneK);
      if (c.second.flags & Counter::kIsRate)
        unit = (c.second.flags & Counter::kInvert) ? "s" : "/s";
    }
    if (output_options_ & OO_Tabular) {
      printer(Out, COLOR_DEFAULT, " %*s%s", cNameLen - strlen(unit), s.c_str(),
              unit);
    } else {
      printer(Out, COLOR_DEFAULT, " %s=%s%s", c.first.c_str(), s.c_str(), unit);
    }
  }

  if (!result.report_label.empty()) {
    printer(Out, COLOR_DEFAULT, " %s", result.report_label.c_str());
  }

  printer(Out, COLOR_DEFAULT, "\n");
}

}  // end namespace benchmark
