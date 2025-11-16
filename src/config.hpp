#pragma once

#include <optional>
#include <string>
#include <cstdint>

enum class Mode {
    Replay,
    Streamer,
    Engine,
};

struct Options {
    Mode mode;
    std::string dbn_path;
    std::optional<std::uint32_t> instrument_id;

    // For replay
    std::string output_path = "book.json";

    // For streaming / engine
    std::string host = "127.0.0.1";
    int port = 9000;
    std::uint64_t rate = 200000; // msgs per second
};

Options parse_options(int argc, char** argv);
