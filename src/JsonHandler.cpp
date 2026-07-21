#include "../include/JsonHandler.hpp"
#include <sstream>
#include <cctype>
#include <functional>

JsonHandler::JsonHandler() {}

JsonHandler::~JsonHandler() {}

std::string JsonHandler::escapeJsonString(const std::string& str) const {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                escaped += c;
        }
    }
    return escaped;
}

std::string JsonHandler::validateJsonSyntax(const std::string& json) const {
    size_t pos = 0;
    const size_t len = json.size();

    const auto skipWs = [&]() {
        while (pos < len && std::isspace(static_cast<unsigned char>(json[pos])))
            ++pos;
    };

    std::function<std::string(std::string&)> parseString;
    std::function<std::string()>             parseValue;
    std::function<std::string()>             parseObject;
    std::function<std::string()>             parseArray;

    // Parse a JSON string (opening '"' must be the current character)
    parseString = [&](std::string& out) -> std::string {
        if (pos >= len || json[pos] != '"')
            return "Expected '\"' at position " + std::to_string(pos);
        ++pos;
        while (pos < len && json[pos] != '"') {
            if (json[pos] == '\\') {
                ++pos;
                if (pos >= len)
                    return "Unterminated escape sequence in string literal";
                const char esc = json[pos];
                if (esc != '"' && esc != '\\' && esc != '/' &&
                    esc != 'b' && esc != 'f' && esc != 'n' &&
                    esc != 'r' && esc != 't' && esc != 'u')
                    return std::string("Invalid escape character '\\") + esc + "' in string";
            } else if (static_cast<unsigned char>(json[pos]) < 0x20) {
                return "Unescaped control character in string literal";
            }
            out += json[pos++];
        }
        if (pos >= len)
            return "Unterminated string literal — missing closing '\"'";
        ++pos; // consume closing '"'
        return "";
    };

    // Parse a JSON object (opening '{' already consumed)
    parseObject = [&]() -> std::string {
        skipWs();
        if (pos >= len)
            return "Unexpected end of input inside object — missing '}'";
        if (json[pos] == '}') { ++pos; return ""; } // empty object {}

        while (true) {
            skipWs();
            if (pos >= len)
                return "Unexpected end of input — expected a key inside object";

            // Key must be a quoted string
            if (json[pos] != '"') {
                if (std::isalpha(static_cast<unsigned char>(json[pos])) || json[pos] == '_') {
                    std::string bareKey;
                    while (pos < len && (std::isalnum(static_cast<unsigned char>(json[pos])) || json[pos] == '_'))
                        bareKey += json[pos++];
                    return "Key '" + bareKey + "' is not quoted — JSON keys must be enclosed in double quotes (e.g. \"" + bareKey + "\")";
                }
                return "Expected a quoted key at position " + std::to_string(pos) +
                       " — found '" + std::string(1, json[pos]) + "'";
            }

            std::string key;
            std::string err = parseString(key);
            if (!err.empty()) return err;

            skipWs();
            if (pos >= len)
                return "Unexpected end of input after key '" + key + "' — expected ':'";
            if (json[pos] != ':')
                return "Expected ':' after key '" + key + "' but found '" + std::string(1, json[pos]) + "'";
            ++pos; // consume ':'

            skipWs();
            // Detect completely absent value — e.g. {"symbol":} or {"symbol":,}
            if (pos < len && (json[pos] == ',' || json[pos] == '}'))
                return "Missing value for key '" + key + "' — a value is required after ':'";
            err = parseValue();
            if (!err.empty()) return err;

            skipWs();
            if (pos >= len)
                return "Unexpected end of input after value for '" + key + "' — expected ',' or '}'";
            if (json[pos] == '}') { ++pos; return ""; }
            if (json[pos] != ',')
                return "Expected ',' or '}' after value for '" + key + "' but found '" + std::string(1, json[pos]) + "'";
            ++pos; // consume ','

            skipWs();
            if (pos < len && json[pos] == '}')
                return "Trailing comma before '}' is not valid JSON";
        }
    };

    // Parse a JSON array (opening '[' already consumed)
    parseArray = [&]() -> std::string {
        skipWs();
        if (pos >= len)
            return "Unexpected end of input inside array — missing ']'";
        if (json[pos] == ']') { ++pos; return ""; }

        while (true) {
            skipWs();
            std::string err = parseValue();
            if (!err.empty()) return err;

            skipWs();
            if (pos >= len)
                return "Unexpected end of input inside array — expected ',' or ']'";
            if (json[pos] == ']') { ++pos; return ""; }
            if (json[pos] != ',')
                return "Expected ',' or ']' inside array but found '" + std::string(1, json[pos]) + "'";
            ++pos; // consume ','

            skipWs();
            if (pos < len && json[pos] == ']')
                return "Trailing comma before ']' is not valid JSON";
        }
    };

    // Parse any JSON value
    parseValue = [&]() -> std::string {
        skipWs();
        if (pos >= len)
            return "Unexpected end of input — expected a value";

        const char c = json[pos];
        if (c == '"') {
            std::string tmp;
            return parseString(tmp);
        }
        if (c == '{') { ++pos; return parseObject(); }
        if (c == '[') { ++pos; return parseArray(); }
        if (c == 't') {
            if (json.compare(pos, 4, "true") == 0) { pos += 4; return ""; }
            return "Invalid token starting with 't' at position " + std::to_string(pos) + " — did you mean 'true'?";
        }
        if (c == 'f') {
            if (json.compare(pos, 5, "false") == 0) { pos += 5; return ""; }
            return "Invalid token starting with 'f' at position " + std::to_string(pos) + " — did you mean 'false'?";
        }
        if (c == 'n') {
            if (json.compare(pos, 4, "null") == 0) { pos += 4; return ""; }
            return "Invalid token starting with 'n' at position " + std::to_string(pos) + " — did you mean 'null'?";
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            if (c == '-') ++pos;
            if (pos >= len || !std::isdigit(static_cast<unsigned char>(json[pos])))
                return "Invalid number at position " + std::to_string(pos);
            while (pos < len && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
            if (pos < len && json[pos] == '.') {
                ++pos;
                if (pos >= len || !std::isdigit(static_cast<unsigned char>(json[pos])))
                    return "Expected digits after decimal point at position " + std::to_string(pos);
                while (pos < len && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
            }
            if (pos < len && (json[pos] == 'e' || json[pos] == 'E')) {
                ++pos;
                if (pos < len && (json[pos] == '+' || json[pos] == '-')) ++pos;
                if (pos >= len || !std::isdigit(static_cast<unsigned char>(json[pos])))
                    return "Expected digits after exponent at position " + std::to_string(pos);
                while (pos < len && std::isdigit(static_cast<unsigned char>(json[pos]))) ++pos;
            }
            return "";
        }
        return std::string("Unexpected character '") + c + "' at position " + std::to_string(pos);
    };

    // Root must be a JSON object
    skipWs();
    if (pos >= len) return ""; // empty — handled upstream
    if (json[pos] != '{') {
        if (json[pos] == '[')
            return "Request body must be a JSON object, not an array";
        return "Request body must be a JSON object — expected '{' but found '" + std::string(1, json[pos]) + "'";
    }
    ++pos; // consume '{'
    std::string err = parseObject();
    if (!err.empty()) return err;

    skipWs();
    if (pos < len)
        return std::string("Unexpected content after closing '}': '") + json[pos] + "'";

    return ""; // valid
}


bool JsonHandler::hasField(const std::string& json, const std::string& field) const {
    // Match "\"fieldname\":" to avoid false positives (e.g. "symbolic" matching "symbol")
    std::string search = "\"" + field + "\":";
    return json.find(search) != std::string::npos;
}

std::string JsonHandler::extractString(const std::string& json, const std::string& field) const {
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return "";
    }
    
    pos = json.find("\"", pos + search.length());
    if (pos == std::string::npos) {
        return "";
    }
    
    size_t end = json.find("\"", pos + 1);
    if (end == std::string::npos) {
        return "";
    }
    
    return json.substr(pos + 1, end - pos - 1);
}

int JsonHandler::extractInt(const std::string& json, const std::string& field) const {
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return 0;
    }
    
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    std::string numStr;
    while (pos < json.length() && (std::isdigit(json[pos]) || json[pos] == '-')) {
        numStr += json[pos++];
    }
    
    return numStr.empty() ? 0 : std::stoi(numStr);
}

