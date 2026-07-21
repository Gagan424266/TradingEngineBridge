#include "../include/RequestHandler.hpp"
#include "../include/Logger.h"
#include "../include/TradingViewPacket.hpp"
#include "../include/SymbolConverter.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>

RequestHandler::RequestHandler() {
    jsonHandler = new JsonHandler();
    tcpClient = nullptr;
}

RequestHandler::RequestHandler(JsonHandler* handler, TcpClient* client) 
    : jsonHandler(handler), tcpClient(client) {}

RequestHandler::~RequestHandler() {
    if (jsonHandler != nullptr) {
        delete jsonHandler;
        jsonHandler = nullptr;
    }
    tcpClient = nullptr;
}

void RequestHandler::setTcpClient(TcpClient* client) {
    tcpClient = client;
}

void RequestHandler::setDealerId(int id) {
    dealerId = id;
}

void RequestHandler::setAuthToken(std::string token) {
    authToken = std::move(token);
}

std::string RequestHandler::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    
#ifdef _WIN32
    gmtime_s(&tm_now, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_now);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

enum MHD_Result RequestHandler::sendResponse(struct MHD_Connection* connection,
                                             const std::string& content,
                                             int statusCode,
                                             const std::string& contentType) {
    struct MHD_Response* response;
    
    response = MHD_create_response_from_buffer(content.length(),
                                               (void*)content.c_str(),
                                               MHD_RESPMEM_MUST_COPY);
    
    // Set content type
    MHD_add_response_header(response, "Content-Type", contentType.c_str());
    
    // CORS headers
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    
    enum MHD_Result ret = MHD_queue_response(connection, statusCode, response);
    MHD_destroy_response(response);
    
    return ret;
}

enum MHD_Result RequestHandler::sendWebhookAck(struct MHD_Connection* connection) {
    struct MHD_Response* response = MHD_create_response_from_buffer(
        0, nullptr, MHD_RESPMEM_PERSISTENT);
    enum MHD_Result ret = MHD_queue_response(connection, 200, response);
    MHD_destroy_response(response);
    return ret;
}

enum MHD_Result RequestHandler::handleGetHealth(struct MHD_Connection* connection) {
    LOG_INFO("GET /health");
    
    std::string timestamp = getCurrentTimestamp();
    std::string response = jsonHandler->createHealthResponse(timestamp);
    
    return sendResponse(connection, response, 200, "application/json");
}

