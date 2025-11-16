#include <iostream>
#include <optional>
#include "dbn_reader.hpp"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: dbn_reader_test <path-to-dbn>\n";
        return 1;
    }

    const std::string dbn_path = argv[1];

    try
    {
        DbnReader reader{dbn_path};

        while (auto ev = reader.next())
        {
            std::cout << "ts=" << ev->hd.ts_event.time_since_epoch().count()
                      << " order_id=" << ev->order_id
                      << " price=" << ev->price
                      << " qty=" << ev->size << "\n";
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Exception: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
