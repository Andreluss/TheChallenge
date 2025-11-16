#include "order_book.hpp"
#include <fstream>

void OrderBook::on_event(const databento::MboMsg& ev) {
    total_orders++;
    try {
        book_.Apply(ev);
    }
    catch (const std::invalid_argument& ex) {
        // Log and ignore invalid events
        std::cerr << "Warning: " << ex.what() << "\n";
        error_count++;
    }
    catch (const std::logic_error& ex) {
        // Log and ignore logic errors
        std::cerr << "Warning: " << ex.what() << "\n";
        error_count++;
    }
}

void OrderBook::write_snapshot_json(const std::string& path) const {
    auto j = book_.snapshot();
    std::ofstream out(path);
    out << j.dump(2) << "\n";
    std::cerr << "Wrote order book snapshot to " << path << "\n";
    std::cerr << "Total orders processed: " << total_orders << "\n";
    std::cerr << "Total errors encountered: " << error_count << "\n";
}
