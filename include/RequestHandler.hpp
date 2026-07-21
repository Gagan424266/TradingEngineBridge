#ifndef REQUEST_HANDLER_HPP
#define REQUEST_HANDLER_HPP

#include <microhttpd.h>
#include <string>
#include <vector>
#include "JsonHandler.hpp"
#include "TcpClient.hpp"
#include "ConfigurationMaster.hpp"

class RequestHandler {
private:
    JsonHandler* jsonHandler;
    TcpClient* tcpClient;
    int dealerId = -1;
    std::string authToken;
    
    // Helper methods
    std::string getCurrentTimestamp() const;
    enum MHD_Result handleGetHealth(struct MHD_Connection* connection);
    enum MHD_Result handlePostWebhook(struct MHD_Connection* connection, const std::string& body);
    enum MHD_Result handleGetRoot(struct MHD_Connection* connection);
    enum MHD_Result handleNotFound(struct MHD_Connection* connection);
    enum MHD_Result sendResponse(struct MHD_Connection* connection, const std::string& content, 
                                  int statusCode, const std::string& contentType);
    enum MHD_Result sendWebhookAck(struct MHD_Connection* connection);
    
public:
    // Constructor & Destructor
    RequestHandler();
    RequestHandler(JsonHandler* handler, TcpClient* client);
    ~RequestHandler();
    
    // CMS connection
    void setTcpClient(TcpClient* client);
    void setDealerId(int id);
    void setAuthToken(std::string token);
    int getDealerId() const { return dealerId; }
    
    // Main request handler
    enum MHD_Result handleRequest(struct MHD_Connection* connection,
                                  const char* url,
                                  const char* method,
                                  const char* upload_data,
                                  size_t* upload_data_size,
                                  void** con_cls);
    
    // Static callback wrapper for MHD
    static enum MHD_Result requestCallback(void* cls,
                                          struct MHD_Connection* connection,
                                          const char* url,
                                          const char* method,
                                          const char* version,
                                          const char* upload_data,
                                          size_t* upload_data_size,
                                          void** con_cls);
};

#endif // REQUEST_HANDLER_HPP
