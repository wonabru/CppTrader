/*!
    \file position.h
    \brief Position definition
    \author Chris Urbanowicz
    \date 22.01.2022
    \copyright MIT License
*/

#ifndef CPPTRADER_RISK_POSITION_H
#define CPPTRADER_RISK_POSITION_H

#include "../matching/errors.h"
#include "../matching/order.h"
#include "../matching/symbol.h"
#include "containers/list.h"
#include "utility/iostream.h"
#include "../kdb/c/c/k.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace CppTrader
{
    namespace Risk
    {

        //! Position side
        /*!
    Possible values:
    \li <b>Long</b>
    \li <b>Short</b>
*/
        enum class PositionSide : uint8_t
        {
            LONG,
            SHORT
        };

        template <class TOutputStream>
        TOutputStream &operator<<(TOutputStream &stream, PositionSide side);

        //! Position
        /*!

*/
        struct Position
        {
            //! Position Id
            uint64_t Id;
            //! Symbol Id
            uint32_t SymbolId;

            //! Position side
            PositionSide Side;
            //! Position price
            double AvgEntryPrice;

            //! Position quantity
            uint64_t Quantity;
            //! AccountId
            uint64_t AccountId;

            uint64_t MarkPrice;
            uint64_t IndexPrice;

            //funding coeficients
            double RiskZ;
            double RiskC;
            double Funding;

            double RealizedPnL;
            double UnrealizedPnL;

            uint64_t FundingTime;

            Position() noexcept = default;
            Position(uint64_t id, uint32_t symbol, PositionSide side, double price, uint64_t quantity,
                     uint64_t accountId, uint64_t markPrice, uint64_t indexPrice,
                     double z=0, double c=0, double funding=0, double realizedPnL=0,
                     double unrealizedPnL=0,uint64_t fundingTime=0UL) noexcept;
            Position(const Position &) noexcept = default;
            Position(Position &&) noexcept = default;
            ~Position() noexcept = default;

            Position &operator=(const Position &) noexcept = default;
            Position &operator=(Position &&) noexcept = default;

            template <class TOutputStream>
            friend TOutputStream &operator<<(TOutputStream &stream, const Position &position);
            static double* CalculatePnL(const Position &position, const CppTrader::Matching::Order &order, uint64_t price, uint64_t quantity, const CppTrader::Matching::Symbol &symbol) noexcept;
            static double CalculateFunding(const Position &position, const uint64_t timespan, const CppTrader::Matching::Symbol &symbol) noexcept;
            Position OrderExecuted(const Position &position, const CppTrader::Matching::Order &order, uint64_t price, uint64_t quantity, const CppTrader::Matching::Symbol &symbol) noexcept;
            Position ReadDbStructure(K data, Kdbp kdb) noexcept;
        };
    } // namespace risk
} // namespace CppTrader

#include "position.inl"

#endif // CPPTRADER_RISK_POSITION_H
