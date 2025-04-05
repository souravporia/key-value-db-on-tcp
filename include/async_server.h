#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <system_error>
#include <sched.h>
#include <netinet/tcp.h>

#define MAX_EVENTS 100
#define BUFFER_SIZE 1024

class Worker {
private:
    int server_fd_;
    int epoll_fd_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::function<std::string(const std::string&)> request_handler_;

    void setNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
            throw std::system_error(errno, std::system_category(), "fcntl");
        }
    }

    void setupSocket(uint16_t port) {
        server_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (server_fd_ == -1) {
            throw std::system_error(errno, std::system_category(), "socket");
        }

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
        setsockopt(server_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
            close(server_fd_);
            throw std::system_error(errno, std::system_category(), "bind");
        }

        if (listen(server_fd_, SOMAXCONN) == -1) {
            close(server_fd_);
            throw std::system_error(errno, std::system_category(), "listen");
        }
    }

    void setupEpoll() {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            close(server_fd_);
            throw std::system_error(errno, std::system_category(), "epoll_create1");
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = server_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &event) == -1) {
            close(epoll_fd_);
            close(server_fd_);
            throw std::system_error(errno, std::system_category(), "epoll_ctl");
        }
    }

    void handleClient(int client_fd) {
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

        if (bytes_read <= 0) {
            if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
                close(client_fd);
            }
            return;
        }

        std::string request(buffer, bytes_read);
        std::string response = request_handler_(request);
        
        ssize_t bytes_sent = send(client_fd, response.data(), response.size(), MSG_NOSIGNAL);
        if (bytes_sent == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
        }
    }

    void eventLoop() {
        epoll_event events[MAX_EVENTS];
        
        while (running_) {
            int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);
            if (num_events == -1) {
                if (errno == EINTR) continue;
                break;
            }

            for (int i = 0; i < num_events; ++i) {
                if (events[i].data.fd == server_fd_) {
                    // Accept new connections
                    while (true) {
                        sockaddr_in client_addr{};
                        socklen_t client_len = sizeof(client_addr);
                        int client_fd = accept(server_fd_, 
                                            reinterpret_cast<sockaddr*>(&client_addr), 
                                            &client_len);

                        if (client_fd == -1) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            throw std::system_error(errno, std::system_category(), "accept");
                        }

                        setNonBlocking(client_fd);
                        
                        epoll_event client_event{};
                        client_event.events = EPOLLIN | EPOLLET;
                        client_event.data.fd = client_fd;
                        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                            close(client_fd);
                            throw std::system_error(errno, std::system_category(), "epoll_ctl");
                        }
                    }
                } else {
                    // Handle client I/O
                    handleClient(events[i].data.fd);
                }
            }
        }
    }

public:
    /**
     * @brief Constructs a Worker instance.
     * @param port Port number for the server.
     * @param core_id CPU core ID for affinity.
     */
    Worker(uint16_t port, size_t core_id) {
        setupSocket(port);
        setupEpoll();
        
        // Set thread affinity if requested
        if (core_id != -1UL) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(core_id % std::thread::hardware_concurrency(), &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        }
    }
    /**
     * @brief Destroys the Worker instance, cleaning up resources.
     */
    ~Worker() {
        stop();
        if (epoll_fd_ != -1) close(epoll_fd_);
        if (server_fd_ != -1) close(server_fd_);
    }

    /**
     * @brief Starts the worker thread.
     */
    void start() {
        if (running_) return;
        running_ = true;
        thread_ = std::thread(&Worker::eventLoop, this);
    }

    /**
     * @brief Stops the worker thread.
     */
    void stop() {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    /**
     * @brief Sets the request handler function.
     * @param handler Function to process client requests.
     */
    void setRequestHandler(std::function<std::string(const std::string&)> handler) {
        request_handler_ = std::move(handler);
    }
};

/**
 * @class AsyncServer
 * @brief Manages multiple Worker instances for handling client connections.
 */
class AsyncServer {
private:
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> running_{false};

public:
    /**
     * @brief Constructs an AsyncServer instance.
     * @param port Port number for the server.
     * @param num_workers Number of worker threads (default: hardware concurrency).
     */
    AsyncServer(uint16_t port, size_t num_workers = std::thread::hardware_concurrency()) {
        if (num_workers == 0) num_workers = 1;
        
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back(std::make_unique<Worker>(port, i));
        }
    }

    /**
     * @brief Destroys the AsyncServer instance, stopping all workers.
     */
    ~AsyncServer() {
        stop();
    }

    /**
     * @brief Starts the server by launching worker threads.
     */
    void start() {
        if (running_) return;
        running_ = true;
        
        for (auto& worker : workers_) {
            worker->start();
        }
    }


    /**
     * @brief Stops the server and worker threads.
     */
    void stop() {
        if (!running_) return;
        running_ = false;
        for (auto& worker : workers_) {
            worker->stop();
        }
    }

    /**
     * @brief Sets the request handler function for all workers.
     * @param handler Function to process client requests.
     */
    void setRequestHandler(std::function<std::string(const std::string&)> handler) {
        for (auto& worker : workers_) {
            worker->setRequestHandler(handler);
        }
    }
};

#endif // TCP_SERVER_H