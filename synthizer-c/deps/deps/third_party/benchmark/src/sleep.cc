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

#include "sleep.h"

#include <cerrno>
#include <cstdlib>
#include <ctime>

#include "internal_macros.h"

#ifdef BENCHMARK_OS_WINDOWS
#include <windows.h>
#endif

#ifdef BENCHMARK_OS_ZOS
#include <unistd.h>
#endif

namespace benchmark {
#ifdef BENCHMARK_OS_WINDOWS
// Window's Sleep takes milliseconds argument.
void SleepForMilliseconds(int milliseconds) { Sleep(milliseconds); }
void SleepForSeconds(double seconds) {
  SleepForMilliseconds(static_cast<int>(kNumMillisPerSecond * seconds));
}
#else  // BENCHMARK_OS_WINDOWS
void SleepForMicroseconds(int microseconds) {
#ifdef BENCHMARK_OS_ZOS
  // z/OS does not support nanosleep. Instead call sleep() and then usleep() to
  // sleep for the remaining microseconds because usleep() will fail if its
  // argument is greater than 1000000.
  div_t sleepTime = div(microseconds, kNumMicrosPerSecond);
  int seconds = sleepTime.quot;
  while (seconds != 0) seconds = sleep(seconds);
  while (usleep(sleepTime.rem) == -1 && errno == EINTR)
    ;
#else
  struct timespec sleep_time;
  sleep_time.tv_sec = microseconds / kNumMicrosPerSecond;
  sleep_time.tv_nsec = (microseconds % kNumMicrosPerSecond) * kNumNanosPerMicro;
  while (nanosleep(&sleep_time, &sleep_time) != 0 && errno == EINTR)
    ;  // Ignore signals and wait for the full interval to elapse.
#endif
}

void SleepForMilliseconds(int milliseconds) {
  SleepForMicroseconds(milliseconds * kNumMicrosPerMilli);
}

void SleepForSeconds(double seconds) {
  SleepForMicroseconds(static_cast<int>(seconds * kNumMicrosPerSecond));
}
#endif  // BENCHMARK_OS_WINDOWS
}  // end namespace benchmark
