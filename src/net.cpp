#include "net.hpp"
#include "dbn_reader.hpp"
#include "order_book.hpp"

#include <databento/record.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
#include <iostream>

using databento::MboMsg;

int create_listen_socket(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind() failed");
    }
    if (::listen(fd, 1) < 0) {
        ::close(fd);
        throw std::runtime_error("listen() failed");
    }
    return fd;
}

int accept_one_client(int listen_fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len);
    if (client_fd < 0) {
        throw std::runtime_error("accept() failed");
    }
    return client_fd;
}

int connect_to_server(const std::string& host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        ::close(fd);
        throw std::runtime_error("inet_pton() failed");
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("connect() failed");
    }
    return fd;
}

void send_all(int fd, const void* buf, std::size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, 0);
        if (n < 0) throw std::runtime_error("send() failed");
        p += n;
        len -= static_cast<std::size_t>(n);
    }
}

std::size_t recv_all(int fd, void* buf, std::size_t len) {
    char* p = static_cast<char*>(buf);
    std::size_t total = 0;
    while (total < len) {
        ssize_t n = ::recv(fd, p + total, len - total, 0);
        if (n < 0) throw std::runtime_error("recv() failed");
        if (n == 0) break; // EOF
        total += static_cast<std::size_t>(n);
    }
    return total;
}

void run_streamer(DbnReader& reader, const Options& opts) {
    int listen_fd = create_listen_socket(opts.port);
    std::cout << "Streamer listening on port " << opts.port << "...\n";
    int client_fd = accept_one_client(listen_fd);
    std::cout << "Client connected, starting stream\n";

    constexpr std::size_t MSG_SIZE = sizeof(MboMsg);
    static_assert(std::is_trivially_copyable_v<MboMsg>, "MboMsg must be POD");

    const std::size_t batch_size = 1024;
    std::vector<MboMsg> batch;
    batch.reserve(batch_size);

    std::uint64_t sent = 0;
    auto start = std::chrono::steady_clock::now();

    while (auto msg = reader.next()) {
        batch.push_back(*msg);

        if (batch.size() == batch_size) {
            const char* data = reinterpret_cast<const char*>(batch.data());
            std::size_t bytes = batch.size() * MSG_SIZE;
            send_all(client_fd, data, bytes);
            sent += batch.size();
            batch.clear();

            // crude rate limiting to opts.rate msgs/sec
            if (opts.rate > 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                double ideal = static_cast<double>(sent) / static_cast<double>(opts.rate);
                if (elapsed < ideal) {
                    auto sleep_for = std::chrono::duration<double>(ideal - elapsed);
                    std::this_thread::sleep_for(sleep_for);
                }
            }
        }
    }

    if (!batch.empty()) {
        const char* data = reinterpret_cast<const char*>(batch.data());
        std::size_t bytes = batch.size() * MSG_SIZE;
        send_all(client_fd, data, bytes);
        sent += batch.size();
    }

    ::close(client_fd);
    ::close(listen_fd);

    std::cout << "Streamer finished sending " << sent << " messages\n";
}

static void write_snapshot_to_file(json j, std::string path, std::optional<int> snapshot_id) {
    if (snapshot_id) {
        // bla.json -> bla_123.json
        auto dot_pos = path.rfind('.');
        std::string base = (dot_pos == std::string::npos) ? path : path.substr(0, dot_pos);
        std::string ext = (dot_pos == std::string::npos) ? "" : path.substr(dot_pos);
        path = base + "_" + std::to_string(*snapshot_id) + ext;
    }

    std::ofstream out(path);
    if (out.fail()) {
        throw std::runtime_error("Failed to open output file: " + path);
    }
    out << j.dump(2) << "\n";
    std::cerr << "Wrote order book snapshot to " << path << "\n";
}

void run_engine(OrderBook& book, const Options& opts) {
    int sock = connect_to_server(opts.host, opts.port);
    std::cout << "Engine connected to " << opts.host << ":" << opts.port << "\n";

    constexpr std::size_t MSG_SIZE = sizeof(MboMsg);

    std::uint64_t received = 0;
    auto start = std::chrono::steady_clock::now();

    std::vector<double> latencies_us;
    latencies_us.reserve(1'000'000);

    MboMsg msg{}; 

    json whole_feed_json = json::array();

    while (true) {
        std::size_t n = recv_all(sock, &msg, MSG_SIZE);
        if (n == 0) break;        // EOF
        if (n < MSG_SIZE) break;  // truncated / error

        // Measuring latency between A and B
        // A: Message received
        auto t0 = std::chrono::steady_clock::now();

        book.on_event(msg); 
        auto snapshot = book.snapshot(opts.order_book_levels.value_or(5));
        snapshot["ts"] = msg.ts_recv.time_since_epoch().count();

        // B: Snapshot generated and ready to be serialized
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us.push_back(us);
        
        whole_feed_json.push_back(snapshot);
        // here the snapshot can be optionally written to file / logged / streamed to a DB
        // write_snapshot_to_file(snapshot, opts.output_path, msg.ts_recv.time_since_epoch().count());

        ++received;
    }

    auto end = std::chrono::steady_clock::now();
    double total_s = std::chrono::duration<double>(end - start).count();

    if (!latencies_us.empty()) {
        std::sort(latencies_us.begin(), latencies_us.end());
        auto idx99 = static_cast<std::size_t>(latencies_us.size() * 0.99);
        auto idx95 = static_cast<std::size_t>(latencies_us.size() * 0.95);
        double p99 = latencies_us[std::min(idx99, latencies_us.size() - 1)];
        double p95 = latencies_us[std::min(idx95, latencies_us.size() - 1)];
        double throughput = received / total_s;

        std::cerr << "== Metrics ==\n";
        std::cerr << "Latency (p99): " << p99 << " us\n";
        std::cerr << "Latency (p95): " << p95 << " us\n";
        std::cerr << "Throughput    : " << throughput << " msg/s\n";
    }

    write_snapshot_to_file(whole_feed_json, opts.output_path, std::nullopt);

    ::close(sock);
}
