#ifndef TRADINGVIEW_PACKET_HPP
#define TRADINGVIEW_PACKET_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>

#pragma pack(push, 1)

// Must match CmsMessageHeader (OMS) in CMS (int32_t length + int16_t messageType = 6 bytes)
struct CmsMessageHeader {
    int32_t length;
    int16_t messageType;

    CmsMessageHeader(int32_t len, int16_t type) : length(len), messageType(type) {}
    CmsMessageHeader() : length(0), messageType(0) {}
};

// Must match CMS message type enum enum in CMS OMS API messages header
enum CmsMessageType : int16_t {
    CMS_TRADINGVIEW_ORDER = 4,  // CMSMESSAGE_WEBSERVER_ORDER = 4
};

// Must exactly mirror OMS WebServerOrder payload in CMS DbStructs.hpp (140 bytes)
// CMS calculates: numOrders = (header.length - 6) / sizeof(OMS WebServerOrder payload)
// So this struct must be binary-identical to OMS WebServerOrder payload
struct WebServerOrderPayload {
    char instrumentId[64];    
    char clientTimestamp[32]; 
    char exchange[16];        
    int32_t contractId;       
    int32_t strategyId;   
    int32_t clientId;         
    int32_t dealerId;   
    int32_t side;             
    int32_t price;            
    uint32_t quantity;        
    int32_t orderType;

    WebServerOrderPayload() {
        std::memset(this, 0, sizeof(WebServerOrderPayload));
        contractId = -1;
        strategyId = -1;
        clientId   = -1;
        dealerId   = -1;
        side       = -1;
        orderType  = 1;  // OT_LIMIT default
    }

    void setOrderType(const std::string& value) {
        if (value == "market" || value == "MARKET") {
            orderType = 0;
        } else if (value == "limit" || value == "LIMIT") {
            orderType = 1;
        } else {
            orderType = 1;  // default LIMIT
        }
    }

    void setInstrumentId(const std::string& value) {
        std::strncpy(instrumentId, value.c_str(), sizeof(instrumentId) - 1);
        instrumentId[sizeof(instrumentId) - 1] = '\0';
    }

    void setExchange(const std::string& value) {
        std::strncpy(exchange, value.c_str(), sizeof(exchange) - 1);
        exchange[sizeof(exchange) - 1] = '\0';
    }

    void setClientTimestamp(const std::string& value) {
        std::strncpy(clientTimestamp, value.c_str(), sizeof(clientTimestamp) - 1);
        clientTimestamp[sizeof(clientTimestamp) - 1] = '\0';
    }

    void setSide(const std::string& value) {
        if (value == "buy" || value == "BUY") {
            side = 0;
        } else if (value == "sell" || value == "SELL") {
            side = 1;
        } else {
            side = -1;
        }
    }
};

// Full packet: CmsMessageHeader (6 bytes) + WebServerOrderPayload (144 bytes) = 150 bytes
// CMS recv: numOrders = (header.length - 6) / sizeof(OMS WebServerOrder payload)
struct CmsWrappedPacket {
    CmsMessageHeader header;
    WebServerOrderPayload payload;

    CmsWrappedPacket() {
        header.length      = static_cast<int32_t>(sizeof(CmsWrappedPacket));
        header.messageType = CmsMessageType::CMS_TRADINGVIEW_ORDER;
    }

    size_t getPacketSize() const {
        return sizeof(CmsWrappedPacket);
    }

    const std::byte* getBytes() const {
        return reinterpret_cast<const std::byte*>(this);
    }
};

#pragma pack(pop)

// ── Incoming packets: CMS → Webserver ────────────────────────────────────────

// Message types CMS sends back (matches MessageType enum in CMS OMS API messages header)
enum CmsResponseMessageType : int16_t {
    CMS_RESPONSE_ORDER_UPDATE = 9,   // MessageType::OrderUpdate
    CMS_RESPONSE_ORDER_FILL  = 10,   // MessageType::OrderFill
};

