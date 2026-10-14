// Purpose: Defines C++ type-state evolver classes mapped 1:1 with filesystem subfolder names.
// Purpose: Implements the domain-specific baking logic operating within nested microstep scopes.
// Purpose: Provides static class reflections for zero-hardcoding recovery and directory creation.

#pragma once
#include "slice_context.hpp"
#include <string_view>

struct HashEntry {
    uint64_t hash;
    uint32_t presentation_index;
};

class ProductState {
public:
    static constexpr std::string_view folder_name = "Product";
};

class SprinklingSackWorker {
public:
    static constexpr std::string_view folder_name = "SprinklingSack";
    using NextState = ProductState;

    explicit SprinklingSackWorker(fs::path workspace) : workspace_(std::move(workspace)) {}

    void execute_work_slice(SliceContext& slice) {
        slice.microstep({"sprinkles_stage1.bf", "sprinkles_stage2.bf"}, [](auto& files) {
            // Write Bloom Filter byte chunks using opaque streams
            files.at("sprinkles_stage1.bf")->write("STAGE1_DATA", 11);
            files.at("sprinkles_stage2.bf")->write("STAGE2_DATA", 11);
        });
    }

private:
    fs::path workspace_;
};

class FillingSackWorker {
public:
    static constexpr std::string_view folder_name = "FillingSack";
    using NextState = SprinklingSackWorker;

    explicit FillingSackWorker(fs::path workspace) : workspace_(std::move(workspace)) {}

    void execute_work_slice(SliceContext& slice, const HashEntry& entry) {
        slice.microstep({"raw_hashes.dat"}, [&](auto& files) {
            files.at("raw_hashes.dat")->write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        });
    }

private:
    fs::path workspace_;
};

class OriginSackWorker {
public:
    static constexpr std::string_view folder_name = "";
    using NextState = FillingSackWorker;
};