enum MHD_Result RequestHandler::handlePostWebhook(struct MHD_Connection* connection, const std::string& body) {
    const char* token =
        MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "token");
    if (!token || std::strcmp(token, authToken.c_str()) != 0) {
        LOG_WARN("Unauthorized request — invalid or missing token");
        return sendResponse(connection, "Unauthorized", 401, "text/plain");
    }

    LOG_INFO("POST /api/v1/webhook");
    LOG_INFO("Webhook payload received: " + body);

    // Validate request body - empty, too large, malformed JSON
    if (body.empty()) {
        LOG_ERROR("Request body is empty");
        return sendWebhookAck(connection);
    }
    constexpr size_t MAX_BODY_SIZE = 65536;  // 64KB - prevent DoS
    if (body.size() > MAX_BODY_SIZE) {
        LOG_ERROR("Request body exceeds maximum allowed size: " + std::to_string(body.size()) + " bytes");
        return sendWebhookAck(connection);
    }
    {
        const std::string jsonError = jsonHandler->validateJsonSyntax(body);
        if (!jsonError.empty()) {
            LOG_ERROR("Invalid JSON syntax: " + jsonError);
            return sendWebhookAck(connection);
        }
    }

    // Required fields from TradingView.
    // strategy_name is used to look up order config rows in DB.
    // client_id/quantity/strategyId come from the DB config table;
    // dealerId comes from config.ini [server].dealerid;
    // contractId is resolved via SymbolConverter from the securitymaster DB.
    static const char* requiredFields[] = {
        "symbol", "action", "price", "exchange", "timestamp", "strategy_name", "order_type"
    };
    std::vector<std::string> missingFields;
    for (const char* field : requiredFields) {
        if (!jsonHandler->hasField(body, field)) {
            missingFields.push_back(std::string("missing required field: ") + field);
        }
    }
    if (!missingFields.empty()) {
        for (const auto& msg : missingFields) {
            LOG_ERROR(msg);
        }
        return sendWebhookAck(connection);
    }

    // Parse fields that come from the webhook
    std::string symbol             = jsonHandler->extractString(body, "symbol");
    const std::string action       = jsonHandler->extractString(body, "action");
    const double      price        = jsonHandler->extractDouble(body, "price");
    const std::string exchange     = jsonHandler->extractString(body, "exchange");
    const std::string timestamp    = jsonHandler->extractString(body, "timestamp");
    const std::string strategyName = jsonHandler->extractString(body, "strategy_name");
    const std::string orderType    = jsonHandler->extractString(body, "order_type");

    // Validate action is BUY or SELL
    if (action != "BUY" && action != "SELL" && action != "buy" && action != "sell") {
        LOG_ERROR("Invalid action: " + action);
        return sendWebhookAck(connection);
    }

    // Validate price is positive
    if (price <= 0.0) {
        LOG_ERROR("Invalid price: " + std::to_string(price));
        return sendWebhookAck(connection);
    }

    // Validate string fields are not empty
    if (symbol.empty()) {
        LOG_ERROR("Field 'symbol' has an empty value");
        return sendWebhookAck(connection);
    }
    if (exchange.empty()) {
        LOG_ERROR("Field 'exchange' has an empty value");
        return sendWebhookAck(connection);
    }
    if (timestamp.empty()) {
        LOG_ERROR("Field 'timestamp' has an empty value");
        return sendWebhookAck(connection);
    }
    if (strategyName.empty()) {
        LOG_ERROR("Field 'strategy_name' has an empty value");
        return sendWebhookAck(connection);
    }

    // Validate order_type is limit or market
    if (orderType != "limit" && orderType != "LIMIT" && orderType != "market" && orderType != "MARKET") {
        LOG_ERROR("Invalid order_type: " + orderType);
        return sendWebhookAck(connection);
    }

    int contractId = -1;

    ConfigurationMaster* dbConfig = ConfigurationMaster::getInstance();

    SymbolConverter::ResolveResult resolved =
        SymbolConverter::resolveContractId(symbol, exchange);

    if (resolved.contractId >= 0) {
        contractId = resolved.contractId;
        symbol = resolved.datafeedTicker;
    } else {
        LOG_WARN("Symbol '" + symbol + "' (exchange=" + exchange +
                 ") not found in securitymaster: " + resolved.error);
    }

    // Do not forward orders to CMS without a valid contractId.
    // Historically we used -1 as a placeholder; this must be treated as a hard stop.
    if (contractId < 0) {
        LOG_ERROR("Request to place " + action + " order for symbol='" + symbol +
                  "' (exchange=" + exchange + ", strategy='" + strategyName + "')" +
                  " cannot be fulfilled: no valid contractId exists for this symbol." +
                  " Order will NOT be forwarded to CMS.");
        return sendWebhookAck(connection);
    }

    LOG_INFO("Parsed webhook - Symbol: " + symbol + ", Action: " + action +
             ", Price: " + std::to_string(price) +
             ", Exchange: " + exchange + ", Timestamp: " + timestamp +
             ", StrategyName: " + strategyName +
             ", OrderType: " + orderType);

    // ── Fetch cached order rows (auto-refreshed from DB periodically) ─────────
    int strategyConfigId = -1;
    const std::vector<OrderConfigRow> orderRows =
        dbConfig->getCachedOrderConfigRowsByStrategyName(strategyName, &strategyConfigId);
    if (orderRows.empty()) {
        if (strategyConfigId < 0) {
            LOG_ERROR("No strategyconfigid found in tradingviewStrategyConfig for strategy_name='"
                      + strategyName + "'");
        } else {
            LOG_ERROR("No rows found in tradingviewConfigToStrategy for strategyconfigid="
                      + std::to_string(strategyConfigId));
        }
        return sendWebhookAck(connection);
    }

    // ── Fan-out: one order per matching row ───────────────────────────────────
    int totalOrders = 0;
    int sentOrders  = 0;

    for (const auto& orderCfg : orderRows) {
        totalOrders++;

        CmsWrappedPacket packet = jsonHandler->createCmsWrappedPacket(
            symbol, action, price,
            orderCfg.quantity,
            exchange, timestamp,
            contractId,
            orderCfg.strategyId,
            orderCfg.clientId,
            dealerId,
            orderType);

        LOG_INFO("Order [" + std::to_string(totalOrders) + "] — "
                 "strategy=" + strategyName +
                 " clientId=" + std::to_string(orderCfg.clientId) +
                 " strategyId=" + std::to_string(orderCfg.strategyId) +
                 " qty=" + std::to_string(orderCfg.quantity));
        LOG_INFO(jsonHandler->formatCmsPacketForLog(packet));

        if (tcpClient != nullptr && tcpClient->isConnected()) {
            if (tcpClient->send(packet.getBytes(), packet.getPacketSize())) {
                LOG_INFO("Order [" + std::to_string(totalOrders) + "] sent to CMS (" +
                         std::to_string(packet.getPacketSize()) + " bytes)");
                sentOrders++;
            } else {
                LOG_ERROR("Order [" + std::to_string(totalOrders) + "] failed to send");
            }
        } else {
            LOG_WARN("CMS not connected — order [" + std::to_string(totalOrders) + "] not forwarded");
        }
    }

    LOG_INFO("Webhook processing complete: " + std::to_string(sentOrders) +
             "/" + std::to_string(totalOrders) + " order(s) sent to CMS");

    return sendWebhookAck(connection);
}

