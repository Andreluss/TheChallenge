#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <chrono>
#include "dbn_reader.hpp"
#include <nlohmann/json.hpp>
#include <databento/pretty.hpp> // Px

// Adapted from https://databento.com/docs/examples/order-book/limit-order-book/example
namespace db = databento;
using json = nlohmann::json;

struct PriceLevel
{
    int64_t price{db::kUndefPrice};
    uint32_t size{0};
    uint32_t count{0};

    bool IsEmpty() const { return price == db::kUndefPrice; }
    operator bool() const { return !IsEmpty(); }
};

class DBBook
{
public:
    std::pair<PriceLevel, PriceLevel> Bbo() const
    {
        return {GetBidLevel(), GetAskLevel()};
    }

    std::pair<int, int> BidAskLevelCounts() const
    {
        return {static_cast<int>(bids_.size()), static_cast<int>(offers_.size())};
    }

    PriceLevel GetBidLevel(std::size_t idx = 0) const
    {
        if (bids_.size() > idx)
        {
            // Reverse iterator to get highest bid prices first
            auto level_it = bids_.rbegin();
            std::advance(level_it, idx);
            return GetPriceLevel(level_it->first, level_it->second);
        }
        return PriceLevel{};
    }

    PriceLevel GetAskLevel(std::size_t idx = 0) const
    {
        if (offers_.size() > idx)
        {
            auto level_it = offers_.begin();
            std::advance(level_it, idx);
            return GetPriceLevel(level_it->first, level_it->second);
        }
        return PriceLevel{};
    }

    PriceLevel GetBidLevelByPx(int64_t px) const
    {
        auto level_it = bids_.find(px);
        if (level_it == bids_.end())
        {
            throw std::invalid_argument{"No bid level at " +
                                        db::pretty::PxToString(px)};
        }
        return GetPriceLevel(px, level_it->second);
    }

    PriceLevel GetAskLevelByPx(int64_t px) const
    {
        auto level_it = offers_.find(px);
        if (level_it == offers_.end())
        {
            throw std::invalid_argument{"No ask level at " +
                                        db::pretty::PxToString(px)};
        }
        return GetPriceLevel(px, level_it->second);
    }

    const db::MboMsg &GetOrder(uint64_t order_id)
    {
        auto order_it = orders_by_id_.find(order_id);
        if (order_it == orders_by_id_.end())
        {
            throw std::invalid_argument{"No order with ID " +
                                        std::to_string(order_id)};
        }
        auto &level = GetLevel(order_it->second.side, order_it->second.price);
        return *GetLevelOrder(level, order_id);
    }

    uint32_t GetQueuePos(uint64_t order_id)
    {
        auto order_it = orders_by_id_.find(order_id);
        if (order_it == orders_by_id_.end())
        {
            throw std::invalid_argument{"No order with ID " +
                                        std::to_string(order_id)};
        }
        const auto &level_it =
            GetLevel(order_it->second.side, order_it->second.price);
        uint32_t prior_size = 0;
        for (const auto &order : level_it)
        {
            if (order.order_id == order_id)
            {
                break;
            }
            prior_size += order.size;
        }
        return prior_size;
    }

    std::vector<db::BidAskPair> GetSnapshot(std::size_t level_count = 1) const
    {
        std::vector<db::BidAskPair> res;
        for (size_t i = 0; i < level_count; ++i)
        {
            db::BidAskPair ba_pair{db::kUndefPrice, db::kUndefPrice, 0, 0, 0, 0};
            auto bid = GetBidLevel(i);
            if (bid)
            {
                ba_pair.bid_px = bid.price;
                ba_pair.bid_sz = bid.size;
                ba_pair.bid_ct = bid.count;
            }
            auto ask = GetAskLevel(i);
            if (ask)
            {
                ba_pair.ask_px = ask.price;
                ba_pair.ask_sz = ask.size;
                ba_pair.ask_ct = ask.count;
            }
            res.emplace_back(ba_pair);
        }
        return res;
    }

