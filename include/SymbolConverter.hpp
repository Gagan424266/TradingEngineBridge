#ifndef SYMBOL_CONVERTER_HPP
#define SYMBOL_CONVERTER_HPP

#include <string>
#include "ConfigurationMaster.hpp"

namespace SymbolConverter {

enum class Instrument {
    EQUITY,
    FUTURES_CONTINUOUS,
    FUTURES_INDIVIDUAL,
    OPTIONS,
    UNKNOWN
};

struct ParsedSymbol {
    std::string exchange;
    Instrument  instrument = Instrument::UNKNOWN;
    std::string tvSymbol;
    std::string rootSymbol;

    int  optYY    = 0;
    int  optMM    = 0;
    int  optDD    = 0;
    char optType  = 0;
    int  strike   = 0;

    int  futMonth = 0;
    int  futYear  = 0;

    int  continuousN = 1;  // Nth nearest contract for FUTURES_CONTINUOUS (1! = 1, 2! = 2, ...)

    bool valid = false;
    std::string error;
};

struct ResolveResult {
    int         contractId = -1;
    std::string datafeedTicker;
    std::string error;
    ParsedSymbol parsed;
};

Instrument detectInstrumentType(const std::string& symbol);
ParsedSymbol parseSymbol(const std::string& symbol, const std::string& exchange);
const char* instrumentTypeStr(Instrument t);
ResolveResult resolveContractId(const std::string& symbol, const std::string& exchange);

}  // namespace SymbolConverter

#endif
