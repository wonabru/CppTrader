/*!
    \file position.h
    \brief Position definition
    \author Chris Urbanowicz
    \date 22.01.2022
    \copyright MIT License
*/

using namespace CppTrader::Matching;

namespace CppTrader {
namespace Risk {

template <class TOutputStream>
inline TOutputStream& operator<<(TOutputStream& stream, PositionSide side)
{
    switch (side)
    {
        case PositionSide::LONG:
            stream << "LONG";
            break;
        case PositionSide::SHORT:
            stream << "SHORT";
            break;
        default:
            stream << "<unknown>";
            break;
    }
    return stream;
}

inline Position::Position(uint64_t id, 
                          uint32_t symbol, 
                          PositionSide side, 
                          float price, 
                          uint64_t quantity,
                          uint64_t accountId, 
                          uint64_t markPrice, 
                          uint64_t indexPrice,
                          float z, 
                          float c, 
                          float funding, 
                          float realizedPnL,
                          float unrealizedPnL) noexcept
    : Id(id),
      SymbolId(symbol),
      Side(side),
      AvgEntryPrice(price),
      Quantity(quantity),
      AccountId(accountId),
      MarkPrice(markPrice),
      IndexPrice(indexPrice),
      RiskZ(z),
      RiskC(c),
      Funding(funding),
      RealizedPnL(realizedPnL),
      UnrealizedPnL(unrealizedPnL)
{
}

template <class TOutputStream>
inline TOutputStream& operator<<(TOutputStream& stream, const Position& position)
{
    stream << "Position(Id=" << position.Id
        << "; SymbolId=" << position.SymbolId
        << "; Side=" << position.Side
        << "; AvgEntryPrice=" << position.AvgEntryPrice
        << "; Quantity=" << position.Quantity
        << "; AccountId=" << position.AccountId
        << "; MarkPrice=" << position.MarkPrice
        << "; IndexPrice=" << position.IndexPrice
        << "; RiskZ=" << position.RiskZ
        << "; RiskC=" << position.RiskC
        << "; Funding=" << position.Funding
        << "; RealizedPnL=" << position.RealizedPnL
        << "; UnrealizedPnL=" << position.UnrealizedPnL;
    stream << ")";
    return stream;
}

inline double Position::CalculateFunding(const Position &position, const uint64_t timespan, const Symbol &symbol) noexcept
{
    double funding;
    double q_pos = (int64_t)(position.Side == PositionSide::LONG?position.Quantity:-position.Quantity);
    double div = (double)symbol.QuantityDivisor;
    // double mult = (double)symbol.Multiplier;
    double z = position.RiskZ;
    double c = position.RiskC;

    if (Symbol::IsInverse(symbol.Type))
    {
        //inverse futures
        funding = q_pos / div * c / z * timespan / 60000.0;
    }else{
        funding = q_pos / div * c / z * timespan / 60000.0;
    }

    return funding;
}

inline double* Position::CalculatePnL(const Position &position, const CppTrader::Matching::Order &order, uint64_t price, uint64_t quantity, const Symbol &symbol) noexcept
{
    double realized;
    double unrealized;
    double q = (int64_t)(order.Side == OrderSide::BUY?quantity:-quantity);
    double q_pos = (int64_t)(position.Side == PositionSide::LONG?position.Quantity:-position.Quantity);
    double div = (double)symbol.QuantityDivisor;
    double mult = (double)symbol.Multiplier;
    double avgEntryPrice;

    if (Symbol::IsInverse(symbol.Type))
    {
        //inverse futures
        double tmp = (q_pos / position.AvgEntryPrice + q / price );
        if (tmp && position.AvgEntryPrice)
            avgEntryPrice = (q_pos + q) / tmp;
        else
            avgEntryPrice = 1;
        unrealized = (q_pos + q) / div * (mult / avgEntryPrice - mult / price);
        if ((q_pos + q) * q_pos < 0)
            realized = q_pos / div * (mult / position.AvgEntryPrice - mult / price);
        else if (q_pos * q < 0)
            realized = q / div * (mult / position.AvgEntryPrice - mult / price);
        else
            realized = 0;

    }else{
        double tmp = (q_pos + q);
        if (tmp)
            avgEntryPrice = (q_pos * position.AvgEntryPrice + q * price) / tmp;
        else
            avgEntryPrice = 1;
        unrealized = (q_pos + q) / div * (price - avgEntryPrice) / mult;
        if ((q_pos + q) * q_pos < 0)
            realized = q_pos / div * (price - position.AvgEntryPrice) / mult;
        else if (q_pos * q < 0)
            realized = q / div * (price - position.AvgEntryPrice) / mult;
        else
            realized = 0; 

    }

    return new double[3]{realized, unrealized, avgEntryPrice};
}

inline Position Position::OrderExecuted(const Position &position, const CppTrader::Matching::Order &order, uint64_t price, uint64_t quantity, const Symbol &symbol) noexcept
{
    if (!quantity || price <= 0)
    {
        return position;
    }
    double * pnls;

    pnls = Position::CalculatePnL(position, order, price, quantity, symbol);

    Position pos = Position(position);
    int64_t q = order.Side == OrderSide::BUY?quantity:-quantity;
    int64_t q_pos = position.Side == PositionSide::LONG?position.Quantity:-position.Quantity;
    int64_t q_all = q + q_pos;
    pos.RealizedPnL += pnls[0];
    pos.UnrealizedPnL = pnls[1];
    pos.AvgEntryPrice = pnls[2];
    pos.Quantity = abs(q_all);
    pos.SymbolId = order.SymbolId;
    pos.AccountId = order.AccountId;
    pos.Side = q_all>=0?PositionSide::LONG:PositionSide::SHORT;

    if (q == -q_pos)
    {
        //TODO closing the position, update balance

    }
    return pos;
}

inline Position Position::ReadDbStructure(K data, Kdbp kdb) noexcept
{
    if (!data)
    {
        return Position();
    }
    K flip = ktd(data);
    // K columns = kK(flip->k)[0];
    K rows = kK(flip->k)[1];
    int rowcount = kK(rows)[0]->n;
    if (!rowcount)
    {
        return Position();
    }
    auto side = (uint8_t)kdb.getitem(kK(rows)[4], 0)->h;
    PositionSide _side = side?PositionSide::SHORT:PositionSide::LONG;

    auto pos = Position((uint64_t)kdb.getitem(kK(rows)[0], 0)->j, 
                        (uint32_t)kdb.getitem(kK(rows)[1], 0)->i, 
                        _side, 
                        (uint64_t)kdb.getitem(kK(rows)[2], 0)->j, 
                        (uint64_t)kdb.getitem(kK(rows)[3], 0)->j,
                        (uint64_t)kdb.getitem(kK(rows)[6], 0)->j, 
                        (uint64_t)kdb.getitem(kK(rows)[10], 0)->j, 
                        (uint64_t)kdb.getitem(kK(rows)[11], 0)->j,
                        (float)kdb.getitem(kK(rows)[7], 0)->f, 
                        (float)kdb.getitem(kK(rows)[8], 0)->f, 
                        (float)kdb.getitem(kK(rows)[9], 0)->f, 
                        (float)kdb.getitem(kK(rows)[12], 0)->f,
                        (float)kdb.getitem(kK(rows)[13], 0)->f);

    return pos;

}

} // namespace Risk
} // namespace CppTrader
