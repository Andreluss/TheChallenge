#include <iostream>
#include <string>
#include <stdexcept>

#include "config.hpp"
#include "dbn_reader.hpp"
#include "order_book.hpp"
#include "net.hpp"

int main(int argc, char** argv) {
    try {
        // 1) Parse CLI args into a simple Options struct
        Options opts = parse_options(argc, argv);

        switch (opts.mode) {
            case Mode::Replay: {
                // DBN -> OrderBook -> JSON snapshot
                DbnReader reader{opts.dbn_path, opts.instrument_id};
                OrderBook book;

                while (auto ev = reader.next()) {
                    book.on_event(*ev);
                }

                book.write_snapshot_json(opts.output_path);
                break;
            }

            case Mode::Streamer: {
                // DBN -> TCP stream (line-based protocol), rate-limited
                DbnReader reader{opts.dbn_path, opts.instrument_id};
                run_streamer(reader, opts); // implement in net.cpp
                break;
            }

            case Mode::Engine: {
                // TCP client -> OrderBook -> metrics + JSON snapshot
                OrderBook book;
                run_engine(book, opts); // implement in net.cpp
                break;
            }

            default:
                std::cerr << "Unknown mode\n";
                return 1;
        }
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