double JsonHandler::extractDouble(const std::string& json, const std::string& field) const {
    std::string search = "\"" + field + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return 0.0;
    }
    
    pos += search.length();
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    
    std::string numStr;
    while (pos < json.length() && (std::isdigit(json[pos]) || json[pos] == '-' || json[pos] == '.')) {
        numStr += json[pos++];
    }
    
    return numStr.empty() ? 0.0 : std::stod(numStr);
}

std::string JsonHandler::createResponse(const std::string& name, int value, const std::string& timestamp) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"name\": \"" << escapeJsonString(name) << "\",\n";
    json << "  \"value\": " << value << ",\n";
    json << "  \"status\": \"processed\",\n";
    json << "  \"processedAt\": \"" << timestamp << "\"\n";
    json << "}";
    return json.str();
}

std::string JsonHandler::createErrorResponse(const std::string& error) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"error\": \"" << escapeJsonString(error) << "\"\n";
    json << "}";
    return json.str();
}

std::string JsonHandler::createErrorResponse(const std::vector<std::string>& errors) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"errors\": [\n";
    for (size_t i = 0; i < errors.size(); ++i) {
        json << "    \"" << escapeJsonString(errors[i]) << "\"";
        if (i < errors.size() - 1) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}";
    return json.str();
}

std::string JsonHandler::createHealthResponse(const std::string& timestamp) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"status\": \"healthy\",\n";
    json << "  \"timestamp\": \"" << timestamp << "\"\n";
    json << "}";
    return json.str();
}

