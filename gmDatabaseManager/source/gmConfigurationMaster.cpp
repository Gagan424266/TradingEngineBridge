#include "gmConfigurationMaster.hpp"
#include "Logger.h"

#include <cctype>
#include <string>
#include <stdexcept>
#include <vector>

gmConfigurationMaster *gmConfigurationMaster::instance = nullptr;

gmConfigurationMaster::gmConfigurationMaster() {}

gmConfigurationMaster::~gmConfigurationMaster()
{
    if (dbManager != nullptr)
    {
        delete dbManager;
        dbManager = nullptr;
    }
    if (dbManagerBSE != nullptr)
    {
        delete dbManagerBSE;
        dbManagerBSE = nullptr;
    }
}

gmConfigurationMaster *gmConfigurationMaster::getInstance()
{
    if (instance == nullptr)
    {
        instance = new gmConfigurationMaster();
    }
    return instance;
}

bool gmConfigurationMaster::initialize(const std::string &ip, int port, const std::string &dbName,
                                       const std::string &userName, const std::string &password)
{
    if (initialized)
        return true;

    dbServerIP   = ip;
    dbServerPort = port;
    this->dbName = dbName;
    dbUserName   = userName;
    dbPassword   = password;

    if (dbServerPort <= 0)
    {
        LOG_ERROR("gmConfigurationMaster::initialize - invalid port: " + std::to_string(dbServerPort));
        return false;
    }

    try
    {
        dbManager   = new PostgresDBManager(dbServerIP, dbServerPort, this->dbName, dbUserName, dbPassword);
        initialized = true;
        LOG_INFO("gmConfigurationMaster::initialize - connected to "
                 + dbServerIP + ":" + std::to_string(dbServerPort) + "/" + this->dbName);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("gmConfigurationMaster::initialize - connection failed: " + std::string(e.what()));
    }

    // Load TradingView strategy config cache once at startup.
    if (initialized)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!refreshTradingviewStrategyToClientDetailsLocked(true))
        {
            LOG_ERROR("gmConfigurationMaster::initialize - TradingView config cache load failed. "
                      "Server startup must abort (missing tradingviewStrategyConfig / tradingviewConfigToStrategy).");
            initialized = false;
            return false;
        }
    }

    return initialized;
}

bool gmConfigurationMaster::initializeBSE(const std::string &ip, int port, const std::string &dbName,
                                           const std::string &userName, const std::string &password)
{
    if (initializedBSE)
        return true;

    dbServerIPBSE   = ip;
    dbServerPortBSE = port;
    dbNameBSE       = dbName;
    dbUserNameBSE   = userName;
    dbPasswordBSE   = password;

    if (dbServerPortBSE <= 0)
    {
        LOG_ERROR("gmConfigurationMaster::initializeBSE - invalid port: " + std::to_string(dbServerPortBSE));
        return false;
    }

    try
    {
        dbManagerBSE   = new PostgresDBManager(dbServerIPBSE, dbServerPortBSE, dbNameBSE, dbUserNameBSE, dbPasswordBSE);
        initializedBSE = true;
        LOG_INFO("gmConfigurationMaster::initializeBSE - connected to "
                 + dbServerIPBSE + ":" + std::to_string(dbServerPortBSE) + "/" + dbNameBSE);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("gmConfigurationMaster::initializeBSE - connection failed: " + std::string(e.what()));
    }

    return initializedBSE;
}

PostgresDBManager *gmConfigurationMaster::getDbForExchange(const std::string &exchange) const
{
    if (exchange == "BSE" || exchange == "bse") {
        if (initializedBSE && dbManagerBSE != nullptr)
            return dbManagerBSE;
        return nullptr;
    }
    return dbManager;
}