// Mirrors OMS OrderUpdate in CMS DbStructs.hpp (__attribute__((packed)))
// Field types use fixed-width integers to be portable.
// long in CMS on Linux-64 = int64_t — same machine, safe.
#pragma pack(push, 1)
struct CmsOrderUpdatePayload {
    int32_t  strategyId;
    int32_t  omsId;
    int32_t  clientId;
    int32_t  orderId;
    int32_t  linkId;
    int32_t  contractId;
    int32_t  price;
    int32_t  quantity;
    int32_t  buyOrSell;
    int32_t  connectionId;
    int32_t  dealerId;
    int32_t  orderType;
    int32_t  orderValidity;
    int32_t  orderStatus;
    int32_t  requestStatus;
    int64_t  entryDateTime;
    int64_t  lastModified;
    int64_t  lastActivityReference;
    int32_t  errorCode;
    int64_t  exchangeOid;
    char     remarks[150];
    int32_t  orderSubType;
    int32_t  triggerPrice;

    static std::string statusName(int32_t s) {
        switch (s) {
            case -1: return "INVALID";
            case  0: return "SENT_NEW";
            case  1: return "SENT_MODIFY";
            case  2: return "SENT_CANCEL";
            case  3: return "NEW_REJECTED";
            case  4: return "MODIFY_REJECTED";
            case  5: return "CANCEL_REJECTED";
            case  6: return "ACCEPTED";
            case  7: return "MODIFIED";
            case  8: return "PARTIAL_FILL";
            case  9: return "CANCELLED";
            case 10: return "COMPLETE_FILL";
            case 11: return "TRIGGERED";
            case 12: return "NEW_REJECTED_INTERNALLY";
            case 13: return "MODIFY_REJECTED_INTERNALLY";
            case 14: return "CANCEL_REJECTED_INTERNALLY";
            default: return "UNKNOWN(" + std::to_string(s) + ")";
        }
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "orderId="      << orderId
            << " contractId="  << contractId
            << " strategyId="  << strategyId
            << " clientId="    << clientId
            << " dealerId="    << dealerId
            << " price="       << price
            << " qty="         << quantity
            << " buyOrSell="   << buyOrSell
            << " status="      << statusName(orderStatus)
            << " reqStatus="   << statusName(requestStatus)
            << " errorCode="   << errorCode
            << " omsId="       << omsId
            << " linkId="      << linkId;
        if (remarks[0] != '\0' && remarks[0] != '0') {
            oss << " remarks=" << remarks;
        }
        return oss.str();
    }
};
#pragma pack(pop)

// Mirrors OMS OrderFill in CMS DbStructs.hpp (__attribute__((packed)))
#pragma pack(push, 1)
struct CmsOrderFillPayload {
    int32_t  strategyId;
    int32_t  omsId;
    int32_t  clientId;
    int32_t  orderId;
    int32_t  orderStatus;
    int32_t  contractId;
    int32_t  fillPrice;
    int32_t  fillQuantity;
    int32_t  price;
    int32_t  quantity;
    int32_t  buyOrSell;
    int64_t  exchangeOid;
    int32_t  fillNumber;
    int64_t  entryDateTime;
    int64_t  lastModified;
    int64_t  lastActivityReference;
    int32_t  connectionId;
    int32_t  dealerId;
    int32_t  triggerPrice;
    int32_t  orderType;
    int32_t  orderValidity;
    int32_t  orderSubType;

    std::string toString() const {
        std::ostringstream oss;
        oss << "OrderFill orderId=" << orderId
            << " contractId=" << contractId
            << " strategyId=" << strategyId
            << " clientId=" << clientId
            << " dealerId=" << dealerId
            << " fillPrice=" << (fillPrice / 10000.0)
            << " fillQty=" << fillQuantity
            << " orderQty=" << quantity
            << " buyOrSell=" << (buyOrSell == 0 ? "BUY" : "SELL")
            << " status=" << CmsOrderUpdatePayload::statusName(orderStatus)
            << " fillNumber=" << fillNumber
            << " omsId=" << omsId;
        return oss.str();
    }
};
#pragma pack(pop)

#endif // TRADINGVIEW_PACKET_HPP
