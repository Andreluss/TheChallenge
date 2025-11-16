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

void run_engine(OrderBook& book, const Options& opts) {
    int sock = connect_to_server(opts.host, opts.port);
    std::cout << "Engine connected to " << opts.host << ":" << opts.port << "\n";

    constexpr std::size_t MSG_SIZE = sizeof(MboMsg);

    std::uint64_t received = 0;
    auto start = std::chrono::steady_clock::now();

    std::vector<double> latencies_us;
    latencies_us.reserve(1'000'000);

    MboMsg msg{};

    while (true) {
        std::size_t n = recv_all(sock, &msg, MSG_SIZE);
        if (n == 0) break;        // EOF
        if (n < MSG_SIZE) break;  // truncated / error

        auto t0 = std::chrono::steady_clock::now();

        book.on_event(msg);

        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us.push_back(us);

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

        std::cerr << "{ \"metric\": \"latency_p99_us\", \"value\": " << p99 << " }\n";
        std::cerr << "{ \"metric\": \"latency_p95_us\", \"value\": " << p95 << " }\n";
        std::cerr << "{ \"metric\": \"throughput_msg_per_s\", \"value\": " << throughput << " }\n";
    }

    ::close(sock);

    book.write_snapshot_json(opts.output_path);
}
