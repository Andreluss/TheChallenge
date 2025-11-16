#pragma once

#include <optional>
#include <string>
#include <cstdint>

#include "databento/dbn_file_store.hpp"

class DbnReader {
public:
    DbnReader(const std::string& file);

    std::optional<databento::MboMsg> next();

private:
    // store databento types here
    databento::DbnFileStore store_;
};
