/*
 * Copyright 2019 The Android Open Source Project
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
#include <ancer/BaseOperation.hpp>

#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sched.h>
#include <thread>
#include <unistd.h>

#include <ancer/System.hpp>
#include <ancer/util/Basics.hpp>
#include <ancer/util/Bitmath.hpp>
#include <ancer/util/Json.hpp>
#include <ancer/util/Time.hpp>

using namespace ancer;
using Bytes = bitmath::Bytes;


//==============================================================================

namespace {
    constexpr Log::Tag TAG{"IOPerformanceOperation"};
}

//==============================================================================
// Config & Datum

namespace {
    enum class FileSetup {
        kBaseApk, kSplitApk, kObb, kCreatedFile
    };
    constexpr const char *kFileSetupNames[] = {
            "Base APK", "Split APK", "OBB", "Created File"
    };

    enum class ThreadSetup {
        kOneCore, kAllCores, kBigCores, kLittleCores
    };
    constexpr const char *kThreadSetupNames[] = {
            "Single Core", "All Cores", "Big Cores", "Little Cores"
    };
    constexpr auto ToAffinity(ThreadSetup setup) {
        switch (setup) {
            case ThreadSetup::kBigCores: return ThreadAffinity::kBigCore;
            case ThreadSetup::kLittleCores: return ThreadAffinity::kLittleCore;
            case ThreadSetup::kAllCores:
            case ThreadSetup::kOneCore: return ThreadAffinity::kAnyCore;
            default:
                FatalError(TAG, "Unknown thread setup %d", (int)setup);
        }
    }

    enum class WorkScheme {
        kDividedEvenly, kInterleaved, kGreedy
    };
    constexpr const char *kWorkSchemeNames[] = {
            "Divided", "Interleaved", "Greedy"
    };

    enum class FileApi {
        kCApi, kCppStreams, kPosix
    };
    constexpr const char *kFileApiNames[] = {
            "CAPI", "C++ Streams", "posix"
    };

    struct Configuration {
        FileSetup file_setup;
        ThreadSetup thread_setup;
        WorkScheme work_scheme;
        FileApi file_api;
        // If we're creating a new file, how big should we make it?
        // TODO(tmillican@google.com): A size helper like we have with time
        //  would make writing configuration files a lot easier.
        Bytes created_file_size = 0;

        // The section of the file to read from. read_area_end will default to
        // the end of the file if 0, so 0/0 will read from the entire file.
        Bytes read_area_start = 0;
        Bytes read_area_end = 0;
        // How much to read in total. We will repeat reading the above area *in
        // full* until we've reached/exceded this amount.
        Bytes total_read;

        Bytes read_align = 0;    // Pad so file reads start at this alignment.
        Bytes buffer_size;       // Read this many bytes per read.
        Bytes buffer_align = -1; // The alignment of the buffer allocation. -1
                                 // writes directly to the shared data; 0 uses
                                 // default malloc.

        Milliseconds report_rate; // How often should each thread make a report?

        // Should we pin threads to a specific core?
        // Note that big/little thread setups will still pin to big/little cores
        // without this flag. Threads just won't be pinned to a single specific
        // core.
        bool pin_affinity = true;
        // Lock before writing to shared data?
        // Depending on your read/alignment/etc., locking access to the final
        // buffer may be unnecessary. Even if it isn't, since this test is
        // primarily about file I/O we give the option to avoid that little bit
        // of extra overhead.
        bool lock_on_data_write = false;

        // Calculated/determined internally
        std::filesystem::path file_path; // Note: Currently single-file only
        Bytes total_area_size;
        Bytes padded_read_size;
        Bytes total_data_size;
        int num_threads;
    };

    JSON_READER(Configuration) {
        JSON_REQENUM(file_setup, kFileSetupNames);
        JSON_REQENUM(thread_setup, kThreadSetupNames);
        JSON_REQENUM(work_scheme, kWorkSchemeNames);
        JSON_REQENUM(file_api, kFileApiNames);

        if (data.file_setup == FileSetup::kCreatedFile) {
            JSON_REQVAR(created_file_size);
        }
        JSON_OPTVAR(read_area_start);
        JSON_OPTVAR(read_area_end);
        JSON_REQVAR(total_read);

        JSON_OPTVAR(pin_affinity);
        JSON_OPTVAR(lock_on_data_write);
        JSON_OPTVAR(read_align);
        JSON_REQVAR(buffer_size);
        JSON_OPTVAR(buffer_align);

        JSON_REQVAR(report_rate);

        // We can determine a few of these before verifying the file info.
        data.padded_read_size =
                NextAlignedValue(data.buffer_size, data.read_align);
    }

//------------------------------------------------------------------------------

    struct Datum {
        Bytes cumulative_bytes;
    };

    JSON_WRITER(Datum) {
        JSON_REQVAR(cumulative_bytes);
    }
}

//==============================================================================
// Shared datatypes

class IOPerformanceOperation;

namespace {
    struct FileDataOffsets {
        Bytes file;
        Bytes data;
    };

    // A place to get the next file/data offsets from when using the greedy scheme.
    class GreedyDispenser {
    public:
        GreedyDispenser(const Configuration& config)
        : _offsets{config.read_area_start, 0}
        , _file_inc{config.padded_read_size}
        , _data_inc{config.buffer_size} {
        }

        [[nodiscard]] FileDataOffsets GrabNextOffsets() {
            std::scoped_lock lock{_mutex};
            auto prev_offsets = _offsets;
            _offsets.file += _file_inc;
            _offsets.data += _data_inc;
            return prev_offsets;
        }
    private:
        std::mutex _mutex;
        FileDataOffsets _offsets;
        Bytes _file_inc;
        Bytes _data_inc;
    };


    // The final location all data is being collected in.
    struct FinalData {
        std::optional<std::mutex> mutex;
        std::vector<char> data;
    };
}

//==============================================================================
// Thread setup & affinity

namespace {
    struct ThreadConfiguration {
        int id;
        ThreadAffinity affinity;
        int cpu_id;
    };


    [[nodiscard]] auto DetermineThreadSetups(const Configuration &config) {
        std::vector<ThreadConfiguration> threads;

        const auto setup = config.thread_setup;
        const auto affinity = ToAffinity(setup);
        // Note: This may be zero if we're requesting little cores on a device
        // that has none. Fortunately, the logic can handle that just fine.
        const auto cpu_count =
                setup == ThreadSetup::kOneCore ? 1 : NumCores(affinity);

        for (int i = 0 ; i < cpu_count ; ++i) {
            threads.push_back({i, affinity,
                               config.pin_affinity ? i : -1});
        }

        Log::D(TAG, "Running %d %s %s threads", cpu_count,
               config.pin_affinity ? "locked" : "unlocked",
               kThreadSetupNames[(int)setup]);

        return threads;
    }
}

//==============================================================================
// Advancing in the file/data based on our thread & scheme

namespace {
    // Minor note: The logic was written with even chunks/divisions in mind, but
    // it will still work with 'unbalanced' loads. There may be a tiny bit of
    // re-reading at division edges in some setups, but it's effectively a non-
    // issue.
    class EvenDivisionScheme {
    public:
        EvenDivisionScheme(int thread, const Configuration& config)
        : _file_chunk{config.padded_read_size}
        , _data_chunk{config.buffer_size}
        , _file_division{config.total_area_size / config.num_threads}
        , _data_division{config.total_data_size / config.num_threads}
        , _offsets{_file_division * thread, _data_division * thread}
        , _file_end{_offsets.file + _file_division} {
        }

        [[nodiscard]]
        const FileDataOffsets& GetOffsetView() const noexcept { return _offsets; }

        void Update() {
            _offsets.file += _file_chunk;
            _offsets.data += _data_chunk;
        }

        [[nodiscard]] bool ShouldContinue() const noexcept {
            return _offsets.file < _file_end;
        }
    private:
        Bytes _file_chunk;
        Bytes _data_chunk;
        Bytes _file_division;
        Bytes _data_division;
        FileDataOffsets _offsets;
        Bytes _file_end;
    };


    class InterleavedScheme {
    public:
        InterleavedScheme(int thread, const Configuration& config)
        : _offsets{config.padded_read_size * thread, config.buffer_size * thread}
        , _file_advance{config.padded_read_size * config.num_threads}
        , _data_advance{config.buffer_size * config.num_threads}
        , _file_end{config.total_area_size} {
        }

        [[nodiscard]]
        const FileDataOffsets& GetOffsetView() const noexcept { return _offsets; }

        void Update() {
            _offsets.file += _file_advance;
            _offsets.data += _data_advance;
        }

        [[nodiscard]] bool ShouldContinue() const noexcept {
            return _offsets.file < _file_end;
        }
    private:
        FileDataOffsets _offsets;
        Bytes _file_advance;
        Bytes _data_advance;
        Bytes _file_end;
    };


    class GreedyScheme {
    public:
        GreedyScheme(const Configuration& config, GreedyDispenser& greedy)
        : _greedy(greedy), _file_end{config.total_area_size} {
            Update();
        }

        [[nodiscard]]
        const FileDataOffsets& GetOffsetView() const noexcept { return _offsets; }

        void Update() {
            _offsets = _greedy.GrabNextOffsets();
        }

        [[nodiscard]] bool ShouldContinue() const noexcept {
            return _offsets.file < _file_end;
        }
    private:
        GreedyDispenser& _greedy;
        Bytes _file_end;
        FileDataOffsets _offsets;
    };
}

//==============================================================================
// Handling a shared & intermediate data buffers.

namespace {
    // Doubles as a post-read helper for OwnedBuffer.
    class DirectBuffer {
    public:
        template <typename WorkScheme>
        DirectBuffer(FinalData &data, const Reporter& reporter,
                     const Configuration &config, const WorkScheme& scheme)
        : _last_report(SteadyClock::now())
        , _report_rate(config.report_rate)
        , _data(data)
        , _reporter(reporter)
        , _read_size(config.buffer_size)
        , _offsets(scheme.GetOffsetView()) {
        }

        ~DirectBuffer() {
            if (_bytes_read != Bytes{0}) {
                ReportReads();
            }
        }

        template <typename File>
        void ReadFrom(File&& file) {
            if (_data.mutex) {
                std::scoped_lock lock{*_data.mutex};
                _bytes_read += file.Read(GetData(), _read_size);
            } else {
                _bytes_read += file.Read(GetData(), _read_size);
            }
            DataTouched();
        }

        void CopyFrom(const char* buffer, Bytes size) {
            assert((_offsets.data + size).count() < _data.data.size());

            if (_data.mutex) {
                std::scoped_lock lock{*_data.mutex};
                memcpy(GetData(), buffer, size.count());
            } else {
                memcpy(GetData(), buffer, size.count());
            }
            _bytes_read += size;
            DataTouched();
        }

    private:
        [[nodiscard]] char *GetData() noexcept {
            return _data.data.data() + _offsets.data.count();
        }

        void DataTouched() {
            const auto now = SteadyClock::now();
            if (_last_report < now + _report_rate) {
                ReportReads();
                _last_report = now;
            }
        }

        void ReportReads() {
            _reporter(Datum{_bytes_read});
        }

        Timestamp _last_report;
        Milliseconds _report_rate;
        FinalData &_data;
        const Reporter& _reporter;
        Bytes _read_size;
        Bytes _bytes_read = 0;
        const FileDataOffsets &_offsets;
    };


    // Reads to a thread-owned buffer and moves into the shared buffer afterward.
    // We use the direct buffer to actually handle writing to the main data
    // storage to avoid code duplication.
    class OwnedBuffer {
    public:
        template <typename WorkScheme>
        OwnedBuffer(FinalData &data, const Reporter& reporter,
                    const Configuration &config, const WorkScheme& scheme)
        : _buf_size(config.buffer_size)
        // TODO(tmillican@google.com): aligned_alloc isn't implemented yet, so
        //  buffer_align is also unsupported.
        , _buffer(/*config.buffer_align
                ? aligned_alloc(config.buffer_align, _buffer_size)
                : */(char *) malloc(_buf_size.count()))
        , _databuffer{data, reporter, config, scheme} {
            if (config.buffer_align.count() != 0) {
                FatalError(TAG, "buffer_align is not currently supported.");
            }
        }

        ~OwnedBuffer() {
            free(_buffer);
        }

        template <typename File>
        void ReadFrom(File&& file) {
            file.Read(_buffer, _buf_size);
            _databuffer.CopyFrom(_buffer, _buf_size);
        }

    private:
        Bytes _buf_size;
        char *_buffer;
        DirectBuffer _databuffer;
    };
}

