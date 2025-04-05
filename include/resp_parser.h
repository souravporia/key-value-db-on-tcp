#ifndef RESP_PARSER_H
#define RESP_PARSER_H

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cctype>
#include <charconv>
#include <system_error>

class RESPValue {
public:
    enum class Type {
        SimpleString,
        Error,
        Integer,
        BulkString,
        Array,
        Null
    };

    Type type;
    std::string strValue;
    int64_t intValue;
    std::vector<std::shared_ptr<RESPValue>> arrayValue;

    RESPValue(Type t) : type(t) {}
    
    // Move constructor for performance
    RESPValue(RESPValue&& other) noexcept
        : type(other.type),
          strValue(std::move(other.strValue)),
          intValue(other.intValue),
          arrayValue(std::move(other.arrayValue)) {}
};

/**
 * @class RESPParser
 * @brief Parses RESP messages and generates RESP responses.
 */
class RESPParser {
public:
    /**
     * @brief Parses a RESP message from input.
     * @param input The RESP input string.
     * @param pos Current position in the input string.
     * @return Parsed RESP value.
     * @throws std::runtime_error on invalid RESP format.
     */
    static std::shared_ptr<RESPValue> parse(const std::string& input, size_t& pos) {
        if (pos >= input.length()) {
            throw std::runtime_error("Unexpected end of input");
        }

        const char prefix = input[pos++];
        switch (prefix) {
            case '+': return parseSimpleString(input, pos);
            case '-': return parseError(input, pos);
            case ':': return parseInteger(input, pos);
            case '$': return parseBulkString(input, pos);
            case '*': return parseArray(input, pos);
            default:
                throw std::runtime_error(std::string("Invalid RESP prefix: '") + prefix + "'");
        }
    }

   /**
     * @brief Creates a RESP bulk string response.
     * @param value The string to be formatted as a bulk string.
     * @return Formatted RESP bulk string.
     */
    static std::string createRESPResponse(const std::string& value) noexcept {
        return "$" + std::to_string(value.size()) + "\r\n" + value + "\r\n";
    }
    /**
     * @brief Creates a RESP "OK" response.
     * @return RESP simple string "OK" response.
     */
    static std::string createOKResponse() noexcept {
        return "+OK\r\n";
    }
    /**
     * @brief Creates a RESP error response.
     * @param message The error message.
     * @return Formatted RESP error message.
     */
    static std::string createErrorResponse(const std::string& message) noexcept {
        return "-" + message + "\r\n";
    }
    /**
     * @brief Creates a RESP response for missing values.
     * @return RESP Null Bulk String ($-1\r\n).
     */
    static std::string createMissingResponse() noexcept {
        return "$-1\r\n";
    }
    /**
     * @brief Creates a RESP response for DEL command.
     * @param deleted Whether the key was deleted (true = 1, false = 0).
     * @return RESP integer response.
     */
    static std::string createDELResponse(bool deleted) noexcept {
        return deleted ? ":1\r\n" : ":0\r\n";
    }

private:
    static void checkCRLF(const std::string& input, size_t pos) {
        if (pos + 1 >= input.size() || input[pos] != '\r' || input[pos + 1] != '\n') {
            throw std::runtime_error("Invalid CRLF terminator");
        }
    }

    static std::shared_ptr<RESPValue> parseSimpleString(const std::string& input, size_t& pos) {
        const size_t end = input.find("\r\n", pos);
        if (end == std::string::npos) {
            throw std::runtime_error("Unterminated simple string");
        }

        auto value = std::make_shared<RESPValue>(RESPValue::Type::SimpleString);
        value->strValue = input.substr(pos, end - pos);
        pos = end + 2;
        return value;
    }

    static std::shared_ptr<RESPValue> parseError(const std::string& input, size_t& pos) {
        const size_t end = input.find("\r\n", pos);
        if (end == std::string::npos) {
            throw std::runtime_error("Unterminated error");
        }

        auto value = std::make_shared<RESPValue>(RESPValue::Type::Error);
        value->strValue = input.substr(pos, end - pos);
        pos = end + 2;
        return value;
    }

    static std::shared_ptr<RESPValue> parseInteger(const std::string& input, size_t& pos) {
        const size_t end = input.find("\r\n", pos);
        if (end == std::string::npos) {
            throw std::runtime_error("Unterminated integer");
        }

        auto value = std::make_shared<RESPValue>(RESPValue::Type::Integer);
        const std::string_view num_str(input.data() + pos, end - pos);
        
        // Using from_chars for more efficient number parsing
        const auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), value->intValue);
        if (ec != std::errc() || ptr != num_str.data() + num_str.size()) {
            throw std::runtime_error("Invalid integer format");
        }

        pos = end + 2;
        return value;
    }

    static std::shared_ptr<RESPValue> parseBulkString(const std::string& input, size_t& pos) {
        const size_t lenEnd = input.find("\r\n", pos);
        if (lenEnd == std::string::npos) {
            throw std::runtime_error("Unterminated bulk string length");
        }

        int64_t length;
        const std::string_view len_str(input.data() + pos, lenEnd - pos);
        const auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), length);
        if (ec != std::errc() || ptr != len_str.data() + len_str.size()) {
            throw std::runtime_error("Invalid bulk string length");
        }

        pos = lenEnd + 2;
        if (length == -1) {
            return std::make_shared<RESPValue>(RESPValue::Type::Null);
        }

        if (pos + static_cast<size_t>(length) + 2 > input.size()) {
            throw std::runtime_error("Incomplete bulk string");
        }

        if (input[pos + length] != '\r' || input[pos + length + 1] != '\n') {
            throw std::runtime_error("Invalid bulk string terminator");
        }

        auto value = std::make_shared<RESPValue>(RESPValue::Type::BulkString);
        value->strValue.assign(input.data() + pos, length);  // More efficient than substr
        pos += length + 2;
        return value;
    }

    static std::shared_ptr<RESPValue> parseArray(const std::string& input, size_t& pos) {
        const size_t lenEnd = input.find("\r\n", pos);
        if (lenEnd == std::string::npos) {
            throw std::runtime_error("Unterminated array length");
        }

        int64_t length;
        const std::string_view len_str(input.data() + pos, lenEnd - pos);
        const auto [ptr, ec] = std::from_chars(len_str.data(), len_str.data() + len_str.size(), length);
        if (ec != std::errc() || ptr != len_str.data() + len_str.size()) {
            throw std::runtime_error("Invalid array length");
        }

        pos = lenEnd + 2;
        if (length == -1) {
            return std::make_shared<RESPValue>(RESPValue::Type::Null);
        }

        auto value = std::make_shared<RESPValue>(RESPValue::Type::Array);
        value->arrayValue.reserve(length);

        for (int64_t i = 0; i < length; ++i) {
            value->arrayValue.push_back(parse(input, pos));
        }

        return value;
    }
};

#endif // RESP_PARSER_H