#include "../include/SymbolConverter.hpp"
#include "../include/Logger.h"
#include <unordered_map>
#include <cctype>
#include <cstdio>
#include <iostream>

namespace SymbolConverter {

static const std::unordered_map<char, int> TV_MONTH_CODE_TO_NUM = {
    {'F', 1},  {'G', 2},  {'H', 3},  {'J', 4},  {'K', 5},  {'M', 6},
    {'N', 7},  {'Q', 8},  {'U', 9},  {'V', 10}, {'X', 11}, {'Z', 12}
};

// ═══════════════════════════════════════════════════════════════════════════
// Detect instrument type from symbol pattern
// ═══════════════════════════════════════════════════════════════════════════

Instrument detectInstrumentType(const std::string& sym) {
    if (sym.empty()) return Instrument::UNKNOWN;

    if (sym.size() >= 3 && sym[sym.size() - 1] == '!' &&
        std::isdigit(static_cast<unsigned char>(sym[sym.size() - 2])))
        return Instrument::FUTURES_CONTINUOUS;

    {
        size_t i = sym.size();
        while (i > 0 && std::isdigit(static_cast<unsigned char>(sym[i - 1])))
            --i;
        size_t strikeDigits = sym.size() - i;
        if (strikeDigits >= 1 && i > 0) {
            char cp = sym[i - 1];
            if ((cp == 'C' || cp == 'P') && (i - 1) >= 6) {
                bool sixDigits = true;
                for (size_t j = i - 1 - 6; j < i - 1; ++j)
                    if (!std::isdigit(static_cast<unsigned char>(sym[j]))) { sixDigits = false; break; }
                if (sixDigits)
                    return Instrument::OPTIONS;
            }
        }
    }

    if (sym.size() >= 6) {
        bool last4Digits = true;
        for (size_t j = sym.size() - 4; j < sym.size(); ++j)
            if (!std::isdigit(static_cast<unsigned char>(sym[j]))) { last4Digits = false; break; }
        if (last4Digits) {
            char mc = sym[sym.size() - 5];
            if (TV_MONTH_CODE_TO_NUM.count(mc))
                return Instrument::FUTURES_INDIVIDUAL;
        }
    }

    return Instrument::EQUITY;
}

// ═══════════════════════════════════════════════════════════════════════════
// Parse TradingView symbol into components
// ═══════════════════════════════════════════════════════════════════════════

ParsedSymbol parseSymbol(const std::string& symbol, const std::string& exchange) {
    ParsedSymbol p;
    p.exchange  = exchange;
    p.tvSymbol  = symbol;
    p.instrument = detectInstrumentType(symbol);

    switch (p.instrument) {

    case Instrument::EQUITY: {
        p.rootSymbol = symbol;
        p.valid = true;
        break;
    }

    case Instrument::FUTURES_CONTINUOUS: {
        p.continuousN = symbol[symbol.size() - 2] - '0';
        p.rootSymbol = symbol.substr(0, symbol.size() - 2);
        p.valid = true;
        break;
    }

    case Instrument::FUTURES_INDIVIDUAL: {
        std::string yearStr = symbol.substr(symbol.size() - 4, 4);
        char monthCode      = symbol[symbol.size() - 5];
        p.rootSymbol        = symbol.substr(0, symbol.size() - 5);

        auto mcIt = TV_MONTH_CODE_TO_NUM.find(monthCode);
        if (mcIt == TV_MONTH_CODE_TO_NUM.end()) {
            p.error = "Invalid futures month code: " + std::string(1, monthCode);
            return p;
        }
        p.futMonth = mcIt->second;
        p.futYear  = std::stoi(yearStr);
        p.valid = true;
        break;
    }

    case Instrument::OPTIONS: {
        size_t i = symbol.size();

        std::string strikeStr;
        while (i > 0 && std::isdigit(static_cast<unsigned char>(symbol[i - 1]))) {
            strikeStr = symbol[i - 1] + strikeStr;
            --i;
        }
        if (strikeStr.empty() || i == 0) { p.error = "No strike price found"; return p; }

        p.optType = symbol[i - 1];
        --i;
        if (p.optType != 'C' && p.optType != 'P') { p.error = "Invalid option type"; return p; }

        if (i < 6) { p.error = "Symbol too short for date extraction"; return p; }
        p.optDD = std::stoi(symbol.substr(i - 2, 2));  i -= 2;
        p.optMM = std::stoi(symbol.substr(i - 2, 2));  i -= 2;
        p.optYY = std::stoi(symbol.substr(i - 2, 2));  i -= 2;
        p.strike = std::stoi(strikeStr);

        p.rootSymbol = symbol.substr(0, i);
        if (p.rootSymbol.empty()) { p.error = "Empty root symbol"; return p; }
        p.valid = true;
        break;
    }

    default:
        p.error = "Unknown instrument type";
        break;
    }

    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
// Instrument type as string (for logging)
// ═══════════════════════════════════════════════════════════════════════════

const char* instrumentTypeStr(Instrument t) {
    switch (t) {
        case Instrument::EQUITY:              return "EQUITY";
        case Instrument::FUTURES_CONTINUOUS:   return "FUTURES_CONTINUOUS";
        case Instrument::FUTURES_INDIVIDUAL:   return "FUTURES_INDIVIDUAL";
        case Instrument::OPTIONS:             return "OPTIONS";
        default:                              return "UNKNOWN";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Resolve contractId from securitymaster
// ═══════════════════════════════════════════════════════════════════════════

ResolveResult resolveContractId(const std::string& symbol, const std::string& exchange) {
    ResolveResult result;
    result.parsed = parseSymbol(symbol, exchange);
    const ParsedSymbol& p = result.parsed;

    if (!p.valid) {
        result.error = "Parse failed: " + p.error;
        LOG_ERROR("SymbolConverter: " + result.error +
                  " [symbol=" + symbol + " exchange=" + exchange + "]");
        return result;
    }

    ConfigurationMaster* db = ConfigurationMaster::getInstance();

    switch (p.instrument) {

    case Instrument::EQUITY: {
        LOG_INFO("SymbolConverter: EQUITY symbol=" + p.rootSymbol +
                 " exchange=" + p.exchange);

        SecurityMasterRow row = db->getSecurityEquity(p.rootSymbol, p.exchange);
        if (row.contractId >= 0) {
            result.contractId    = row.contractId;
            result.datafeedTicker = row.datafeedTicker;
        } else {
            result.error = "Equity '" + p.rootSymbol + "' not found";
        }
        break;
    }

    case Instrument::OPTIONS: {
        int fullYear = 2000 + p.optYY;
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", fullYear, p.optMM, p.optDD);
        std::string maturityDate(dateBuf);
        std::string optStr = (p.optType == 'C') ? "CE" : "PE";

        LOG_INFO("SymbolConverter: OPTIONS symbol=" + p.rootSymbol +
                 " maturity=" + maturityDate +
                 " type=" + optStr +
                 " strike=" + std::to_string(p.strike));

        SecurityMasterRow row = db->getSecurityByOptionComponents(
            p.rootSymbol, maturityDate, optStr, p.strike, p.exchange);
        if (row.contractId >= 0) {
            result.contractId    = row.contractId;
            result.datafeedTicker = row.datafeedTicker;
        } else {
            result.error = "Option not found: " + p.rootSymbol
                         + " " + maturityDate + " " + optStr
                         + " " + std::to_string(p.strike);
        }
        break;
    }

    case Instrument::FUTURES_INDIVIDUAL: {
        LOG_INFO("SymbolConverter: FUTURES symbol=" + p.rootSymbol +
                 " month=" + std::to_string(p.futMonth) +
                 " year=" + std::to_string(p.futYear));

        SecurityMasterRow row = db->getSecurityByFuturesLookup(
            p.rootSymbol, p.futMonth, p.futYear, p.exchange);
        if (row.contractId >= 0) {
            result.contractId    = row.contractId;
            result.datafeedTicker = row.datafeedTicker;
        } else {
            result.error = "Futures not found: " + p.rootSymbol
                         + " " + std::to_string(p.futMonth)
                         + "/" + std::to_string(p.futYear);
        }
        break;
    }

    case Instrument::FUTURES_CONTINUOUS: {
        LOG_INFO("SymbolConverter: CONTINUOUS N=" + std::to_string(p.continuousN) +
                 " symbol=" + p.rootSymbol);

        SecurityMasterRow row = db->getSecurityFrontMonthFutures(
            p.rootSymbol, p.exchange, p.continuousN);
        if (row.contractId >= 0) {
            result.contractId    = row.contractId;
            result.datafeedTicker = row.datafeedTicker;
        } else {
            result.error = "Front-month futures not found: " + p.rootSymbol;
        }
        break;
    }

    default:
        result.error = "Unhandled instrument type";
        break;
    }

    if (result.contractId >= 0) {
        LOG_INFO("SymbolConverter: RESOLVED TV=" + symbol +
                 " exchange=" + exchange +
                 " type=" + std::string(instrumentTypeStr(p.instrument)) +
                 " contractId=" + std::to_string(result.contractId) +
                 " datafeedTicker=" + result.datafeedTicker);
    } else {
        LOG_ERROR("SymbolConverter: FAILED " + result.error);
    }

    return result;
}

}  // namespace SymbolConverter
