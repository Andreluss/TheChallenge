#include "order_book.hpp"
#include <fstream>

void OrderBook::on_event(const databento::MboMsg& ev) {
    using namespace std::chrono;
    auto start = Clock::now();

    total_orders++;
    try {
        book_.Apply(ev);
    }
    catch (const std::invalid_argument& ex) {
        // Log and ignore invalid events
        // std::cerr << "Warning: " << ex.what() << "\n";
        error_count++;
    }
    catch (const std::logic_error& ex) {
        // Log and ignore logic errors
        // std::cerr << "Warning: " << ex.what() << "\n";
        error_count++;
    }

    auto end = Clock::now();
    auto dt  = duration_cast<nanoseconds>(end - start).count();
    latencies_ns_.push_back(static_cast<uint64_t>(dt));
}

void OrderBook::write_snapshot_json(const std::string& path) const {
    auto j = snapshot(10);
    std::ofstream out(path);
    out << j.dump(2) << "\n";
    std::cerr << "Wrote order book snapshot to " << path << "\n";
    std::cerr << "Total orders processed: " << total_orders << "\n";
    std::cerr << "Total errors encountered: " << error_count << "\n";
}

void OrderBook::print_latency_stats() const {
    if (latencies_ns_.empty()) {
        std::cout << "No latencies recorded.\n";
        return;
    }

    std::vector<uint64_t> a = latencies_ns_;  // copy so we can sort
    std::sort(a.begin(), a.end());

    auto get_percentile = [&](double p) {
        size_t idx = static_cast<size_t>(p * (a.size() - 1));
        return a[idx];
    };

    uint64_t p50_ns = get_percentile(0.50);
    uint64_t p90_ns = get_percentile(0.90);
    uint64_t p99_ns = get_percentile(0.99);
    uint64_t max_ns = a.back();

    auto ns_to_ms = [](uint64_t ns) {
        return static_cast<double>(ns) / 1'000'000.0;
    };

    std::cout << "Latency stats:\n";
    std::cout << "  p50 = " << ns_to_ms(p50_ns) << " ms\n";
    std::cout << "  p90 = " << ns_to_ms(p90_ns) << " ms\n";
    std::cout << "  p99 = " << ns_to_ms(p99_ns) << " ms\n";
    std::cout << "  max = " << ns_to_ms(max_ns) << " ms\n";
}
