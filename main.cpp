#include "kv_store.h"
#include "async_server.h"
#include "resp_parser.h"
#include "proto_handler.h"
#include <iostream>
#include <memory>
#include <thread>

int main() {
    try {
        KVStore store;
        RedisProtocolHandler dbHandler(store);
        AsyncServer server(9001);
        
        server.setRequestHandler([&dbHandler](const std::string& request) {
            return dbHandler.handle_request(request);
        });
        std::cout << "Server starting at " << 9001 << std::endl; 
        server.start();
        
        std::thread t1([&store] {
            while (true) {
                store.persistToDisk();
                std::this_thread::sleep_for(std::chrono::seconds(1000));
            }
        });
        t1.detach();

        std::cin.get();
        
        server.stop();
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}