//==============================================================================
// Different file APIs

namespace {
    class CApi {
    public:
        template <typename Scheme>
        CApi(const Configuration &config, const Scheme& scheme)
        : _offsets(scheme.GetOffsetView())
        , _avoid_seek(config.work_scheme == WorkScheme::kDividedEvenly &&
                      config.padded_read_size == config.buffer_size)
        , _file(fopen(config.file_path.c_str(), "rb")) {
            if (_file == nullptr) {
                FatalError(TAG, "Failed to open file '%s'",
                           config.file_path.c_str());
            }
        }

        ~CApi() {
            fclose(_file);
        }

        Bytes Read(char *buffer, Bytes buf_size) {
            if (!_avoid_seek) {
                fseek(_file, _offsets.file.count(), SEEK_SET);
            }
            return fread(buffer, sizeof(char), buf_size.count(), _file);
        }

    private:
        const FileDataOffsets &_offsets;
        bool _avoid_seek;
        FILE *_file;
    };


    class CppStreams {
    public:
        template <typename Scheme>
        CppStreams(const Configuration &config, const Scheme& scheme)
        : _offsets(scheme.GetOffsetView())
        , _avoid_seek(config.work_scheme == WorkScheme::kDividedEvenly &&
                      config.padded_read_size == config.buffer_size)
        , _file(config.file_path, std::ios::binary) {
            if (!_file.is_open()) {
                FatalError(TAG, "Failed to open file '%s'",
                           config.file_path.c_str());
            }
        }