    void Apply(const db::MboMsg &mbo)
    {
        switch (mbo.action)
        {
        case db::Action::Clear:
        {
            Clear();
            break;
        }
        case db::Action::Add:
        {
            Add(mbo);
            break;
        }
        case db::Action::Cancel:
        {
            Cancel(mbo);
            break;
        }
        case db::Action::Modify:
        {
            Modify(mbo);
            break;
        }
        case db::Action::Trade:
        case db::Action::Fill:
        case db::Action::None:
        {
            break;
        }
        default:
        {
            throw std::invalid_argument{std::string{"Unknown action: "} +
                                        db::ToString(mbo.action)};
        }
        }
    }

private:
    using LevelOrders = std::vector<db::MboMsg>;
    struct PriceAndSide
    {
        int64_t price;
        db::Side side;
    };
    using Orders = std::unordered_map<uint64_t, PriceAndSide>;
    using SideLevels = std::map<int64_t, LevelOrders>;

    static PriceLevel GetPriceLevel(int64_t price, const LevelOrders level)
    {
        PriceLevel res{price};
        for (const auto &order : level)
        {
            if (!order.flags.IsTob())
            {
                ++res.count;
            }
            res.size += order.size;
        }
        return res;
    }

    static LevelOrders::iterator GetLevelOrder(LevelOrders &level,
                                               uint64_t order_id)
    {
        auto order_it = std::find_if(level.begin(), level.end(),
                                     [order_id](const db::MboMsg &order)
                                     {
                                         return order.order_id == order_id;
                                     });
        if (order_it == level.end())
        {
            throw std::invalid_argument{"No order with ID " +
                                        std::to_string(order_id)};
        }
        return order_it;
    }

    void Clear()
    {
        orders_by_id_.clear();
        offers_.clear();
        bids_.clear();
    }

    void Add(db::MboMsg mbo)
    {
        if (mbo.flags.IsTob())
        {
            SideLevels &levels = GetSideLevels(mbo.side);
            levels.clear();
            // kUndefPrice indicates the side's book should be cleared
            // and doesn't represent an order that should be added
            if (mbo.price != db::kUndefPrice)
            {
                LevelOrders level = {mbo};
                levels.emplace(mbo.price, level);
            }
        }
        else
        {
            LevelOrders &level = GetOrInsertLevel(mbo.side, mbo.price);
            level.emplace_back(mbo);
            auto res = orders_by_id_.emplace(mbo.order_id,
                                             PriceAndSide{mbo.price, mbo.side});
            if (!res.second)
            {
                throw std::invalid_argument{"Received duplicated order ID " +
                                            std::to_string(mbo.order_id)};
            }
        }
    }

    void Cancel(db::MboMsg mbo)
    {
        LevelOrders &level = GetLevel(mbo.side, mbo.price);
        auto order_it = GetLevelOrder(level, mbo.order_id);
        if (order_it->size < mbo.size)
        {
            throw std::logic_error{
                "Tried to cancel more size than existed for order ID " +
                std::to_string(mbo.order_id)};
        }
        order_it->size -= mbo.size;
        if (order_it->size == 0)
        {
            orders_by_id_.erase(mbo.order_id);
            level.erase(order_it);
            if (level.empty())
            {
                RemoveLevel(mbo.side, mbo.price);
            }
        }
    }

