#pragma once

#include "config.hpp"
#include "dbn_reader.hpp"
#include "order_book.hpp"

void run_streamer(DbnReader& reader, const Options& opts);
void run_engine(OrderBook& book, const Options& opts);
