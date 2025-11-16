#pragma once

#include <optional>
#include <string>
#include <cstdint>

#include "databento/dbn_file_store.hpp"  // include from databento-cpp (adjust path)

struct MboEvent {
    std::uint64_t ts_event;
    std::uint64_t order_id;
    double        price;
    std::int64_t  qty;
    std::uint32_t instrument_id;
    char          side;   // 'B' or 'S'
    enum class Type : std::uint8_t { Add, Modify, Delete, Trade } type;
};

class DbnReader {
public:
    DbnReader(const std::string& path,
              std::optional<std::uint32_t> instrument_id);

    std::optional<MboEvent> next();

private:
    // store databento types here
};
