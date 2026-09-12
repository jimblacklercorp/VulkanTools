/* Copyright (C) 2026 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "device_memory_report_perfetto.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

void InitializeDeviceMemoryReportPerfetto() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kSystemBackend;
        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
    });
}

perfetto::CounterTrack GetCounterTrack(std::string_view name) {
    InitializeDeviceMemoryReportPerfetto();

    static std::mutex tracks_mutex;
    static std::unordered_map<std::string_view, perfetto::CounterTrack> tracks;
    static std::unordered_set<std::string> track_names;
    std::lock_guard<std::mutex> lock(tracks_mutex);

    auto it = tracks.find(name);
    if (it != tracks.end()) {
        return it->second;
    }

    auto name_it = track_names.insert(std::string(name)).first;
    perfetto::CounterTrack track = perfetto::CounterTrack(perfetto::DynamicString(name_it->c_str()))
        .set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES);
    tracks.emplace(*name_it, track);
    return track;
}
