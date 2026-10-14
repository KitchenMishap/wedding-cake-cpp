#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <system_error>
#include <fstream>
#include <mutex>
#include <condition_variable>

// The 6 exact headers of evolving_disk_state
#include "evolving_disk_state/external_feeder.hpp"
#include "evolving_disk_state/manifest_engine.hpp"
#include "evolving_disk_state/pipeline_buffer.hpp"
#include "evolving_disk_state/slice_context.hpp"
#include "evolving_disk_state/tier_orchestrator.hpp"
#include "evolving_disk_state/type_states.hpp"

namespace {

using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Mock Tier 0 receiver to test ExternalFeeder connection
template <typename T>
class MockTier0 {
public:
    std::atomic<bool> notified{false};

    void notify_input_buffer_full_and_ready(ExternalFeeder<T, MockTier0<T>>& feeder) {
        notified.store(true);
        feeder.with_full_contents_as_consumer_maybe_wipe([](const T& data) {
            return true; // Consume and wipe slot
        });
    }
};

class EvolvingDiskStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unique directory per test execution using timestamp / thread id
        auto time_stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        test_dir_ = fs::temp_directory_path() / ("evolving_disk_state_run_" + std::to_string(time_stamp));

        std::error_code ec;
        fs::remove_all(test_dir_, ec);
        fs::create_directories(test_dir_, ec);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(test_dir_, ec);
    }

    fs::path test_dir_;
};

// Helper class to manage pipeline quiescence / wait_for_settled()
class PipelineSettler {
public:
    void add_work(int64_t count = 1) {
        in_flight_.fetch_add(count, std::memory_order_relaxed);
    }

    void mark_completed(int64_t count = 1) {
        if (in_flight_.fetch_sub(count, std::memory_order_acq_rel) == count) {
            std::lock_guard<std::mutex> lock(cv_mutex_);
            cv_.notify_all();
        }
    }

    void wait_for_settled() {
        std::unique_lock<std::mutex> lock(cv_mutex_);
        cv_.wait(lock, [this] { return in_flight_.load(std::memory_order_acquire) == 0; });
    }