bool gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked(bool force)
{
    if (!initialized || dbManager == nullptr)
    {
        LOG_ERROR("gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked - not initialized");
        return false;
    }

    // Startup-only mode: cache never expires.
    if (tradingviewStrategyToClientDetails.loaded && !force)
        return true;

    TradingviewStrategyToClientDetails next;

    const std::string query =
        "SELECT c.strategyconfigname AS strategyconfigname, "
        "c.strategyconfigid AS strategyconfigid, "
        "m.clientid AS clientid, m.strategyid AS strategyid, m.quantity AS quantity "
        "FROM public.tradingviewStrategyConfig c "
        "LEFT JOIN public.tradingviewConfigToStrategy m ON m.strategyconfigid = c.strategyconfigid "
        "ORDER BY c.strategyconfigname";

    auto cmd = dbManager->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked - JOIN query failed");
        return false;
    }

    try
    {
        while (cmd->FetchNext())
        {
            const std::string name =
                cmd->Field(_TSA("strategyconfigname")).asString().GetMultiByteChars();
            if (name.empty())
                continue;

            const int strategyConfigId =
                static_cast<int>(cmd->Field(_TSA("strategyconfigid")).asInt64());

            TradingviewStrategyClientOrderBundle &bundle = next.nameToDetails[name];
            bundle.strategyConfigId = strategyConfigId;

            if (cmd->Field(_TSA("clientid")).isNull())
                continue;

            OrderConfigRow row;
            const std::string clientIdStr =
                cmd->Field(_TSA("clientid")).asString().GetMultiByteChars();
            const std::string strategyIdStr =
                cmd->Field(_TSA("strategyid")).asString().GetMultiByteChars();
            row.clientId   = std::stoi(clientIdStr);
            row.strategyId = std::stoi(strategyIdStr);
            row.quantity   = static_cast<int>(cmd->Field(_TSA("quantity")).asInt64());
            bundle.orderRows.push_back(row);
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked - SAException: "
                  + std::string((const char *)e.ErrText()));
        return false;
    }
    catch (const std::invalid_argument &e)
    {
        LOG_ERROR("gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked - non-numeric id: "
                  + std::string(e.what()));
        return false;
    }

    next.loaded = true;

    tradingviewStrategyToClientDetails = std::move(next);
    LOG_INFO("gmConfigurationMaster::refreshTradingviewStrategyToClientDetailsLocked - loaded "
             + std::to_string(tradingviewStrategyToClientDetails.nameToDetails.size())
             + " strategy name(s) from JOIN");
    return true;
}

std::vector<OrderConfigRow> gmConfigurationMaster::getCachedOrderConfigRowsByStrategyName(
    const std::string &strategyName, int *outStrategyConfigId)
{
    if (outStrategyConfigId) *outStrategyConfigId = -1;

    std::lock_guard<std::mutex> lock(cacheMutex);
    if (!refreshTradingviewStrategyToClientDetailsLocked(false))
        return {};

    auto it = tradingviewStrategyToClientDetails.nameToDetails.find(strategyName);
    if (it == tradingviewStrategyToClientDetails.nameToDetails.end())
        return {};

    if (outStrategyConfigId)
        *outStrategyConfigId = it->second.strategyConfigId;

    return it->second.orderRows;
}

