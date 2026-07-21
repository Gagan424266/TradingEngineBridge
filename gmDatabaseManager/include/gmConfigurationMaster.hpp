#ifndef GMCONFIGURATIONMASTER_HPP_INCLUDE
#define GMCONFIGURATIONMASTER_HPP_INCLUDE

#pragma once

#include "PostgresDBManager.hpp"
#include <mutex>
#include <unordered_map>
#include <vector>
#include "gmStructs.hpp"

/**
 * Singleton class managing PostgreSQL database connections for gm_webserver.
 * Two connections: NSE (default) and BSE.
 * Strategy/order config always uses NSE DB.
 * Security master queries are routed by exchange.
 */
class gmConfigurationMaster
{
  private:
    bool initialized    = false;
    bool initializedBSE = false;
    PostgresDBManager *dbManager    = nullptr;
    PostgresDBManager *dbManagerBSE = nullptr;
    std::string dbServerIP, dbName, dbUserName, dbPassword;
    int dbServerPort = 0;
    std::string dbServerIPBSE, dbNameBSE, dbUserNameBSE, dbPasswordBSE;
    int dbServerPortBSE = 0;

    static gmConfigurationMaster *instance;
    gmConfigurationMaster();

    PostgresDBManager *getDbForExchange(const std::string &exchange) const;

    // TradingView strategy name -> client/strategy/qty rows (+ config id for logs).
    struct TradingviewStrategyClientOrderBundle {
        int strategyConfigId = -1;
        std::vector<OrderConfigRow> orderRows;
    };

    // Startup-loaded map: webhook strategy_name -> orders to fan out.
    struct TradingviewStrategyToClientDetails {
        bool loaded = false;
        std::unordered_map<std::string, TradingviewStrategyClientOrderBundle> nameToDetails;
    };
    mutable std::mutex cacheMutex;
    TradingviewStrategyToClientDetails tradingviewStrategyToClientDetails;

    bool refreshTradingviewStrategyToClientDetailsLocked(bool force);

  public:
    ~gmConfigurationMaster();

    static gmConfigurationMaster *getInstance();

    /**
     * Initializes the primary (NSE) DB connection.
     * Called from main.cpp after reading config.ini [database].
     */
    bool initialize(const std::string &ip, int port, const std::string &dbName,
                    const std::string &userName, const std::string &password);

    /**
     * Initializes the BSE DB connection.
     * Called from main.cpp after reading config.ini [database-BSE].
     */
    bool initializeBSE(const std::string &ip, int port, const std::string &dbName,
                       const std::string &userName, const std::string &password);

    // ── Cached API (recommended for webhook path) ────────────────────────────
    // Returns matching order rows for strategy name from startup-loaded cache.
    // If outStrategyConfigId != nullptr, it will be set to the resolved id (or -1).
    std::vector<OrderConfigRow> getCachedOrderConfigRowsByStrategyName(const std::string &strategyName,
                                                                       int *outStrategyConfigId = nullptr);

    /**
     * Looks up securitymaster by datafeedticker.
     * Returns contractid and datafeedticker for the matching row.
     *
     * @param datafeedTicker  The ticker to search for.
     * @return SecurityMasterRow with contractId and datafeedTicker; contractId is -1 if not found.
     */
    SecurityMasterRow getSecurityByDatafeedTicker(const std::string &datafeedTicker);

    SecurityMasterRow getSecurityEquity(const std::string &symbol,
                                        const std::string &exchange);

    SecurityMasterRow getSecurityByFuturesLookup(const std::string &symbol,
                                                  int expiryMonth, int expiryYear,
                                                  const std::string &exchange);

    SecurityMasterRow getSecurityFrontMonthFutures(const std::string &symbol,
                                                    const std::string &exchange,
                                                    int nthMonth = 1);

    SecurityMasterRow getSecurityByOptionComponents(const std::string &symbol,
                                                     const std::string &maturityDate,
                                                     const std::string &optionType,
                                                     int strikePrice,
                                                     const std::string &exchange);
};

#endif
