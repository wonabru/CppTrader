/*!
    \file symbol.h
    \brief Symbol definition
    \author Ivan Shynkarenka
    \date 31.07.2017
    \copyright MIT License
*/

#ifndef CPPTRADER_MATCHING_SYMBOL_H
#define CPPTRADER_MATCHING_SYMBOL_H

#include "utility/iostream.h"
#include "trader/redis_db.h"

#include <cstdint>
#include <cstring>

namespace CppTrader {
namespace Matching {

enum class SymbolType : uint8_t
{
    SPOT,
    VANILLAPERP,  
    VANNILAFUT,
    OPTIONVANILLAPERP,
    OPTIONVANILLAFUT,
    //Inverse >= 10
    INVERSEPERP = 10,
    INVERSEFUT,
    OPTIONINVERSEPERP,
    OPTIONINVERSEFUT
};
//! Symbol
struct Symbol
{
    //! Symbol Id
    uint32_t Id;
    //! Symbol name
    char Name[8];

    SymbolType Type;

    uint64_t Multiplier;

    uint64_t QuantityDividor;

    Symbol() noexcept = default;
    Symbol(uint32_t id, const char name[8], SymbolType type=SymbolType::SPOT, uint64_t multiplier = 1, uint64_t qtyDividor = 100) noexcept;
    Symbol(const Symbol&) noexcept = default;
    Symbol(Symbol&&) noexcept = default;
    ~Symbol() noexcept = default;

    Symbol& operator=(const Symbol&) noexcept = default;
    Symbol& operator=(Symbol&&) noexcept = default;

    template <class TOutputStream>
    friend TOutputStream& operator<<(TOutputStream& stream, const Symbol& symbol);
    static bool IsInverse(SymbolType type) noexcept;
    Symbol GetSymbolById(uint32_t id) noexcept;
    Symbol ReadDbStructure(std::unordered_map<std::string, std::string> data) noexcept;
};

} // namespace Matching
} // namespace CppTrader

#include "symbol.inl"

#endif // CPPTRADER_MATCHING_SYMBOL_H