        Bytes Read(char *buffer, Bytes buf_size) {
            if (!_avoid_seek) {
                _file.seekg(_offsets.file.count());
            }
            _file.read(buffer, buf_size.count());
            return _file.gcount();
        }

    private:
        const FileDataOffsets &_offsets;
        bool _avoid_seek;
        std::ifstream _file;
    };


    class Posix {
    public:
        template <typename Scheme>
        Posix(const Configuration &config, const Scheme& scheme)
        : _offsets(scheme.GetOffsetView())
        , _file(open(config.file_path.c_str(), O_RDONLY)) {
            if (_file == -1) {
                FatalError(TAG, "Failed to open file '%s' with errno %d",
                           config.file_path.c_str(), errno);
            }
        }

        ~Posix() {
            close(_file);
        }

        Bytes Read(char *buffer, Bytes buf_size) {
            return pread(_file, buffer, buf_size.count(), _offsets.file.count());
        }

    private:
        const FileDataOffsets &_offsets;
        int _file;
    };
}

//==============================================================================
// The main class

class IOPerformanceOperation : public BaseOperation {
public:
    IOPerformanceOperation() = default;

    void Start() override {
        BaseOperation::Start();
        _config = GetConfiguration<Configuration>();

        _thread = std::thread{[this] {
            PrepareFileAndData();

            const auto thread_configs = DetermineThreadSetups(_config);
            _config.num_threads = std::size(thread_configs);

            // Perform the operation we've set up until we've reached/exceeded
            // our quota.
            std::vector<std::thread> threads;
            for ( Bytes read = 0 ; read < _config.total_read ;
                  read += _config.total_area_size ) {
                Log::D(TAG, "Performing new read (%zu / %zu)",
                       read, _config.total_read);

                threads.reserve(_config.num_threads);

                // GreedyScheme has some state to the side we need to handle.
                if (_config.work_scheme == WorkScheme::kGreedy) {
                    GreedyDispenser greedy{_config};
                    for (auto& thread_config : thread_configs) {
                        threads.emplace_back([this, thread_config, &greedy] {
                            GreedyScheme scheme{_config, greedy};
                            DoWork(thread_config, scheme);
                        });
                    }
                    for (auto &thread : threads) {
                        thread.join();
                    }
                } else {
                    for (auto& thread_config : thread_configs) {
                        threads.emplace_back([this, thread_config] {
                            switch (_config.work_scheme) {
                                case WorkScheme::kDividedEvenly: {
                                    EvenDivisionScheme scheme{thread_config.id,
                                                        _config};
                                    return DoWork(thread_config, scheme);
                                }
                                case WorkScheme::kInterleaved: {
                                    InterleavedScheme scheme{thread_config.id,
                                                       _config};
                                    return DoWork(thread_config, scheme);
                                }
                                default:
                                    FatalError(TAG, "Invalid work scheme '%d'",
                                               _config.work_scheme);
                            }
                        });
                    }
                    for (auto &thread : threads) {
                        thread.join();
                    }
                }

                threads.clear();
            }

            Log::D(TAG, "Read complete");

            CleanupFileAndData();
        }};
    }

