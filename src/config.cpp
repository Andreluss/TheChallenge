#include "config.hpp"

#include <stdexcept>
#include <string_view>

Options parse_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error("Usage: mbo_app --mode=[replay|streamer|engine] [options]");
    }

    Options opts;

    // Very dumb parsing just for starting point
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        if (arg.rfind("--mode=", 0) == 0) {
            auto v = arg.substr(7);
            if (v == "replay")      opts.mode = Mode::Replay;
            else if (v == "streamer") opts.mode = Mode::Streamer;
            else if (v == "engine")   opts.mode = Mode::Engine;
            else throw std::runtime_error("Unknown mode: " + std::string(v));
        } else if (arg.rfind("--dbn=", 0) == 0) {
            opts.dbn_path = std::string(arg.substr(6));
        } else if (arg.rfind("--out=", 0) == 0) {
            opts.output_path = std::string(arg.substr(6));
        } else if (arg.rfind("--port=", 0) == 0) {
            opts.port = std::stoi(std::string(arg.substr(7)));
        } else if (arg.rfind("--rate=", 0) == 0) {
            opts.rate = std::stoull(std::string(arg.substr(7)));
        } else if (arg.rfind("--host=", 0) == 0) {
            opts.host = std::string(arg.substr(7));
        } else if (arg.rfind("--levels=", 0) == 0) {
            opts.order_book_levels = static_cast<std::uint32_t>(
                std::stoul(std::string(arg.substr(9))));
        }
    }

    if (opts.mode != Mode::Engine && opts.dbn_path.empty()) {
        throw std::runtime_error("Missing --dbn=PATH");
    }

    return opts;
}
