// Copyright 2021 Google Inc. All rights reserved.
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

#ifndef BENCHMARK_PERF_COUNTERS_H
#define BENCHMARK_PERF_COUNTERS_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "benchmark/benchmark.h"
#include "check.h"
#include "log.h"
#include "mutex.h"

#ifndef BENCHMARK_OS_WINDOWS
#include <unistd.h>
#endif

#if defined(_MSC_VER)
#pragma warning(push)
// C4251: <symbol> needs to have dll-interface to be used by clients of class
#pragma warning(disable : 4251)
#endif

namespace benchmark {
namespace internal {

// Typically, we can only read a small number of counters. There is also a
// padding preceding counter values, when reading multiple counters with one
// syscall (which is desirable). PerfCounterValues abstracts these details.
// The implementation ensures the storage is inlined, and allows 0-based
// indexing into the counter values.
// The object is used in conjunction with a PerfCounters object, by passing it
// to Snapshot(). The values are populated such that
// perfCounters->names()[i]'s value is obtained at position i (as given by
// operator[]) of this object.
class PerfCounterValues {
 public:
  explicit PerfCounterValues(size_t nr_counters) : nr_counters_(nr_counters) {
    BM_CHECK_LE(nr_counters_, kMaxCounters);
  }

  uint64_t operator[](size_t pos) const { return values_[kPadding + pos]; }

  static constexpr size_t kMaxCounters = 3;

 private:
  friend class PerfCounters;
  // Get the byte buffer in which perf counters can be captured.
  // This is used by PerfCounters::Read
  std::pair<char*, size_t> get_data_buffer() {
    return {reinterpret_cast<char*>(values_.data()),
            sizeof(uint64_t) * (kPadding + nr_counters_)};
  }

  static constexpr size_t kPadding = 1;
  std::array<uint64_t, kPadding + kMaxCounters> values_;
  const size_t nr_counters_;
};

// Collect PMU counters. The object, once constructed, is ready to be used by
// calling read(). PMU counter collection is enabled from the time create() is
// called, to obtain the object, until the object's destructor is called.
class BENCHMARK_EXPORT PerfCounters final {
 public:
  // True iff this platform supports performance counters.
  static const bool kSupported;

  bool IsValid() const { return !counter_names_.empty(); }
  static PerfCounters NoCounters() { return PerfCounters(); }

  ~PerfCounters() { CloseCounters(); }
  PerfCounters(PerfCounters&&) = default;
  PerfCounters(const PerfCounters&) = delete;
  PerfCounters& operator=(PerfCounters&&) noexcept;
  PerfCounters& operator=(const PerfCounters&) = delete;

  // Platform-specific implementations may choose to do some library
  // initialization here.
  static bool Initialize();

  // Return a PerfCounters object ready to read the counters with the names
  // specified. The values are user-mode only. The counter name format is
  // implementation and OS specific.
  // TODO: once we move to C++-17, this should be a std::optional, and then the
  // IsValid() boolean can be dropped.
  static PerfCounters Create(const std::vector<std::string>& counter_names);

  // Take a snapshot of the current value of the counters into the provided
  // valid PerfCounterValues storage. The values are populated such that:
  // names()[i]'s value is (*values)[i]
  BENCHMARK_ALWAYS_INLINE bool Snapshot(PerfCounterValues* values) const {
#ifndef BENCHMARK_OS_WINDOWS
    assert(values != nullptr);
    assert(IsValid());
    auto buffer = values->get_data_buffer();
    auto read_bytes = ::read(counter_ids_[0], buffer.first, buffer.second);
    return static_cast<size_t>(read_bytes) == buffer.second;
#else
    (void)values;
    return false;
#endif
  }

  const std::vector<std::string>& names() const { return counter_names_; }
  size_t num_counters() const { return counter_names_.size(); }

 private:
  PerfCounters(const std::vector<std::string>& counter_names,
               std::vector<int>&& counter_ids)
      : counter_ids_(std::move(counter_ids)), counter_names_(counter_names) {}
  PerfCounters() = default;

  void CloseCounters() const;

  std::vector<int> counter_ids_;
  std::vector<std::string> counter_names_;
};

// Typical usage of the above primitives.
class BENCHMARK_EXPORT PerfCountersMeasurement final {
 public:
  PerfCountersMeasurement(const std::vector<std::string>& counter_names);
  ~PerfCountersMeasurement();

  // The only way to get to `counters_` is after ctor-ing a
  // `PerfCountersMeasurement`, which means that `counters_`'s state is, here,
  // decided (either invalid or valid) and won't change again even if a ctor is
  // concurrently running with this. This is preferring efficiency to
  // maintainability, because the address of the static can be known at compile
  // time.
  bool IsValid() const {
    MutexLock l(mutex_);
    return counters_.IsValid();
  }

  BENCHMARK_ALWAYS_INLINE void Start() {
    assert(IsValid());
    MutexLock l(mutex_);
    // Tell the compiler to not move instructions above/below where we take
    // the snapshot.
    ClobberMemory();
    valid_read_ &= counters_.Snapshot(&start_values_);
    ClobberMemory();
  }

  BENCHMARK_ALWAYS_INLINE bool Stop(
      std::vector<std::pair<std::string, double>>& measurements) {
    assert(IsValid());
    MutexLock l(mutex_);
    // Tell the compiler to not move instructions above/below where we take
    // the snapshot.
    ClobberMemory();
    valid_read_ &= counters_.Snapshot(&end_values_);
    ClobberMemory();

    for (size_t i = 0; i < counters_.names().size(); ++i) {
      double measurement = static_cast<double>(end_values_[i]) -
                           static_cast<double>(start_values_[i]);
      measurements.push_back({counters_.names()[i], measurement});
    }

    return valid_read_;
  }

 private:
  static Mutex mutex_;
  GUARDED_BY(mutex_) static int ref_count_;
  GUARDED_BY(mutex_) static PerfCounters counters_;
  bool valid_read_ = true;
  PerfCounterValues start_values_;
  PerfCounterValues end_values_;
};

BENCHMARK_UNUSED static bool perf_init_anchor = PerfCounters::Initialize();

}  // namespace internal
}  // namespace benchmark

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif  // BENCHMARK_PERF_COUNTERS_H