    void Wait() override {
        _thread.join();
    }

private:

//------------------------------------------------------------------------------
// File & Data creation & cleanup

    static auto CheckPreexistingFile(FileSetup setup) {
        std::filesystem::path file_path = [setup] {
            switch (setup) {
                case FileSetup::kBaseApk: return RawResourcePath();
                case FileSetup::kSplitApk: FatalError(TAG, "Currently unsupported");
                case FileSetup::kObb: FatalError(TAG, "Currently unsupported");
                default:
                    FatalError(TAG, "Bad file location %d", static_cast<int>(setup));
            }
        }();

        std::ifstream file{file_path, std::ios_base::binary};
        if (!file.is_open()) {
            FatalError(TAG, "Failed to open file '%s' for writing",
                       file_path.c_str());
        }
        file.ignore(std::numeric_limits<std::streamsize>::max());
        auto file_size = static_cast<size_t>(file.gcount());

        Log::D(TAG, "Verified %s file exists with size %zu",
               file_path.c_str(), file_size);

        return std::pair{file_path, file_size};
    }

    static auto CreateTemporaryFile(Bytes file_size) {
        const auto file_path = std::filesystem::path{InternalDataPath() + "/test.bin"};
        std::ofstream file{file_path, std::ios_base::binary | std::ios_base::trunc};
        if (!file.is_open()) {
            FatalError(TAG, "Failed to open file '%s' for writing",
                       file_path.c_str());
        }

        for (Bytes i = 0; i < file_size; ++i) {
            file.put(static_cast<char>(i.count()));
        }

        Log::D(TAG, "Created %s with size %zu", file_path.c_str(), file_size.count());

        return std::pair{file_path, file_size};
    }

