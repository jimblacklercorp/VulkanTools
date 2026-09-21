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

#include "debug_marker_perfetto.h"
#include "object_names/vulkan_object_names_perfetto.h"

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace layersvt {

// Binds the shared object name store to this layer's track event data source.
void EmitVulkanObjectName(const VulkanObjectName& object_name) {
    WriteVulkanObjectNamePacket<perfetto::TrackEvent>(object_name);
}

}  // namespace layersvt

class MarkerSessionObserver : public perfetto::TrackEventSessionObserver {
 public:
  void OnStart(const perfetto::DataSourceBase::StartArgs&) override {
    // Replay the names of objects that were named before this session started.
    layersvt::VulkanObjectNames::Get().EmitAll();
  }
};

static MarkerSessionObserver g_marker_session_observer;

void InitializeDebugMarkerPerfetto() {
    perfetto::TracingInitArgs args;
    args.backends = perfetto::kSystemBackend;

    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();
    perfetto::TrackEvent::AddSessionObserver(&g_marker_session_observer);
}