SecurityMasterRow gmConfigurationMaster::getSecurityByDatafeedTicker(const std::string &datafeedTicker)
{
    SecurityMasterRow result;
    result.contractId = -1;

    if (!initialized)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByDatafeedTicker - not initialized");
        return result;
    }

    std::string escaped;
    for (char c : datafeedTicker)
    {
        if (c == '\'')
            escaped += "''";
        else
            escaped += c;
    }
    const std::string query =
        "SELECT contractid, datafeedticker FROM public.\"securitymaster\" "
        "WHERE datafeedticker = '" + escaped + "' "
        "ORDER BY contractid ASC LIMIT 1";

    LOG_INFO("gmConfigurationMaster::getSecurityByDatafeedTicker - query: " + query);
    auto cmd = dbManager->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByDatafeedTicker - query failed");
        return result;
    }

    try
    {
        if (cmd->FetchNext())
        {
            result.contractId = static_cast<int>(cmd->Field(1).asInt64());
            result.datafeedTicker = cmd->Field(2).asString().GetMultiByteChars();
            LOG_INFO("gmConfigurationMaster::getSecurityByDatafeedTicker - found contractId="
                     + std::to_string(result.contractId) + " datafeedTicker=" + result.datafeedTicker
                     + " for query " + datafeedTicker);
        }
        else
        {
            LOG_WARN("gmConfigurationMaster::getSecurityByDatafeedTicker - no row found for "
                     + datafeedTicker);
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByDatafeedTicker - SAException: "
                  + std::string((const char *)e.ErrText()));
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// Equity lookup: NSE uses series='EQ', BSE uses instrument_type
// ────────────────────────────────────────────────────────────────────────────
SecurityMasterRow gmConfigurationMaster::getSecurityEquity(
    const std::string &symbol, const std::string &exchange)
{
    SecurityMasterRow result;
    result.contractId = -1;

    if (!initialized)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityEquity - not initialized");
        return result;
    }

    PostgresDBManager *db = getDbForExchange(exchange);
    if (db == nullptr)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityEquity - " + exchange + " database not connected");
        return result;
    }

    std::string escaped;
    for (char c : symbol)
    {
        if (c == '\'') escaped += "''";
        else           escaped += c;
    }

    const std::string query =
        "SELECT contractid, datafeedticker FROM public.\"securitymaster\" "
        "WHERE asset_code = '" + escaped + "' "
        "AND securitytype in (0,4) "
        "ORDER BY contractid ASC LIMIT 1";

    LOG_INFO("gmConfigurationMaster::getSecurityEquity - query: " + query);
    auto cmd = db->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityEquity - query failed");
        return result;
    }

    try
    {
        if (cmd->FetchNext())
        {
            result.contractId     = static_cast<int>(cmd->Field(1).asInt64());
            result.datafeedTicker = cmd->Field(2).asString().GetMultiByteChars();
            LOG_INFO("gmConfigurationMaster::getSecurityEquity - found contractId="
                     + std::to_string(result.contractId) + " for " + symbol
                     + " exchange=" + exchange);
        }
        else
        {
            LOG_WARN("gmConfigurationMaster::getSecurityEquity - no row for "
                     + symbol + " exchange=" + exchange);
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityEquity - SAException: "
                  + std::string((const char *)e.ErrText()));
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// Futures lookup by symbol + expiry month + expiry year
// Uses instrument_type for both NSE (FUTSTK/FUTIDX) and BSE (SF/IF)
// ────────────────────────────────────────────────────────────────────────────
SecurityMasterRow gmConfigurationMaster::getSecurityByFuturesLookup(
    const std::string &symbol, int expiryMonth, int expiryYear,
    const std::string &exchange)
{
    SecurityMasterRow result;
    result.contractId = -1;

    if (!initialized)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByFuturesLookup - not initialized");
        return result;
    }

    PostgresDBManager *db = getDbForExchange(exchange);
    if (db == nullptr)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByFuturesLookup - " + exchange + " database not connected");
        return result;
    }

    std::string escaped;
    for (char c : symbol)
    {
        if (c == '\'') escaped += "''";
        else           escaped += c;
    }

    const std::string query =
        "SELECT contractid, datafeedticker FROM public.\"securitymaster\" "
        "WHERE asset_code = '" + escaped + "' "
        "AND (instrument_type LIKE '%FUT%' OR instrument_type IN ('SF','IF')) "
        "AND EXTRACT(MONTH FROM maturitydate) = " + std::to_string(expiryMonth) + " "
        "AND EXTRACT(YEAR FROM maturitydate) = " + std::to_string(expiryYear) + " "
        "and securitytype in (1,5) "
        "ORDER BY maturitydate ASC LIMIT 1";

    LOG_INFO("gmConfigurationMaster::getSecurityByFuturesLookup - query: " + query);
    auto cmd = db->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByFuturesLookup - query failed");
        return result;
    }

    try
    {
        if (cmd->FetchNext())
        {
            result.contractId     = static_cast<int>(cmd->Field(1).asInt64());
            result.datafeedTicker = cmd->Field(2).asString().GetMultiByteChars();
            LOG_INFO("gmConfigurationMaster::getSecurityByFuturesLookup - found contractId="
                     + std::to_string(result.contractId) + " for " + symbol
                     + " " + std::to_string(expiryMonth) + "/" + std::to_string(expiryYear));
        }
        else
        {
            LOG_WARN("gmConfigurationMaster::getSecurityByFuturesLookup - no row for "
                     + symbol + " " + std::to_string(expiryMonth) + "/" + std::to_string(expiryYear));
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByFuturesLookup - SAException: "
                  + std::string((const char *)e.ErrText()));
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// Front-month futures: nearest unexpired futures contract for a symbol
// ────────────────────────────────────────────────────────────────────────────
SecurityMasterRow gmConfigurationMaster::getSecurityFrontMonthFutures(const std::string &symbol,
                                                                       const std::string &exchange,
                                                                       int nthMonth)
{
    SecurityMasterRow result;
    result.contractId = -1;

    if (!initialized)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityFrontMonthFutures - not initialized");
        return result;
    }

    if (nthMonth < 1) nthMonth = 1;

    PostgresDBManager *db = getDbForExchange(exchange);
    if (db == nullptr)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityFrontMonthFutures - " + exchange + " database not connected");
        return result;
    }

    std::string escaped;
    for (char c : symbol)
    {
        if (c == '\'') escaped += "''";
        else           escaped += c;
    }

    const std::string query =
        "SELECT contractid, datafeedticker FROM public.\"securitymaster\" "
        "WHERE asset_code = '" + escaped + "' "
        "AND (instrument_type LIKE '%FUT%' OR instrument_type IN ('SF','IF')) "
        "AND maturitydate >= CURRENT_DATE "
        "and securitytype in (1,5) "
        "ORDER BY maturitydate ASC LIMIT 1 OFFSET " + std::to_string(nthMonth - 1);

    LOG_INFO("gmConfigurationMaster::getSecurityFrontMonthFutures - query: " + query);
    auto cmd = db->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityFrontMonthFutures - query failed");
        return result;
    }

    try
    {
        if (cmd->FetchNext())
        {
            result.contractId     = static_cast<int>(cmd->Field(1).asInt64());
            result.datafeedTicker = cmd->Field(2).asString().GetMultiByteChars();
            LOG_INFO("gmConfigurationMaster::getSecurityFrontMonthFutures - found contractId="
                     + std::to_string(result.contractId) + " for " + symbol);
        }
        else
        {
            LOG_WARN("gmConfigurationMaster::getSecurityFrontMonthFutures - no unexpired futures for "
                     + symbol);
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityFrontMonthFutures - SAException: "
                  + std::string((const char *)e.ErrText()));
    }

    return result;
}

