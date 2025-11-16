#include "order_book.hpp"
#include <fstream>

void OrderBook::on_event(const databento::MboMsg& ev) {
    // TODO: implement add/modify/delete/trade logic
}

nlohmann::json OrderBook::snapshot() const {
    nlohmann::json j;
    // TODO: fill bids/asks
    return j;
}

void OrderBook::write_snapshot_json(const std::string& path) const {
    auto j = snapshot();
    std::ofstream out(path);
    out << j.dump(2) << "\n";
}
