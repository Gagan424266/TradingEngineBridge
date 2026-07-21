#include "../include/WebServer.hpp"
#include "../include/Logger.h"
#include "../include/TradingViewPacket.hpp"
#include <iostream>
#include <cstring>

WebServer::WebServer() 
    : configParser(nullptr), jsonHandler(nullptr), requestHandler(nullptr), 
      tcpClient(nullptr), daemon(nullptr), ip("0.0.0.0"), port(8080), 
      cmsHost(""), cmsPort(0), dealerId(-1), isRunning(false) {
    
    configParser = new ConfigParser();
    jsonHandler = new JsonHandler();
    tcpClient = new TcpClient();
    requestHandler = new RequestHandler(jsonHandler, tcpClient);
}

WebServer::WebServer(const std::string& configFile) 
    : daemon(nullptr), ip("0.0.0.0"), port(8080), 
      cmsHost(""), cmsPort(0), dealerId(-1), isRunning(false) {
    
    configParser = new ConfigParser();
    jsonHandler = new JsonHandler();
    tcpClient = new TcpClient();
    requestHandler = new RequestHandler(jsonHandler, tcpClient);
    
    loadConfiguration(configFile);
}

WebServer::~WebServer() {
    if (isRunning) {
        stop();
    }
    
    if (tcpClient != nullptr) {
        tcpClient->disconnect();
        delete tcpClient;
        tcpClient = nullptr;
    }
    
    if (requestHandler != nullptr) {
        delete requestHandler;
        requestHandler = nullptr;
    }
    
    // Note: jsonHandler is deleted by RequestHandler
    jsonHandler = nullptr;
    
    if (configParser != nullptr) {
        delete configParser;
        configParser = nullptr;
    }
}

bool WebServer::loadConfiguration(const std::string& configFile) {
    if (!configParser->loadConfig(configFile)) {
        return false;
    }
    
    return validateConfiguration();
}

bool WebServer::validateConfiguration() {
    if (!configParser->hasKey("server.ip")) {
        LOG_ERROR("Missing 'ip' in config file [server] section");
        return false;
    }
    
    if (!configParser->hasKey("server.port")) {
        LOG_ERROR("Missing 'port' in config file [server] section");
        return false;
    }
    
    ip = configParser->getValue("server.ip");
    port = configParser->getIntValue("server.port");
    
    if (port <= 0 || port > 65535) {
        LOG_ERROR("Invalid port value: " + std::to_string(port));
        return false;
    }
    
    // Dealer ID from config (required for CMS orders)
    dealerId = configParser->getIntValue("server.dealerid", -1);
    if (dealerId >= 0) {
        LOG_INFO("Dealer ID loaded from config: " + std::to_string(dealerId));
        requestHandler->setDealerId(dealerId);
    } else {
        LOG_WARN("No valid dealerid in config [server] section — using default -1");
    }

    if (!configParser->hasKey("auth.token")) {
        LOG_ERROR("Missing 'token' in config file [auth] section");
        return false;
    }
    std::string authToken = configParser->getValue("auth.token");
    if (authToken.empty()) {
        LOG_ERROR("Empty 'token' in config file [auth] section");
        return false;
    }
    requestHandler->setAuthToken(std::move(authToken));

    // CMS configuration (optional)
    if (configParser->hasKey("cms.host") && configParser->hasKey("cms.port")) {
        cmsHost = configParser->getValue("cms.host");
        cmsPort = configParser->getIntValue("cms.port");
        
        if (cmsPort <= 0 || cmsPort > 65535) {
            LOG_WARN("Invalid CMS port value: " + std::to_string(cmsPort));
            cmsHost = "";
            cmsPort = 0;
        } else {
            LOG_INFO("CMS configuration loaded: " + cmsHost + ":" + std::to_string(cmsPort));
        }
    }
    
    return true;
}

void WebServer::printStartupInfo(const std::string& configFile) {
    LOG_INFO("========================================");
    LOG_INFO("C++ WebServer Starting...");
    LOG_INFO("Config: " + configFile);
    LOG_INFO("IP: " + ip);
    LOG_INFO("Port: " + std::to_string(port));
    LOG_INFO("Endpoints:");
    LOG_INFO("  POST /api/v1/webhook - Webhook processing endpoint");
    LOG_INFO("  GET  /health         - Health check");
    LOG_INFO("========================================");
}