enum MHD_Result RequestHandler::handleGetRoot(struct MHD_Connection* connection) {
    std::string response = "C++ WebServer - POST to /api/v1/webhook";
    return sendResponse(connection, response, 200, "text/plain");
}

enum MHD_Result RequestHandler::handleNotFound(struct MHD_Connection* connection) {
    std::string response = jsonHandler->createErrorResponse("endpoint not found");
    return sendResponse(connection, response, 404, "application/json");
}

enum MHD_Result RequestHandler::handleRequest(struct MHD_Connection* connection,
                                              const char* url,
                                              const char* method,
                                              const char* upload_data,
                                              size_t* upload_data_size,
                                              void** con_cls) {
    // Handle POST data
    if (strcmp(method, "POST") == 0) {
        if (*con_cls == nullptr) {
            // First call - allocate buffer for POST data
            std::string* data = new std::string();
            *con_cls = data;
            return MHD_YES;
        }
        
        if (*upload_data_size != 0) {
            // Store the upload data
            std::string* data = (std::string*)*con_cls;
            data->append(upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }
    }
    
    // Extract POST body and free con_cls (fixes memory leak for POST to any route)
    std::string body;
    if (*con_cls != nullptr) {
        body = *(std::string*)*con_cls;
        delete (std::string*)*con_cls;
        *con_cls = nullptr;
    }

    // Route handling
    if (strcmp(url, "/health") == 0 && strcmp(method, "GET") == 0) {
        return handleGetHealth(connection);
        
    } else if (strcmp(url, "/api/v1/webhook") == 0) {
        if (strcmp(method, "POST") == 0) {
            return handlePostWebhook(connection, body);
        } else {
            return sendResponse(connection,
                               jsonHandler->createErrorResponse("method not allowed: webhook accepts POST only"),
                               405, "application/json");
        }
        
    } else if (strcmp(url, "/") == 0 && strcmp(method, "GET") == 0) {
        return handleGetRoot(connection);
        
    } else {
        return handleNotFound(connection);
    }
}

enum MHD_Result RequestHandler::requestCallback(void* cls,
                                                struct MHD_Connection* connection,
                                                const char* url,
                                                const char* method,
                                                const char* version,
                                                const char* upload_data,
                                                size_t* upload_data_size,
                                                void** con_cls) {
    RequestHandler* handler = static_cast<RequestHandler*>(cls);
    return handler->handleRequest(connection, url, method, upload_data, upload_data_size, con_cls);
}