    int64_t in_flight() const { return in_flight_.load(std::memory_order_acquire); }

private:
    std::atomic<int64_t> in_flight_{0};
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

// -----------------------------------------------------------------------------
// Test 1: ExternalFeeder push and consumption cycle
// -----------------------------------------------------------------------------
TEST_F(EvolvingDiskStateTest, ExternalFeederConnectAndConsume) {
    ExternalFeeder<HashEntry, MockTier0<HashEntry>> feeder;
    MockTier0<HashEntry> mock_tier0;

    feeder.connect_tier0(&mock_tier0);

    HashEntry sample_entry{0x1234567890ABCDEFULL, 42};
    feeder.insert_data(sample_entry);

    EXPECT_TRUE(mock_tier0.notified.load());
}

// -----------------------------------------------------------------------------
// Test 2: OutputBuffer multi-threaded producer/consumer with Settling
// -----------------------------------------------------------------------------
TEST_F(EvolvingDiskStateTest, OutputBufferConcurrentPushAndWipe) {
    constexpr size_t capacity = 5;
    OutputBuffer<HashEntry> buffer(capacity);
    PipelineSettler settler;

    std::atomic<size_t> items_consumed{0};
    std::atomic<bool> keep_running{true};

    // Consumer thread draining full batches
    std::thread consumer([&]() {
        while (keep_running.load() || settler.in_flight() > 0) {
            bool processed = buffer.with_full_contents_as_consumer_maybe_wipe([&](const std::vector<HashEntry>& contents) {
                items_consumed.fetch_add(contents.size());
                settler.mark_completed(contents.size());
                return true; // Request wipe
            });

            if (!processed) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    // Producer pushes exactly 2 full capacity batches (10 items)
    constexpr size_t total_items = capacity * 2;
    settler.add_work(total_items);

    for (uint32_t i = 0; i < total_items; ++i) {
        buffer.push_back_as_provider(HashEntry{i * 1000ULL, i});
    }

    // Client waits for pipeline to settle
    settler.wait_for_settled();

    keep_running.store(false);
    consumer.join();

    EXPECT_EQ(items_consumed.load(), total_items);
    EXPECT_EQ(settler.in_flight(), 0);
}

// -----------------------------------------------------------------------------
// Test 3: SliceContext microstep execution and Manifest commit
// -----------------------------------------------------------------------------
TEST_F(EvolvingDiskStateTest, SliceContextWritesAndCommits) {
    fs::path state_dir = test_dir_ / "slice_state";
    fs::create_directories(state_dir);

    ManifestEngine manifest(state_dir);

    {
        SliceContext slice(state_dir, manifest, 101 /* slice_id */);
        FillingSackWorker worker(state_dir);

        HashEntry entry{0xDEADBEEFULL, 1};
        worker.execute_work_slice(slice, entry);
    } // SliceContext destructor triggers commit_work_slice

    // Verify raw_hashes.dat was created and contains data
    fs::path data_file = state_dir / "raw_hashes.dat";
    EXPECT_TRUE(fs::exists(data_file));
    EXPECT_EQ(fs::file_size(data_file), sizeof(HashEntry));

    // Verify manifest.tx binary log exists
    fs::path manifest_file = state_dir / "manifest.tx";
    EXPECT_TRUE(fs::exists(manifest_file));
}


    // -----------------------------------------------------------------------------
    // Test 4: ManifestEngine crash recovery truncation
    // -----------------------------------------------------------------------------
    TEST_F(EvolvingDiskStateTest, ManifestEngineRecoveryTruncatesUncommittedBytes) {
    fs::path state_dir = test_dir_ / "recovery_state";
    fs::create_directories(state_dir);

    // Scope 1: Write and commit slice 1 cleanly
    {
        ManifestEngine manifest(state_dir);
        {
            SliceContext slice1(state_dir, manifest, 1);
            slice1.microstep({"data.bin"}, [](auto& files) {
                files.at("data.bin")->write("COMMITTED_DATA_", 15);
            });
        } // ~SliceContext triggers commit_work_slice, flushes & closes data.bin handles
    } // ~ManifestEngine flushes & closes manifest.tx handle

    fs::path data_file = state_dir / "data.bin";

    // Scope 2: Simulate uncommitted dirty append from crash mid-write
    {
        std::ofstream raw_out(data_file, std::ios::binary | std::ios::app);
        raw_out.write("UNCOMMITTED_GARBAGE", 19);
        raw_out.flush();
        raw_out.close();
    }

    EXPECT_EQ(fs::file_size(data_file), 15 + 19);

    // Scope 3: Crash recovery truncates dirty append back to last commit offset (15)
    ManifestEngine::recover_state_directory(state_dir);

    // Verify file truncated back to 15 bytes
    EXPECT_EQ(fs::file_size(data_file), 15u);
}

// -----------------------------------------------------------------------------
// Test 5: TierOrchestrator boot, state detection, and folder transitions
// -----------------------------------------------------------------------------
TEST_F(EvolvingDiskStateTest, TierOrchestratorFolderTransitions) {
    fs::path sack_dir = test_dir_ / "sack_workspace";
    fs::create_directories(sack_dir);

    TierOrchestrator orchestrator(sack_dir);

    // 1. Boot from empty directory -> Origin state
    orchestrator.boot_and_recover();

    // 2. Create FillingSack subfolder and transition
    fs::create_directories(sack_dir / FillingSackWorker::folder_name);
    orchestrator.boot_and_recover();

    // Transition Filling -> Sprinkling
    orchestrator.transition_to_next_state();
    EXPECT_TRUE(fs::exists(sack_dir / SprinklingSackWorker::folder_name));

    // Transition Sprinkling -> Product
    orchestrator.transition_to_next_state();
    EXPECT_TRUE(fs::exists(sack_dir / ProductState::folder_name));
}

// -----------------------------------------------------------------------------
// Test 6: End-to-End Multi-Threaded Ingestion Pipeline with Quiescence
// -----------------------------------------------------------------------------
TEST_F(EvolvingDiskStateTest, FullEndToEndIngestPipeline) {
    fs::path sack_dir = test_dir_ / "e2e_workspace";
    fs::path filling_dir = sack_dir / FillingSackWorker::folder_name;
    fs::create_directories(filling_dir);

    ExternalFeeder<HashEntry, MockTier0<HashEntry>> feeder;
    OutputBuffer<HashEntry> buffer(10 /* capacity */);

    ManifestEngine manifest(filling_dir);
    FillingSackWorker worker(filling_dir);
    PipelineSettler settler;

    std::atomic<bool> pipeline_running{true};
    std::atomic<size_t> items_processed{0};

    // Background worker thread simulating pipeline processing
    std::thread worker_thread([&]() {
        uint32_t slice_counter = 0;

        while (pipeline_running.load() || settler.in_flight() > 0) {
            bool processed = buffer.with_full_contents_as_consumer_maybe_wipe([&](const std::vector<HashEntry>& entries) {
                SliceContext slice(filling_dir, manifest, slice_counter++);
                for (const auto& entry : entries) {
                    worker.execute_work_slice(slice, entry);
                }
                items_processed.fetch_add(entries.size());
                settler.mark_completed(entries.size());
                return true; // Clear buffer
            });

            if (!processed) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    // Ingest exactly 2 full batches (20 items) into the system
    constexpr size_t kTotalPushes = 20;
    settler.add_work(kTotalPushes);

    for (size_t i = 0; i < kTotalPushes; ++i) {
        buffer.push_back_as_provider(HashEntry{i * 0x22222222ULL, static_cast<uint32_t>(i)});
    }

    // Client thread calls wait_for_settled() - waits for full quiescence
    settler.wait_for_settled();

    // Signal worker thread to finish
    pipeline_running.store(false);
    worker_thread.join();

    EXPECT_EQ(items_processed.load(), kTotalPushes);
    EXPECT_EQ(settler.in_flight(), 0);
    EXPECT_TRUE(fs::exists(filling_dir / "raw_hashes.dat"));
    EXPECT_EQ(fs::file_size(filling_dir / "raw_hashes.dat"), kTotalPushes * sizeof(HashEntry));
}

} // namespace