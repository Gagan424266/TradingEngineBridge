#pragma once

#include <string>

// One row from tradingviewConfigToStrategy
struct OrderConfigRow {
    int strategyId = 0;
    int clientId   = 0;
    int quantity   = 0;
};

// One row from securitymaster table (contractid, datafeedticker)
struct SecurityMasterRow {
    int contractId = -1;
    std::string datafeedTicker;
};
