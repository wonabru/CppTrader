/*!
    \file matching_engine.cpp
    \brief Matching engine example
    \author Ivan Shynkarenka
    \date 16.08.2017
    \copyright MIT License
*/
#define ORDER_INT_MAX 100000000UL

#include "trader/matching/market_manager.h"
#include "trader/kdbp_db.h"
#include "trader/risk/position.h"
#include "system/stream.h"
#include "trader/matching/symbol.h"

#include <iostream>
#include <thread>
#include <regex>
#include <string>
#include <chrono>



using namespace CppTrader::Matching;
using namespace CppTrader::Risk;
using namespace std;

class CheckTime
{
private:
    std::chrono::steady_clock::time_point _begin;
    std::chrono::steady_clock::time_point _end;
    string _name = "";

public:
    void start(const string &name)
    {
        _begin = std::chrono::steady_clock::now();
        _name = name;
    }
    void end()
    {
        _end = std::chrono::steady_clock::now();
        int64_t time_diff = std::chrono::duration_cast<std::chrono::nanoseconds>(_end - _begin).count();
        std::cout << _name << ": Time = " << time_diff << "[ns]" << std::endl;
    }
};

static I handleOk(I handle)
{
    if(handle > 0)
        return 1;
    if(handle == 0)
        fprintf(stderr, "Authentication error %d\n", handle);
    else if(handle == -1)
        fprintf(stderr, "Connection error %d\n", handle);
    else if(handle == -2)
        fprintf(stderr, "Timeout error %d\n", handle);
    return 0;
}

class MyMarketHandler : public MarketHandler
{
public:
    uint64_t last_index(const string &type)
    {
        //should be last index of rows
        return 1;
    }

    Kdbp _kdb;
    CheckTime ct = CheckTime();
    unordered_map<uint32_t, Symbol> symbols = {};
    MyMarketHandler(I kdb): _kdb(Kdbp(kdb)){}
    MyMarketHandler() noexcept = delete;
    void createTables(const Symbol &symbol)
    {

        string _query = "meta symbols:([] Time:`long$(); Id:`short$(); Name:`symbol$(); Type:`short$(); Multiplier:`int$())";

        _kdb.executeQuery(_query);

        _query = "meta prices:([] Time:`long$(); SymbolId:`short$(); MarkPrice:`long$(); IndexPrice:`long$(); BestBid:`long$(); BestAsk:`long$(); RiskZ:`float$(); RiskC:`float$())";

        _kdb.executeQuery(_query);

        _query = "meta orders:([] Id:`long$(); SymbolId:`short$(); ExecutedQuantity:`long$(); LeavesQuantity:`long$(); MaxVisibleQuantity:`long$(); ";
        _query += "Price:`long$(); Quantity:`long$(); Side:`short$(); Slippage:`long$(); StopPrice:`long$(); TimeInForce:`short$(); TrailingDistance:`long$(); ";
        _query += "TrailingStep:`long$(); Type:(); Time:`long$(); AccountId:`long$(); CurrentExecutedPrice:`long$(); CurrentExecutedQuantity:`long$())";
        _kdb.executeQuery(_query);

        _query = "meta transactions:([] Id:`long$(); SymbolId:`short$(); ExecutedQuantity:`long$(); LeavesQuantity:`long$(); MaxVisibleQuantity:`long$(); ";
        _query += "Price:`long$(); Quantity:`long$(); Side:`short$(); Slippage:`long$(); StopPrice:`long$(); TimeInForce:`short$(); TrailingDistance:`long$(); ";
        _query += "TrailingStep:`long$(); Type:(); Time:`long$(); AccountId:`long$(); CurrentExecutedPrice:`long$(); CurrentExecutedQuantity:`long$())";
        _kdb.executeQuery(_query);

        _query = "meta positions:([] Id:`long$(); SymbolId:`short$(); AvgEntryPrice:`long$(); Quantity:`long$(); Side:`short$(); ";
        _query += "Time:`long$(); AccountId:`long$(); RiskZ:`float$(); RiskC:`float$(); Funding:`float$(); MarkPrice:`long$(); IndexPrice:`long$(); ";
        _query += "RealizedPnL:`float$(); UnrealizedPnL:`float$())";
        _kdb.executeQuery(_query);

    }

protected:
    void onAddSymbol(const Symbol &symbol) override
    {
        std::cout << "Add symbol: " << symbol << std::endl;
        time_t currentTime;
        struct tm *ct;
        string _query = "insert";
        string _table = "symbols";
        time(&currentTime);
        ct = localtime(&currentTime);
        K row = knk(5,
                    kj(_kdb.castTime(ct)),
                    kh(symbol.Id),
                    ks(S(symbol.Name)),
                    kh((uint8_t)symbol.Type),
                    ki(symbol.Multiplier));

        _kdb.insertRow(_query, _table, row);
        symbols[symbol.Id] = symbol;
    }