std::string JsonHandler::createMessageResponse(const std::string& message) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"message\": \"" << escapeJsonString(message) << "\"\n";
    json << "}";
    return json.str();
}

CmsWrappedPacket JsonHandler::createCmsWrappedPacket(const std::string& instrumentId, const std::string& side,
                                                     double limitPrice, int quantity, const std::string& exchange,
                                                     const std::string& clientTimestamp,
                                                     int contractId, int strategyId,
                                                     int clientId, int dealerId,
                                                     const std::string& orderType) const {
    CmsWrappedPacket wrappedPacket;

    wrappedPacket.payload.contractId = contractId;
    wrappedPacket.payload.strategyId = strategyId;
    wrappedPacket.payload.clientId   = clientId;
    wrappedPacket.payload.dealerId   = dealerId;
    wrappedPacket.payload.setSide(side);
    wrappedPacket.payload.price    = static_cast<int32_t>(limitPrice * 10000.0);
    wrappedPacket.payload.quantity = static_cast<uint32_t>(quantity);
    wrappedPacket.payload.setInstrumentId(instrumentId);
    wrappedPacket.payload.setExchange(exchange);
    wrappedPacket.payload.setClientTimestamp(clientTimestamp);
    wrappedPacket.payload.setOrderType(orderType);

    return wrappedPacket;
}

std::string JsonHandler::formatCmsPacketForLog(const CmsWrappedPacket& packet) const {
    std::ostringstream oss;
    oss << "CMS Wrapped Packet [" << packet.getPacketSize() << " bytes]:\n";
    oss << "  Header: length=" << packet.header.length
        << " messageType=" << packet.header.messageType << " (CMSMESSAGE_WEBSERVER_ORDER)\n";
    oss << "  Payload (WebServerOrderPayload):\n";
    oss << "    contractId: "     << packet.payload.contractId     << "\n";
    oss << "    strategyId: "     << packet.payload.strategyId     << "\n";
    oss << "    clientId: "       << packet.payload.clientId       << "\n";
    oss << "    dealerId: "       << packet.payload.dealerId       << "\n";
    oss << "    side: "           << packet.payload.side
        << " (" << (packet.payload.side == 0 ? "BUY" : packet.payload.side == 1 ? "SELL" : "INVALID") << ")\n";
    oss << "    price (x10000): "   << packet.payload.price
        << " (" << (packet.payload.price / 10000.0) << " actual)\n";
    oss << "    quantity: "       << packet.payload.quantity       << "\n";
    oss << "    orderType: "      << packet.payload.orderType
        << " (" << (packet.payload.orderType == 0 ? "MARKET" : packet.payload.orderType == 1 ? "LIMIT" : "OTHER") << ")\n";
    oss << "    instrumentId: "   << packet.payload.instrumentId   << "\n";
    oss << "    exchange: "       << packet.payload.exchange       << "\n";
    oss << "    clientTimestamp: "<< packet.payload.clientTimestamp;
    return oss.str();
}

std::string JsonHandler::buildJson(const std::map<std::string, std::string>& fields) const {
    std::ostringstream json;
    json << "{\n";
    
    bool first = true;
    for (const auto& pair : fields) {
        if (!first) {
            json << ",\n";
        }
        json << "  \"" << pair.first << "\": \"" << escapeJsonString(pair.second) << "\"";
        first = false;
    }
    
    json << "\n}";
    return json.str();
}
