#ifndef WEB_SERVER_HPP
#define WEB_SERVER_HPP

#include <microhttpd.h>
#include <string>
#include "ConfigParser.hpp"
#include "JsonHandler.hpp"
#include "RequestHandler.hpp"
#include "TcpClient.hpp"

class WebServer {
private:
    ConfigParser* configParser;
    JsonHandler* jsonHandler;
    RequestHandler* requestHandler;
    TcpClient* tcpClient;
    struct MHD_Daemon* daemon;
    
    std::string ip;
    int port;
    std::string cmsHost;
    int cmsPort;
    int dealerId;
    bool isRunning;
    
    // Helper methods
    bool validateConfiguration();
    void printStartupInfo(const std::string& configFile);
    
public:
    // Constructor & Destructor
    WebServer();
    explicit WebServer(const std::string& configFile);
    ~WebServer();
    
    // Configuration methods
    bool loadConfiguration(const std::string& configFile);
    std::string getIp() const { return ip; }
    int getPort() const { return port; }
    std::string getCmsHost() const { return cmsHost; }
    int getCmsPort() const { return cmsPort; }
    int getDealerId() const { return dealerId; }
    
    // Server lifecycle methods
    bool start();
    void stop();
    void waitForTermination();
    bool isServerRunning() const { return isRunning; }
    
    // CMS connection methods
    bool connectToCms();
    bool disconnectFromCms();
    bool isCmsConnected() const;
    bool sendToCms(const std::string& message);
    std::string receiveFromCms(size_t maxBytes = 4096);

    // Start/stop background thread that reads CMS responses and logs them
    void startCmsReceiveLoop();
    void stopCmsReceiveLoop();
    
    // Utility methods
    void printInfo() const;
};

#endif // WEB_SERVER_HPP