bool WebServer::start() {
    if (isRunning) {
        LOG_WARN("Server is already running");
        return false;
    }
    
    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD,
                             port,
                             nullptr, nullptr,
                             &RequestHandler::requestCallback, requestHandler,
                             MHD_OPTION_END);
    
    if (daemon == nullptr) {
        LOG_ERROR("Failed to start server on port " + std::to_string(port));
        isRunning = false;
        return false;
    }
    
    isRunning = true;
    return true;
}

void WebServer::stop() {
    if (daemon != nullptr) {
        MHD_stop_daemon(daemon);
        daemon = nullptr;
    }
    isRunning = false;
    LOG_INFO("Server stopped");
}

void WebServer::waitForTermination() {
    std::cout << "Press Enter to stop the server..." << std::endl;
    getchar();
}

bool WebServer::connectToCms() {
    if (cmsHost.empty() || cmsPort == 0) {
        LOG_ERROR("CMS host/port not configured");
        return false;
    }
    
    return tcpClient->connect(cmsHost, static_cast<kissnet::port_t>(cmsPort));
}

bool WebServer::disconnectFromCms() {
    if (tcpClient) {
        return tcpClient->disconnect();
    }
    return false;
}

bool WebServer::isCmsConnected() const {
    return tcpClient && tcpClient->isConnected();
}

bool WebServer::sendToCms(const std::string& message) {
    if (tcpClient && tcpClient->isConnected()) {
        return tcpClient->send(message);
    }
    LOG_ERROR("Cannot send to CMS: not connected");
    return false;
}

std::string WebServer::receiveFromCms(size_t maxBytes) {
    if (tcpClient && tcpClient->isConnected()) {
        return tcpClient->receive(maxBytes);
    }
    LOG_ERROR("Cannot receive from CMS: not connected");
    return "";
}

void WebServer::startCmsReceiveLoop() {
    if (!tcpClient) return;

    tcpClient->startReceiveLoop([](int16_t messageType, const std::byte* payload, size_t payloadLen) {
        if (messageType == CMS_RESPONSE_ORDER_UPDATE) {
            if (payloadLen < sizeof(CmsOrderUpdatePayload)) {
                LOG_WARN("OrderUpdate payload too small: " + std::to_string(payloadLen) + " bytes");
                return;
            }
            CmsOrderUpdatePayload update;
            std::memcpy(&update, payload, sizeof(CmsOrderUpdatePayload));
            Logger::logOrderResponse(update.toString());

        } else if (messageType == CMS_RESPONSE_ORDER_FILL) {
            if (payloadLen < sizeof(CmsOrderFillPayload)) {
                LOG_WARN("OrderFill payload too small: " + std::to_string(payloadLen) + " bytes");
                return;
            }
            CmsOrderFillPayload fill;
            std::memcpy(&fill, payload, sizeof(CmsOrderFillPayload));
            Logger::logOrderResponse(fill.toString());
        } else {
            LOG_DEBUG("CMS receive loop: unhandled messageType=" + std::to_string(messageType));
        }
    });

    LOG_INFO("CMS receive loop started — order responses will be logged to order_responses log");
}

void WebServer::stopCmsReceiveLoop() {
    if (tcpClient) {
        tcpClient->stopReceiveLoop();
    }
}

void WebServer::printInfo() const {
    LOG_INFO("Server Status: " + std::string(isRunning ? "Running" : "Stopped"));
    LOG_INFO("IP: " + ip);
    LOG_INFO("Port: " + std::to_string(port));
    LOG_INFO("CMS Host: " + (cmsHost.empty() ? "Not configured" : cmsHost));
    LOG_INFO("CMS Port: " + (cmsPort == 0 ? "Not configured" : std::to_string(cmsPort)));
    LOG_INFO("CMS Connected: " + std::string(isCmsConnected() ? "Yes" : "No"));
}