    void onDeleteSymbol(const Symbol &symbol) override
    {
        std::cout << "Delete symbol: " << symbol << std::endl;
        string _query = "delete";
        string _table = "symbol";
        // _kdb.deleteRow(_query, _table, row);
        symbols.erase(symbol.Id);
    }

    void onAddOrderBook(const OrderBook &order_book) override
    {
        std::cout << "Add order book: " << order_book << std::endl;
    }

    uint64_t calc_mark_price(const OrderBook &order_book)
    {
        if (!order_book.bids().empty() && !order_book.asks().empty())
        {
            return (uint64_t)std::lround((order_book.best_ask()->Price + order_book.best_bid()->Price) / 2.0);
        }
        else
        {
            return 0;
        }
    }

    double funding_rate(uint64_t mark_price, uint64_t index_price, bool is_inverse)
    {
        double fr;
        if (!is_inverse)
        {
            fr = (mark_price - index_price) * 1.0 / index_price; // vanilla
        }
        else
        {
            fr = (index_price - mark_price) * 1.0 / mark_price; // inverse
        }
        return fr;
    }

    vector<double> funding_coeficient(uint64_t mark_price, uint64_t index_price, bool is_inverse)
    {
        double fr = funding_rate(mark_price, index_price, is_inverse);
        double riskZ = std::abs(fr);
        double riskC = fr * fr;
        if (!is_inverse)
        {
            riskZ *= mark_price; // vanilla
            riskC *= mark_price * mark_price;
        }
        else
        {
            riskZ /= index_price; // inverse
            riskC /= index_price * index_price;
        }
        return vector<double>{riskZ, riskC};
    }

    void mark_price_db(const OrderBook &order_book)
    {
        string _table = "prices";
        string _query = "insert";
        // ct.start("mark price calc");
        auto _mark_price = calc_mark_price(order_book);
        // ct.end();
        auto _now = std::chrono::steady_clock::now().time_since_epoch();
        uint64_t _time = std::chrono::duration_cast<std::chrono::nanoseconds>(_now).count();

        //TODO
        uint64_t _index_price = 10000;
        if (_mark_price)
        {
            // ct.start("funding calc");
            bool is_inverse = Symbol::IsInverse(order_book.symbol().Type);

            auto _funding_coeficient = funding_coeficient(_mark_price, _index_price, is_inverse);

            // ct.end();
            // ct.start("price insert");
            K row = knk(8,
                        kj(_time),
                        kh(order_book.symbol().Id),
                        kj(_mark_price),
                        kj(_index_price),
                        kj(order_book.best_bid()->Price),
                        kj(order_book.best_ask()->Price),
                        kf(_funding_coeficient[0]),
                        kf(_funding_coeficient[1]));

            _kdb.insertRow(_query, _table, row);
            // ct.end();
        }
    }

    void onUpdateOrderBook(const OrderBook &order_book, bool top) override
    {
        // std::cout << "Update order book: " << order_book << (top ? " - Top of the book!" : "") << std::endl;
        mark_price_db(order_book);
    }
    // void onDeleteOrderBook(const OrderBook& order_book) override
    // { std::cout << "Delete order book: " << order_book << std::endl; }

    void onAddLevel(const OrderBook &order_book, const Level &level, bool top) override
    {
        // std::cout << "Add level: " << level << (top ? " - Top of the book!" : "") << std::endl;
        if (top)
            mark_price_db(order_book);
    }
    void onUpdateLevel(const OrderBook &order_book, const Level &level, bool top) override
    {
        // std::cout << "Update level: " << level << (top ? " - Top of the book!" : "") << std::endl;
        if (top)
            mark_price_db(order_book);
    }
    void onDeleteLevel(const OrderBook &order_book, const Level &level, bool top) override
    {
        // std::cout << "Delete level: " << level << (top ? " - Top of the book!" : "") << std::endl;
        if (top)
            mark_price_db(order_book);
    }

