#include "dbn_reader.hpp"

// TODO: include necessary databento headers and implement constructor + next()
// For now you can just return std::nullopt in next() as a stub.
DbnReader::DbnReader(const std::string&, std::optional<std::uint32_t>) {
    // TODO
}

std::optional<MboEvent> DbnReader::next() {
    return std::nullopt; // stub
}
