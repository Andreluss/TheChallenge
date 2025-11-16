#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "dbn_reader.hpp"
#include <nlohmann/json.hpp>

struct Order {
    std::uint64_t order_id;
    char side;
    double price;
    std::int64_t qty;
};

struct Level {
    double price;
    std::int64_t total_qty;
    std::size_t num_orders;
};

class OrderBook {
public:
    void on_event(const MboEvent& ev);

    nlohmann::json snapshot() const;
    void write_snapshot_json(const std::string& path) const;

private:
    std::unordered_map<std::uint64_t, Order> orders_;
    std::map<double, Level, std::greater<>> bids_;
    std::map<double, Level> asks_;
};