    //--------------

    void PrepareFileAndData() {
        const auto [file_path, file_size] =
                _config.file_setup == FileSetup::kCreatedFile
                ? CreateTemporaryFile(_config.created_file_size)
                : CheckPreexistingFile(_config.file_setup);

        _config.file_path = file_path;


        if (_config.read_area_end == Bytes{0}) {
            _config.read_area_end = file_size;
        }
        if (_config.read_area_start > _config.read_area_end ||
            _config.read_area_end > file_size) {
            FatalError(TAG,
                    "Area start/end (%zu/%zu) are invalid for file of size %zu",
                    _config.read_area_start, _config.read_area_end, file_size);
        }
        _config.total_area_size = _config.read_area_end - _config.read_area_start;


        // TODO(tmillican@google.com): Might lose a bit of data on the end.
        _config.total_data_size =
                (_config.total_area_size / _config.padded_read_size) *
                _config.buffer_size;

        _data.data.resize(_config.total_data_size.count());
        if (_config.lock_on_data_write) {
            _data.mutex.emplace();
        }

        Log::D(TAG, "File & data prepared.");
    }


    void CleanupFileAndData() {
        _data.data.resize(0);
        _data.mutex = std::nullopt;

        if (_config.file_setup == FileSetup::kCreatedFile &&
            remove(_config.file_path.c_str()) != 0) {
            Log::W(TAG, "Failed to delete created file '%s' with errno '%d'",
                    _config.file_path.c_str(), errno);
        }

        Log::D(TAG, "Cleanup complete.");
    }

//------------------------------------------------------------------------------
// Putting everything together

    template <typename Scheme>
    void DoWork(const ThreadConfiguration& thread_config, Scheme& scheme) {
        SetThreadAffinity(thread_config.cpu_id, thread_config.affinity);

        Reporter reporter{*this};
        if (_config.buffer_align == Bytes{-1}) {
            DirectBuffer buffer{_data, reporter, _config, scheme};
            DoWork(scheme, buffer);
        } else {
            OwnedBuffer buffer{_data, reporter, _config, scheme};
            DoWork(scheme, buffer);
        }
    }

    template <typename Scheme, typename Buffer>
    void DoWork(Scheme& scheme, Buffer& buffer) {
        switch (_config.file_api) {
            case FileApi::kCApi: {
                CApi api{_config, scheme};
                return DoWork(scheme, buffer, api);
            }
            case FileApi::kCppStreams: {
                CppStreams api{_config, scheme};
                return DoWork(scheme, buffer, api);
            }
            case FileApi::kPosix: {
                Posix api{_config, scheme};
                return DoWork(scheme, buffer, api);
            }
            default:
                FatalError(TAG, "Invalid File API '%d'", _config.file_api);
        }
    }

    template<typename Scheme, typename Buffer, typename File>
    void DoWork(Scheme& scheme, Buffer& buffer, File& file) {
        bool is_stopped = IsStopped();
        bool should_continue = scheme.ShouldContinue();

        while (!IsStopped() && scheme.ShouldContinue()) {
            buffer.ReadFrom(file);
            scheme.Update();
        }
    }

//------------------------------------------------------------------------------

    Configuration _config;
    std::thread _thread;

    FinalData _data;
};

//==============================================================================

EXPORT_ANCER_OPERATION(IOPerformanceOperation)
