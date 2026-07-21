#ifndef JSON_HANDLER_HPP
#define JSON_HANDLER_HPP

#include <string>
#include <map>
#include <vector>
#include "TradingViewPacket.hpp"

class JsonHandler {
private:
    // Helper methods
    std::string escapeJsonString(const std::string& str) const;
    
public:
    // Constructor & Destructor
    JsonHandler();
    ~JsonHandler();
    
    // JSON parsing methods
    // Returns empty string if valid JSON object; returns a descriptive error message otherwise
    std::string validateJsonSyntax(const std::string& json) const;
    bool hasField(const std::string& json, const std::string& field) const;
    std::string extractString(const std::string& json, const std::string& field) const;
    int extractInt(const std::string& json, const std::string& field) const;
    double extractDouble(const std::string& json, const std::string& field) const;
    
    // JSON building methods
    std::string createResponse(const std::string& name, int value, const std::string& timestamp) const;
    std::string createErrorResponse(const std::string& error) const;
    std::string createErrorResponse(const std::vector<std::string>& errors) const;
    std::string createHealthResponse(const std::string& timestamp) const;
    std::string createMessageResponse(const std::string& message) const;

    // CMS packet methods (wrapped for CMS protocol)
    CmsWrappedPacket createCmsWrappedPacket(const std::string& instrumentId, const std::string& side,
                                            double limitPrice, int quantity, const std::string& exchange,
                                            const std::string& clientTimestamp,
                                            int contractId, int strategyId,
                                            int clientId, int dealerId,
                                            const std::string& orderType = "limit") const;
    std::string formatCmsPacketForLog(const CmsWrappedPacket& packet) const;
    
    // Generic JSON builder
    std::string buildJson(const std::map<std::string, std::string>& fields) const;
};

#endif // JSON_HANDLER_HPP
