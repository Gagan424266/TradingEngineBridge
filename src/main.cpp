#include <iostream>
#include "../include/WebServer.hpp"
#include "../include/Logger.h"
#include "../include/ConfigParser.hpp"
#include "ConfigurationMaster.hpp"

int main(int argc, char* argv[]) {
    // Determine config file path
    std::string configFile = "config.ini";
    if (argc > 1) {
        configFile = argv[1];
    }
    
    // Load configuration first to get logger settings
    ConfigParser configParser;
    if (!configParser.loadConfig(configFile)) {
        std::cerr << "[ERROR] Failed to load configuration from: " << configFile << std::endl;
        return 1;
    }
    
    // Initialize logger from configuration
    std::string logFile = configParser.getValue("logger.file", "log.log");
    std::string logLevel = configParser.getValue("logger.level", "info");
    int64_t logSize = configParser.getIntValue("logger.size", 500) * 1024; // Convert KB to bytes
    int maxFiles = configParser.getIntValue("logger.max_file", 5);
    
    // Convert log level string to enum
    Logger::LogLevel level = Logger::InfoLevel;
    if (logLevel == "debug") level = Logger::DebugLevel;
    else if (logLevel == "info") level = Logger::InfoLevel;
    else if (logLevel == "warning") level = Logger::WarningLevel;
    else if (logLevel == "error") level = Logger::ErrorLevel;
    else if (logLevel == "critical") level = Logger::CriticalLevel;
    
    // Initialize the main logger
    Logger::init(logFile, level, logSize, maxFiles);
    LOG_INFO("Logger initialized successfully");
    LOG_INFO("Log file: " + logFile);
    LOG_INFO("Log level: " + logLevel);

    // Initialize the order-response logger (separate file)
    std::string orderResponseLog = configParser.getValue("logger.order_response_file", "logs/order_responses.log");
    Logger::initOrderResponse(orderResponseLog, logSize, maxFiles);
    LOG_INFO("Order response log: " + orderResponseLog);
    
    // Create and configure the web server
    WebServer server;
    
    if (!server.loadConfiguration(configFile)) {
        LOG_ERROR("Failed to load configuration from: " + configFile);
        Logger::shutdown();
        return 1;
    }
    
    // Print startup information
    LOG_INFO("========================================");
    LOG_DEBUG("C++ WebServer Starting...");
    LOG_INFO("Config: " + configFile);
    LOG_INFO("IP: " + server.getIp());
    LOG_INFO("Port: " + std::to_string(server.getPort()));
    LOG_INFO("Endpoints:");
    LOG_INFO("  POST /api/v1/webhook - Webhook processing endpoint");
    LOG_INFO("  GET  /health         - Health check");
    LOG_INFO("CMS Configuration:");
    LOG_INFO("  Host: " + server.getCmsHost());
    LOG_INFO("  Port: " + std::to_string(server.getCmsPort()));
    LOG_INFO("========================================");
    
    // Initialize NSE database connection from config.ini [database]
    std::string dbIp       = configParser.getValue("database.ip", "");
    int         dbPort     = configParser.getIntValue("database.port", 0);
    std::string dbName     = configParser.getValue("database.name", "");
    std::string dbUser     = configParser.getValue("database.username", "");
    std::string dbPassword = configParser.getValue("database.password", "");

    ConfigurationMaster* dbConfig = ConfigurationMaster::getInstance();
    bool nseDbOk = false;
    bool bseDbOk = false;

    if (!dbIp.empty() && dbPort > 0 && !dbName.empty() &&
        dbConfig->initialize(dbIp, dbPort, dbName, dbUser, dbPassword)) {
        LOG_INFO("NSE database connected successfully (" + dbName + ")");
        nseDbOk = true;
    } else {
        LOG_ERROR("NSE database connection failed or not configured.");
    }

    // Initialize BSE database connection from config.ini [database-BSE]
    std::string dbIpBSE       = configParser.getValue("database-BSE.ip", "");
    int         dbPortBSE     = configParser.getIntValue("database-BSE.port", 0);
    std::string dbNameBSE     = configParser.getValue("database-BSE.name", "");
    std::string dbUserBSE     = configParser.getValue("database-BSE.username", "");
    std::string dbPasswordBSE = configParser.getValue("database-BSE.password", "");

    if (!dbIpBSE.empty() && dbPortBSE > 0 && !dbNameBSE.empty() &&
        dbConfig->initializeBSE(dbIpBSE, dbPortBSE, dbNameBSE, dbUserBSE, dbPasswordBSE)) {
        LOG_INFO("BSE database connected successfully (" + dbNameBSE + ")");
        bseDbOk = true;
    } else {
        LOG_ERROR("BSE database connection failed or not configured. Orders for BSE exchange will be rejected.");
    }

    if (!nseDbOk && !bseDbOk) {
        LOG_FATAL("Unable to initialise any database (NSE and BSE both failed). "
                  "No orders can be processed. Shutting down server.");
        Logger::shutdown();
        return 1;
    }

    // Start the server
    if (!server.start()) {
        LOG_ERROR("Failed to start server");
        Logger::shutdown();
        return 1;
    }
    
    LOG_INFO("Server started successfully");
    
    // Connect to CMS if configured
    if (!server.getCmsHost().empty() && server.getCmsPort() > 0) {
        LOG_INFO("Attempting to connect to CMS...");
        if (server.connectToCms()) {
            LOG_INFO("Connected to CMS successfully");
            server.startCmsReceiveLoop();
        } else {
            LOG_WARN("Failed to connect to CMS. Server will run without CMS connection.");
        }
    }
    
    // Wait for user input to stop
    server.waitForTermination();
    
    // Stop receive loop and disconnect from CMS
    server.stopCmsReceiveLoop();
    if (server.isCmsConnected()) {
        server.disconnectFromCms();
    }
    
    // Stop the server
    server.stop();
    
    LOG_INFO("Server shutdown complete");
    
    // Shutdown logger before exit
    Logger::shutdown();
    
    return 0;
}