    K order_prep(const Order &order, uint64_t currentExecutedPrice=0, uint64_t currentExecutedQuantity=0)
    {

        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        K val = knk(18,
                    kj(order.Id),
                    kh(order.SymbolId),
                    kj(order.ExecutedQuantity),
                    kj(order.LeavesQuantity),
                    kj(order.MaxVisibleQuantity),
                    kj(order.Price),
                    kj(order.Quantity),
                    kh((uint8_t)order.Side),
                    kj(order.Slippage),
                    kj(order.StopPrice),
                    kh((uint8_t)order.TimeInForce),
                    kj(order.TrailingDistance),
                    kj(order.TrailingStep),
                    kh((uint8_t)order.Type),
                    kj(_time),
                    kj(order.AccountId),
                    kj(currentExecutedPrice),
                    kj(currentExecutedQuantity));
        return val;
    }

    K position_prep(const Position &position)
    {

        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        K val = knk(14,
                    kj(position.Id),
                    kh(position.SymbolId),
                    kj(position.AvgEntryPrice),
                    kj(position.Quantity),
                    kh((uint8_t)position.Side),
                    kj(_time),
                    kj(position.AccountId),
                    kf(position.RiskZ),
                    kf(position.RiskC),
                    kf(position.Funding),
                    kj(position.MarkPrice),
                    kj(position.IndexPrice),
                    kf(position.RealizedPnL),
                    kf(position.UnrealizedPnL));
        return val;
    }

    void onAddOrder(const Order &order) override
    {
        // std::cout << "Add order: " << order << std::endl;

        string _table = "orders";
        string _query = "insert";

        auto val = order_prep(order);

        // ct.start("order insert");
        _kdb.insertRow(_query, _table, val);
        // ct.end();
    }

    void onUpdateOrder(const Order &order) override
    {
        // std::cout << "Update order: " << order << std::endl;
        string _table = "orders";
        char *_query = new char[600];
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        sprintf(_query, 
        "TimeInForce=%d, TrailingDistance=%lu, TrailingStep=%lu, Type=%d, Time=%lu, ExecutedQuantity=%lu, LeavesQuantity=%lu, MaxVisibleQuantity=%lu, Price=%lu, Quantity=%lu, Side=%d, Slippage=%lu, StopPrice=%lu",
        (uint8_t)order.TimeInForce, 
        order.TrailingDistance, 
        order.TrailingStep,
        (uint8_t)order.Type,
        _time,
        order.ExecutedQuantity,
        order.LeavesQuantity,
        order.MaxVisibleQuantity,
        order.Price,
        order.Quantity,
        (uint8_t)order.Side,
        order.Slippage,
        order.StopPrice);

        char *_query2 = new char[600];

        sprintf(_query2, "update %s from %s where Id=%lu", _query, _table.c_str(), order.Id);
        _kdb.executeQuery(_query2);
    }

    void onDeleteOrder(const Order &order) override
    {
        // std::cout << "Delete order: " << order << std::endl;
        // string _table = "orders";
        // string _query = "delete";

        // K val = order_prep(order);

        // ct.start("order delete");
        // _kdb.insertRow(_query, _table, val);
        // ct.end();
    }

    void onExecuteOrder(const Order &order, uint64_t price, uint64_t quantity) override
    {
        // std::cout << "Execute order: " << order << std::endl;
        string _table = "transactions";
        string _query = "insert";

        auto val = order_prep(order, price, quantity);

        _kdb.insertRow(_query, _table, val);

        _table = "positions";
        char *_query2 = new char[600];
        sprintf(_query2, "select [-1] from %s where AccountId=%lu and SymbolId=%u", _table.c_str(), order.AccountId, order.SymbolId);
        //Id, SymbolId, Side, AvgEntryPrice, Quantity, AccountId, MarkPrice, IndexPrice, RiskZ, RiskC, Funding, RealizedPnL, UnrealizedPnL by Time
        K data = _kdb.readQuery(_query2);
        // _kdb.printq(data);

        Position last_pos, curr_pos;
        last_pos = last_pos.ReadDbStructure(data, _kdb);
        curr_pos = last_pos.OrderExecuted(last_pos, order, price, quantity, symbols[order.SymbolId]);
        val = position_prep(curr_pos);
        _table = "positions";
        _query = "insert";
        _kdb.insertRow(_query, _table, val);

    }
};