    void Modify(db::MboMsg mbo)
    {
        auto price_side_it = orders_by_id_.find(mbo.order_id);
        if (price_side_it == orders_by_id_.end())
        {
            // If order not found, treat it as an add
            Add(mbo);
            return;
        }
        if (price_side_it->second.side != mbo.side)
        {
            throw std::logic_error{"Order " + std::to_string(mbo.order_id) +
                                   " changed side"};
        }
        auto prev_price = price_side_it->second.price;
        LevelOrders &prev_level = GetLevel(mbo.side, prev_price);
        auto level_order_it = GetLevelOrder(prev_level, mbo.order_id);
        if (prev_price != mbo.price)
        {
            price_side_it->second.price = mbo.price;
            prev_level.erase(level_order_it);
            if (prev_level.empty())
            {
                RemoveLevel(mbo.side, prev_price);
            }
            LevelOrders &level = GetOrInsertLevel(mbo.side, mbo.price);
            // Changing price loses priority
            level.emplace_back(mbo);
        }
        else if (level_order_it->size < mbo.size)
        {
            LevelOrders &level = prev_level;
            // Increasing size loses priority
            level.erase(level_order_it);
            level.emplace_back(mbo);
        }
        else
        {
            level_order_it->size = mbo.size;
        }
    }

    SideLevels &GetSideLevels(db::Side side)
    {
        switch (side)
        {
        case db::Side::Ask:
        {
            return offers_;
        }
        case db::Side::Bid:
        {
            return bids_;
        }
        case db::Side::None:
        default:
        {
            throw std::invalid_argument{"Invalid side"};
        }
        }
    }

    LevelOrders &GetLevel(db::Side side, int64_t price)
    {
        SideLevels &levels = GetSideLevels(side);
        auto level_it = levels.find(price);
        if (level_it == levels.end())
        {
            throw std::invalid_argument{
                std::string{"Received event for unknown level "} +
                db::ToString(side) + " " + db::pretty::PxToString(price)};
        }
        return level_it->second;
    }

    LevelOrders &GetOrInsertLevel(db::Side side, int64_t price)
    {
        SideLevels &levels = GetSideLevels(side);
        return levels[price];
    }

    void RemoveLevel(db::Side side, int64_t price)
    {
        SideLevels &levels = GetSideLevels(side);
        levels.erase(price);
    }

    uint64_t LevelOrdersCount(const DBBook::LevelOrders &lvl) const
    {
        uint64_t cnt = 0;
        for (const auto &order : lvl)
        {
            cnt += order.size;
        }
        return cnt;
    }

    uint32_t LevelNumOrders(const DBBook::LevelOrders &lvl) const
    {
        return static_cast<uint32_t>(lvl.size());
    }

    Orders orders_by_id_;
    SideLevels offers_;
    SideLevels bids_;
};

class OrderBook
{
public:
    uint64_t total_orders = 0;
    uint64_t error_count = 0;
    void on_event(const databento::MboMsg &ev);

    json snapshot(int level_count=10) const
    {
        json j;

        auto [best_bid, best_offer] = book_.Bbo();
        j["best_bid"] = best_bid.price;
        j["best_bid_size"] = best_bid.size;
        j["best_ask"] = best_offer.price;
        j["best_ask_size"] = best_offer.size;
        auto [bid_levels, ask_levels] = book_.BidAskLevelCounts();
        j["bid_levels"] = static_cast<uint32_t>(bid_levels);
        j["ask_levels"] = static_cast<uint32_t>(ask_levels);

        j["levels"] = json::array();

        for (int level = 0; level < level_count; ++level)
        {
            json level_json;
            auto bid_level = book_.GetBidLevel(level);
            if (bid_level)
            {
                level_json["bid_price"] = bid_level.price;
                level_json["bid_size"] = bid_level.size;
            }

            auto ask_level = book_.GetAskLevel(level);
            if (ask_level)
            {
                level_json["ask_price"] = ask_level.price;
                level_json["ask_size"] = ask_level.size;
            }

            if (!bid_level && !ask_level)
            {
                break;
            }

            j["levels"].push_back(level_json);
        }

        return j;
    }

    void write_snapshot_json(const std::string &path) const;

    void print_latency_stats() const;
private:
    using Clock = std::chrono::steady_clock;
    std::vector<uint64_t> latencies_ns_;  // one per event / JSON output
    DBBook book_;
};
