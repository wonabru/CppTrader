/*!
    \file symbol.inl
    \brief Symbol inline implementation
    \author Chris Urbanowicz
    \date 22.01.2022
    \copyright MIT License
*/



namespace CppTrader {
namespace Matching {

inline Symbol::Symbol(uint32_t id, const char name[8], const SymbolType type, uint64_t multiplier, uint64_t qtyDivisor) noexcept
    : Id(id), 
      Type(type),
      Multiplier(multiplier),
      QuantityDivisor(qtyDivisor)

{
    std::memcpy(Name, name, sizeof(Name));
}

template <class TOutputStream>
inline TOutputStream& operator<<(TOutputStream& stream, const Symbol& symbol)
{
    stream << "Symbol(Id=" << symbol.Id
        << "; Name=" << CppCommon::WriteString(symbol.Name)
        << ")";
    return stream;
}

inline bool Symbol::IsInverse(SymbolType type) noexcept
{
    return (uint32_t)type >= 10;
}

inline Symbol Symbol::GetSymbolById(uint32_t id) noexcept
{
    std::string key = "symbol:";
    key += std::to_string(id);
    Symbol symbol = Symbol();
    MyRedis _redis = MyRedis();

    std::unordered_map<std::string, std::string> data = _redis._hgetall_db(key);
    symbol = symbol.ReadDbStructure(data);
    return symbol;
}

inline Symbol Symbol::ReadDbStructure(std::unordered_map<std::string, std::string> data) noexcept
{
    if (data.empty())
    {
        return Symbol();
    }
    SymbolType type = SymbolType(std::stoi(data["Type"]));
    Symbol symbol = Symbol(std::stoul(data["Id"]), 
                           data["Name"].c_str(),
                           type);
    return symbol;
}

} // namespace Matching
} // namespace CppTrader