// void AddSymbol(MarketManager& market, const std::string& command)
// {
//     static std::regex pattern("^add symbol (\\d+) (.+)$ (\\d+) (\\d+)");
//     std::smatch match;

//     if (std::regex_search(command, match, pattern))
//     {
//         uint32_t id = std::stoi(match[1]);

//         char name[8];
//         std::string sname = match[2];
//         std::memcpy(name, sname.data(), std::min(sname.size(), sizeof(name)));
//         SymbolType type = SymbolType(std::stoi(match[3]));
//         uint64_t multiplier = std::stoul(match[4]);

//         Symbol symbol(id, name, type, multiplier);

//         ErrorCode result = market.AddSymbol(symbol);
//         if (result != ErrorCode::OK)
//             std::cerr << "Failed 'add symbol' command: " << result << std::endl;

//         return;
//     }

//     std::cerr << "Invalid 'add symbol' command: " << command << std::endl;
// }

// void DeleteSymbol(MarketManager& market, const std::string& command)
// {
//     static std::regex pattern("^delete symbol (\\d+)$");
//     std::smatch match;

//     if (std::regex_search(command, match, pattern))
//     {
//         uint32_t id = std::stoi(match[1]);

//         ErrorCode result = market.DeleteSymbol(id);
//         if (result != ErrorCode::OK)
//             std::cerr << "Failed 'delete symbol' command: " << result << std::endl;

//         return;
//     }

//     std::cerr << "Invalid 'delete symbol' command: " << command << std::endl;
// }

// void AddOrderBook(MarketManager& market, const std::string& command)
// {
//     static std::regex pattern("^add book (\\d+ \\d+ \\d+)$");
//     std::smatch match;

//     if (std::regex_search(command, match, pattern))
//     {
//         uint32_t id = std::stoi(match[1]);
//         SymbolType type = SymbolType(std::stoi(match[2]));
//         uint64_t multiplier = std::stoi(match[3]);
//         char name[8];
//         std::memset(name, 0, sizeof(name));

//         Symbol symbol(id, name, type, multiplier);

//         ErrorCode result = market.AddOrderBook(symbol);
//         if (result != ErrorCode::OK)
//             std::cerr << "Failed 'add book' command: " << result << std::endl;

//         return;
//     }

//     std::cerr << "Invalid 'add book' command: " << command << std::endl;
// }

