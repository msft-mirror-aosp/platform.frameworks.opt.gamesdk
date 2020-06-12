/*
 * Copyright 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>

#include "tuningfork/tuningfork.h"
#include "proto/protobuf_util.h"
#include "settings.h"

namespace tuningfork {

// Load default fidelity params from either the saved file or the file in
//  settings.default_fidelity_parameters_filename, then start the download thread.
TuningFork_ErrorCode GetDefaultsFromAPKAndDownloadFPs(
    const Settings& settings);

// Kill all the threads the GetDefaults... may have started.
TuningFork_ErrorCode KillDownloadThreads();

// Read a fidelity parameter file from assets/tuningfork/<filename> in the APK.
TuningFork_ErrorCode FindFidelityParamsInApk(const std::string& filename,
                                    ProtobufSerialization& fp);

} // namespace tuningfork