// ────────────────────────────────────────────────────────────────────────────
// Options lookup by symbol + maturitydate + optiontype + strike_price
// strike_price is numeric(20,6) in the DB — integer comparison still works
// ────────────────────────────────────────────────────────────────────────────
SecurityMasterRow gmConfigurationMaster::getSecurityByOptionComponents(
    const std::string &symbol, const std::string &maturityDate,
    const std::string &optionType, int strikePrice,
    const std::string &exchange)
{
    SecurityMasterRow result;
    result.contractId = -1;

    if (!initialized)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByOptionComponents - not initialized");
        return result;
    }

    PostgresDBManager *db = getDbForExchange(exchange);
    if (db == nullptr)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByOptionComponents - " + exchange + " database not connected");
        return result;
    }

    std::string escSym, escDate, escOpt;
    for (char c : symbol)       { if (c == '\'') escSym  += "''"; else escSym  += c; }
    for (char c : maturityDate) { if (c == '\'') escDate += "''"; else escDate += c; }
    for (char c : optionType)   { if (c == '\'') escOpt  += "''"; else escOpt  += c; }

    const std::string query =
        "SELECT contractid, datafeedticker FROM public.\"securitymaster\" "
        "WHERE asset_code = '" + escSym + "' "
        "AND maturitydate = '" + escDate + "' "
        "AND optiontype = '" + escOpt + "' "
        "AND strike_price = " + std::to_string(strikePrice) + " "
        "ORDER BY contractid ASC LIMIT 1";

    LOG_INFO("gmConfigurationMaster::getSecurityByOptionComponents - query: " + query);
    auto cmd = db->executeQuery(query);
    if (!cmd)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByOptionComponents - query failed");
        return result;
    }

    try
    {
        if (cmd->FetchNext())
        {
            result.contractId     = static_cast<int>(cmd->Field(1).asInt64());
            result.datafeedTicker = cmd->Field(2).asString().GetMultiByteChars();
            LOG_INFO("gmConfigurationMaster::getSecurityByOptionComponents - found contractId="
                     + std::to_string(result.contractId) + " for " + symbol + " " + maturityDate
                     + " " + optionType + " " + std::to_string(strikePrice));
        }
        else
        {
            LOG_WARN("gmConfigurationMaster::getSecurityByOptionComponents - no row for "
                     + symbol + " " + maturityDate + " " + optionType + " " + std::to_string(strikePrice));
        }
    }
    catch (const SAException &e)
    {
        LOG_ERROR("gmConfigurationMaster::getSecurityByOptionComponents - SAException: "
                  + std::string((const char *)e.ErrText()));
    }

    return result;
}