int main(int argc, char **argv)
{
    I kdb = khpu(S("127.0.0.1"), I(5000), S(":"));
    if(!handleOk(kdb))
        return 1;
    MyMarketHandler market_handler = MyMarketHandler(kdb);
    MarketManager market(market_handler);
    int id = market_handler.last_index("orders");
    uint64_t account_id;
    cout << "id: " << id << endl;
    int price;
    int quantity;
    // long start_time;
    long txn_no = 10000;
    Order order;
    ErrorCode result;

    uint32_t idSymbol = 1;
    char name[8]{"BTCUSDT"};

    SymbolType type = SymbolType::VANILLAPERP;
    uint64_t multiplier = 1;

    Symbol symbol(idSymbol, name, type, multiplier);
    market_handler.createTables(symbol);
    result = market.AddSymbol(symbol);
    if (result != ErrorCode::OK)
        std::cerr << "Failed 'add symbol' command: " << result << std::endl;

    result = market.AddOrderBook(symbol);
    if (result != ErrorCode::OK)
        std::cerr << "Failed 'add book' command: " << result << std::endl;

    market.EnableMatching();

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    for (int i = 0; i < txn_no; i++)
    {

        price = (int)(rand() * 10000.0 / RAND_MAX);
        quantity = (int)(rand() * 1000.0 / RAND_MAX) + 1;
        order = Order::BuyLimit(id, 1, price, quantity);
        account_id = (int)(rand() * 1.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        price = 10000 - (int)(rand() * 10000.0 / RAND_MAX);
        quantity = (int)(rand() * 1000.0 / RAND_MAX) + 1;
        order = Order::SellLimit(id, 1, price, quantity);
        account_id = (int)(rand() * 1.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        // price = 200 - (int)(rand() * 100.0 / RAND_MAX);
        quantity = (int)(rand() * 10.0 / RAND_MAX) + 1;
        order = Order::BuyMarket(id, 1, quantity);
        account_id = (int)(rand() * 1.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        quantity = (int)(rand() * 10.0 / RAND_MAX) + 1;
        order = Order::SellMarket(id, 1, quantity);
        account_id = (int)(rand() * 1.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    int64_t time_diff = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
    cout << "No of orders send: " << txn_no * 4 << endl;
    std::cout << "Time difference = " << time_diff / 4.0 / txn_no << "[ns]" << std::endl;
    cout << "TPS: " << txn_no * 4.0 / time_diff * 1e9 << endl;
    CppTrader::Matching::MarketManager::OrderBooks ob = market.order_books();
    if (ob.size())
    {
        cout << ob.size() << endl;
        auto ob_btcusdt = ob[1];
        cout << "Bids level size: " << ob_btcusdt->bids().size() << "; Asks level size: " << ob_btcusdt->asks().size() << endl;
    }
    kclose(kdb);
    return 0;
}

/*

void DeleteOrderBook(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^delete book (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint32_t id = std::stoi(match[1]);

        ErrorCode result = market.DeleteOrderBook(id);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'delete book' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'delete book' command: " << command << std::endl;
}

void AddMarketOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add market (buy|sell) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t quantity = std::stoi(match[4]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyMarket(id, symbol_id, quantity);
        else if (match[1] == "sell")
            order = Order::SellMarket(id, symbol_id, quantity);
        else
        {
            std::cerr << "Invalid market order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add market' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add market' command: " << command << std::endl;
}

void AddSlippageMarketOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add slippage market (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t quantity = std::stoi(match[4]);
        uint64_t slippage = std::stoi(match[5]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyMarket(id, symbol_id, quantity, slippage);
        else if (match[1] == "sell")
            order = Order::SellMarket(id, symbol_id, quantity, slippage);
        else
        {
            std::cerr << "Invalid market order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add slippage market' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add slippage market' command: " << command << std::endl;
}

void AddLimitOrder(MarketManager& market,
                    int is_buy,
                    uint64_t id,
                    uint32_t symbol_id,
                    uint64_t price,
                    uint64_t quantity)
{

        Order order;
        if (is_buy == 1)
            order = Order::BuyLimit(id, symbol_id, price, quantity);
        else if (is_buy == 0)
            order = Order::SellLimit(id, symbol_id, price, quantity);
        else
        {
            std::cerr << "Invalid limit order side: " << is_buy << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;

        return;
}

void AddIOCLimitOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add ioc limit (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t price = std::stoi(match[4]);
        uint64_t quantity = std::stoi(match[5]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyLimit(id, symbol_id, price, quantity, OrderTimeInForce::IOC);
        else if (match[1] == "sell")
            order = Order::SellLimit(id, symbol_id, price, quantity, OrderTimeInForce::IOC);
        else
        {
            std::cerr << "Invalid limit order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add ioc limit' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add ioc limit' command: " << command << std::endl;
}

void AddFOKLimitOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add fok limit (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t price = std::stoi(match[4]);
        uint64_t quantity = std::stoi(match[5]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyLimit(id, symbol_id, price, quantity, OrderTimeInForce::FOK);
        else if (match[1] == "sell")
            order = Order::SellLimit(id, symbol_id, price, quantity, OrderTimeInForce::FOK);
        else
        {
            std::cerr << "Invalid limit order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add fok limit' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add fok limit' command: " << command << std::endl;
}

void AddAONLimitOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add aon limit (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t price = std::stoi(match[4]);
        uint64_t quantity = std::stoi(match[5]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyLimit(id, symbol_id, price, quantity, OrderTimeInForce::AON);
        else if (match[1] == "sell")
            order = Order::SellLimit(id, symbol_id, price, quantity, OrderTimeInForce::AON);
        else
        {
            std::cerr << "Invalid limit order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add aon limit' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add aon limit' command: " << command << std::endl;
}

void AddStopOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add stop (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t stop_price = std::stoi(match[4]);
        uint64_t quantity = std::stoi(match[5]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyStop(id, symbol_id, stop_price, quantity);
        else if (match[1] == "sell")
            order = Order::SellStop(id, symbol_id, stop_price, quantity);
        else
        {
            std::cerr << "Invalid stop order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add stop' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add stop' command: " << command << std::endl;
}

void AddStopLimitOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add stop-limit (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t stop_price = std::stoi(match[4]);
        uint64_t price = std::stoi(match[5]);
        uint64_t quantity = std::stoi(match[6]);

        Order order;
        if (match[1] == "buy")
            order = Order::BuyStopLimit(id, symbol_id, stop_price, price, quantity);
        else if (match[1] == "sell")
            order = Order::SellStopLimit(id, symbol_id, stop_price, price, quantity);
        else
        {
            std::cerr << "Invalid stop-limit order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add stop-limit' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add stop-limit' command: " << command << std::endl;
}

void AddTrailingStopOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add trailing stop (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t stop_price = std::stoi(match[4]);
        uint64_t quantity = std::stoi(match[5]);
        int64_t trailing_distance = std::stoi(match[6]);
        int64_t trailing_step = std::stoi(match[7]);

        Order order;
        if (match[1] == "buy")
            order = Order::TrailingBuyStop(id, symbol_id, stop_price, quantity, trailing_distance, trailing_step);
        else if (match[1] == "sell")
            order = Order::TrailingSellStop(id, symbol_id, stop_price, quantity, trailing_distance, trailing_step);
        else
        {
            std::cerr << "Invalid stop order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add trailing stop' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add trailing stop' command: " << command << std::endl;
}

void AddTrailingStopLimitOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add trailing stop-limit (buy|sell) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[2]);
        uint32_t symbol_id = std::stoi(match[3]);
        uint64_t stop_price = std::stoi(match[4]);
        uint64_t price = std::stoi(match[5]);
        uint64_t quantity = std::stoi(match[6]);
        int64_t trailing_distance = std::stoi(match[7]);
        int64_t trailing_step = std::stoi(match[8]);

        Order order;
        if (match[1] == "buy")
            order = Order::TrailingBuyStopLimit(id, symbol_id, stop_price, price, quantity, trailing_distance, trailing_step);
        else if (match[1] == "sell")
            order = Order::TrailingSellStopLimit(id, symbol_id, stop_price, price, quantity, trailing_distance, trailing_step);
        else
        {
            std::cerr << "Invalid stop-limit order side: " << match[1] << std::endl;
            return;
        }

        ErrorCode result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add trailing stop-limit' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add trailing stop-limit' command: " << command << std::endl;
}

void ReduceOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^reduce order (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[1]);
        uint64_t quantity = std::stoi(match[2]);

        ErrorCode result = market.ReduceOrder(id, quantity);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'reduce order' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'reduce order' command: " << command << std::endl;
}

void ModifyOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^modify order (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[1]);
        uint64_t new_price = std::stoi(match[2]);
        uint64_t new_quantity = std::stoi(match[3]);

        ErrorCode result = market.ModifyOrder(id, new_price, new_quantity);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'modify order' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'modify order' command: " << command << std::endl;
}

void MitigateOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^mitigate order (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[1]);
        uint64_t new_price = std::stoi(match[2]);
        uint64_t new_quantity = std::stoi(match[3]);

        ErrorCode result = market.MitigateOrder(id, new_price, new_quantity);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'mitigate order' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'mitigate order' command: " << command << std::endl;
}

void ReplaceOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^replace order (\\d+) (\\d+) (\\d+) (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[1]);
        uint64_t new_id = std::stoi(match[2]);
        uint64_t new_price = std::stoi(match[3]);
        uint64_t new_quantity = std::stoi(match[4]);

        ErrorCode result = market.ReplaceOrder(id, new_id, new_price, new_quantity);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'replace order' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'replace order' command: " << command << std::endl;
}

void DeleteOrder(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^delete order (\\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint64_t id = std::stoi(match[1]);

        ErrorCode result = market.DeleteOrder(id);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'delete order' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'delete order' command: " << command << std::endl;
}

*/