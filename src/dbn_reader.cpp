#include "dbn_reader.hpp"

DbnReader::DbnReader(const std::string& file): store_(file)
{
}

std::optional<databento::MboMsg> DbnReader::next()
{
    if (const databento::Record *rec = store_.NextRecord())
    {
        const auto &mbo = rec->Get<databento::MboMsg>();
        return mbo;
    }
    return std::nullopt;
}
