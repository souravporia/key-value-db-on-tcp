#ifndef REDIS_PROTOCOL_HANDLER_H
#define REDIS_PROTOCOL_HANDLER_H

#include "kv_store.h"
#include "resp_parser.h"
#include <memory>


/**
 * @class RedisProtocolHandler
 * @brief Handles Redis-style commands using RESP protocol.
 */
class RedisProtocolHandler {
public:
/**
     * @brief Constructs the RedisProtocolHandler with a reference to KVStore.
     * @param store Reference to the key-value store instance.
     */
    explicit RedisProtocolHandler(KVStore& store) : store_(store) {}

    /**
     * @brief Processes a raw RESP request string and generates a response.
     * @param request The RESP-encoded command string.
     * @return The RESP-encoded response string.
     */
    std::string handle_request(const std::string& request) {
        size_t pos = 0;
        try {
            auto resp_value = RESPParser::parse(request, pos);
            return process_command(resp_value);
        } catch (const std::exception& e) {
            return RESPParser::createErrorResponse("ERR " + std::string(e.what()));
        }
    }

private:
    std::string process_command(const std::shared_ptr<RESPValue>& command) {
        if (command->type != RESPValue::Type::Array || command->arrayValue.empty()) {
            return RESPParser::createErrorResponse("ERR invalid command");
        }

        const auto& cmd = command->arrayValue[0];
        if (cmd->type != RESPValue::Type::BulkString && cmd->type != RESPValue::Type::SimpleString) {
            return RESPParser::createErrorResponse("ERR invalid command");
        }

        const std::string& command_str = cmd->strValue;

        if (command_str == "GET" && command->arrayValue.size() == 2) {
            auto key = command->arrayValue[1]->strValue;
            auto value = store_.get(key);
            return value ? RESPParser::createRESPResponse(*value) 
                        : RESPParser::createMissingResponse();
        }
        else if (command_str == "SET" && command->arrayValue.size() == 3) {
            auto key = command->arrayValue[1]->strValue;
            auto value = command->arrayValue[2]->strValue;
            store_.set(key, value);
            return RESPParser::createOKResponse();
        }
        else if (command_str == "DEL" && command->arrayValue.size() == 2) {
            auto key = command->arrayValue[1]->strValue;
            bool deleted = store_.del(key);
            return RESPParser::createDELResponse(deleted);
        }

        return RESPParser::createErrorResponse("ERR unknown command");
    }

    KVStore& store_;
};

#endif // REDIS_PROTOCOL_HANDLER_H