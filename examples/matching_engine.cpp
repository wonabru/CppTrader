/*!
    \file matching_engine.cpp
    \brief Matching engine example
    \author Ivan Shynkarenka
    \date 16.08.2017
    \copyright MIT License
*/

#include "trader/matching/market_manager.h"
#include "trader/redis_db.h"
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
    void start(const string& name)
    {
        _begin = std::chrono::steady_clock::now();
        _name = name;
    }
    void end()
    {
        _end = std::chrono::steady_clock::now();
        int64_t time_diff = std::chrono::duration_cast<std::chrono::nanoseconds> (_end - _begin).count();
        std::cout << _name << ": Time = " << time_diff  << "[ns]" << std::endl;
    }
};


class MyMarketHandler : public MarketHandler
{
public:

    uint64_t last_index(const string& type)
    {
        return _redis._get_last_index(type);
    }

    MyRedis _redis = MyRedis();
    CheckTime ct = CheckTime();
    unordered_map<uint32_t, Symbol> symbols = {};

protected:

    void onAddSymbol(const Symbol& symbol) override
    { 
        std::cout << "Add symbol: " << symbol << std::endl;
        string key = "symbol:";
        key += to_string(symbol.Id);
        std::unordered_map<std::string, std::string> val = {{"id", to_string(symbol.Id)}, 
                                                            {"name", symbol.Name},
                                                            {"Type", to_string((uint8_t)symbol.Type)},
                                                            {"Multiplier", to_string(symbol.Multiplier)}
                                                            }; 
        _redis._hmset_db(key, val);
        symbols[symbol.Id] = symbol;
        // printf("get: %s", _get_db(key).c_str());
    }
    void onDeleteSymbol(const Symbol& symbol) override
    { 
        std::cout << "Delete symbol: " << symbol << std::endl; 
        string key = "symbol:";
        key += to_string(symbol.Id);
        _redis._del_db(key);
        symbols.erase(symbol.Id);
    }

    void onAddOrderBook(const OrderBook& order_book) override
    { 
        std::cout << "Add order book: " << order_book << std::endl; 
    }

    uint64_t calc_mark_price(const OrderBook& order_book)
    {
        if (!order_book.bids().empty() && !order_book.asks().empty())
        {
            return (uint64_t)std::lround((order_book.best_ask()->Price + order_book.best_bid()->Price) / 2.0);
        }else{
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
        double Z = std::abs(fr);
        double c = fr * fr;
        if (!is_inverse)
        {
            Z *= mark_price; // vanilla
            c *= mark_price * mark_price;
        }else{
            Z /= index_price; // inverse
            c /= index_price * index_price;
        }
        return vector<double>{Z, c};
    }

    void mark_price_db(const OrderBook& order_book)
    {
        string key = "mark_price:";
        key += to_string(order_book.symbol().Id);

        auto _mark_price = calc_mark_price(order_book);
        auto _now = std::chrono::steady_clock::now().time_since_epoch();
        uint64_t _time = std::chrono::duration_cast<std::chrono::nanoseconds>(_now).count();
        key += ":" + to_string(_time);
        
        //TODO
        uint64_t _index_price = 100;
        if (_mark_price)
        {

            std::unordered_map<std::string, std::string> val = {{"Time", to_string(_time)}, 
                                                                {"mark_price", to_string(_mark_price)},
                                                                {"best_bid", to_string(order_book.best_bid()->Price)},
                                                                {"best_ask", to_string(order_book.best_ask()->Price)}
                                                                }; 
            _redis._hmset_db(key, val);

            key = "mark_prices:";
            key += to_string(order_book.symbol().Id);
            
            vector<uint64_t> vec = {_time};

            _redis._append_db(key, vec);

            funding_coeficient_db(order_book, _mark_price, _index_price);

        }
    }

    void funding_coeficient_db(const OrderBook& order_book, double mark_price, double index_price)
    {
        string key = "funding_coeficient:";
        key += to_string(order_book.symbol().Id);

        bool is_inverse = Symbol::IsInverse(order_book.symbol().Type);

        auto _now = std::chrono::steady_clock::now().time_since_epoch();
        uint64_t _time = std::chrono::duration_cast<std::chrono::nanoseconds>(_now).count();
        key += ":" + to_string(_time);
        
        if (mark_price)
        {
            auto _funding_coeficient = funding_coeficient(mark_price, index_price, is_inverse);

            std::unordered_map<std::string, std::string> val = {{"Time", to_string(_time)}, 
                                                                {"Z", to_string(_funding_coeficient[0])},
                                                                {"c", to_string(_funding_coeficient[1])}
                                                                };
            _redis._hmset_db(key, val);

            key = "funding_coeficients:";
            key += to_string(order_book.symbol().Id);
            
            vector<uint64_t> vec = {_time};

            _redis._append_db(key, vec);
        }
    }

    void onUpdateOrderBook(const OrderBook& order_book, bool top) override
    { 
        // std::cout << "Update order book: " << order_book << (top ? " - Top of the book!" : "") << std::endl;
        mark_price_db(order_book); 
    }
    // void onDeleteOrderBook(const OrderBook& order_book) override
    // { std::cout << "Delete order book: " << order_book << std::endl; }

    void onAddLevel(const OrderBook& order_book, const Level& level, bool top) override
    { 
        // std::cout << "Add level: " << level << (top ? " - Top of the book!" : "") << std::endl;
        if (top)
            mark_price_db(order_book);  
    }
    void onUpdateLevel(const OrderBook& order_book, const Level& level, bool top) override
    { 
        // std::cout << "Update level: " << level << (top ? " - Top of the book!" : "") << std::endl;
        if (top)
            mark_price_db(order_book);  
    }
    void onDeleteLevel(const OrderBook& order_book, const Level& level, bool top) override
    { 
        // std::cout << "Delete level: " << level << (top ? " - Top of the book!" : "") << std::endl; 
        if (top)
            mark_price_db(order_book); 
    }

    std::unordered_map<std::string, std::string> order_prep(const Order& order)
    {        
        stringstream side_str;
        side_str << order.Side;

        stringstream TimeInForce_str;
        TimeInForce_str << order.TimeInForce;

        stringstream Type_str;
        Type_str << order.Type;
        
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        std::unordered_map<std::string, std::string> val = {{"Id", to_string(order.Id)},
                                                            {"SymbolId", to_string(order.SymbolId)}, 
                                                            {"ExecutedQuantity", to_string(order.ExecutedQuantity)},
                                                            {"LeavesQuantity", to_string(order.LeavesQuantity)},
                                                            {"MaxVisibleQuantity", to_string(order.MaxVisibleQuantity)},
                                                            {"Price", to_string(order.Price)},
                                                            {"Quantity", to_string(order.Quantity)},
                                                            {"Side", side_str.str()},
                                                            {"Slippage", to_string(order.Slippage)},
                                                            {"StopPrice", to_string(order.StopPrice)},
                                                            {"TimeInForce", TimeInForce_str.str()},
                                                            {"TrailingDistance", to_string(order.TrailingDistance)},
                                                            {"TrailingStep", to_string(order.TrailingStep)},
                                                            {"Type", Type_str.str()},
                                                            {"Time", to_string(_time)},
                                                            {"AccountId", to_string(order.AccountId)}
                                                            };
        return val; 
    }

    std::unordered_map<std::string, std::string> position_prep(const Position& position)
    {        
        stringstream side_str;
        side_str << position.Side;
        
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        std::unordered_map<std::string, std::string> val = {{"Id", to_string(position.Id)},
                                                            {"SymbolId", to_string(position.SymbolId)},
                                                            {"Price", to_string(position.AvgEntryPrice)},
                                                            {"Quantity", to_string(position.Quantity)},
                                                            {"Side", side_str.str()},
                                                            {"Time", to_string(_time)},
                                                            {"AccountId", to_string(position.AccountId)},
                                                            {"Z", to_string(position.Z)},
                                                            {"C", to_string(position.C)},
                                                            {"Funding", to_string(position.Funding)},
                                                            {"MarkPrice", to_string(position.MarkPrice)},
                                                            {"IndexPrice", to_string(position.IndexPrice)},
                                                            {"RealizedPnL", to_string(position.RealizedPnL)},
                                                            {"UnrealizedPnL", to_string(position.UnrealizedPnL)}
                                                            };
        return val; 
    }

    void onAddOrder(const Order& order) override
    { 
        // std::cout << "Add order: " << order << std::endl; 

        // vec = _lrange_db(key);
        // for (std::vector<string>::iterator it = vec.begin() ; it != vec.end(); ++it)
        // {
        //     printf("orders: %s\n", (*it).c_str());
        // }

        string key = "order:";
        key += to_string(order.Id);

        auto val = order_prep(order);

        // ct.start("redis insert");
        _redis._hmset_db(key, val);
        // ct.end();
        key = "orders";
        vector<uint64_t> vec = {order.Id};

        // ct.start("redist list insert");
        _redis._append_db(key, vec);
        // ct.end();
    }

    void onUpdateOrder(const Order& order) override
    { 
        // std::cout << "Update order: " << order << std::endl;
        string key = "order:";
        key += to_string(order.Id);
        
        std::unordered_map<std::string, std::string> val;

        val = order_prep(order);
 
        _redis._hmset_db(key, val);

        // key = "orders";
        // vector<uint64_t> vec = {order.Id};

        // _redis._append_db(key, vec); 
    }
    void onDeleteOrder(const Order& order) override
    { 
        // std::cout << "Delete order: " << order << std::endl;
        string key = "order:";
        key += to_string(order.Id);
        _redis._del_db(key); 

        key = "del_order:";
        key += to_string(order.Id);
        
        std::unordered_map<std::string, std::string> val;

        val = order_prep(order);
        _redis._hmset_db(key, val);

        key = "del_orders";
        vector<uint64_t> vec = {order.Id};

        _redis._append_db(key, vec);

    }

    void onExecuteOrder(const Order& order, uint64_t price, uint64_t quantity) override
    { 
        // std::cout << "Execute order: " << order << std::endl;
        string key = "execute:";
        key += to_string(order.Id);

        auto val = order_prep(order);

        val["CurrentExecutedPrice"] = price;
        val["CurrentExecutedQuantity"] = quantity;

        _redis._hmset_db(key, val);
        key = "executes";
        vector<uint64_t> vec = {order.Id};

        _redis._append_db(key, vec);

        key = "position:";
        key += to_string(order.AccountId);
        key += ":" + to_string(order.SymbolId);

        std::unordered_map<std::string, std::string> data = _redis._hgetall_db(key);
        Position last_pos, curr_pos;
        last_pos = last_pos.ReadDbStructure(data);
        curr_pos = last_pos.OrderExecuted(last_pos, order, price, quantity, symbols[order.SymbolId]);
        val = position_prep(curr_pos);

        _redis._hmset_db(key, val);

        key = "positions";
        vector<string> vec_str = {val["Time"]};

        _redis._append_db(key, vec_str);
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

void AddOrderBook(MarketManager& market, const std::string& command)
{
    static std::regex pattern("^add book (\\d+ \\d+ \\d+)$");
    std::smatch match;

    if (std::regex_search(command, match, pattern))
    {
        uint32_t id = std::stoi(match[1]);
        SymbolType type = SymbolType(std::stoi(match[2]));
        uint64_t multiplier = std::stoi(match[3]);
        char name[8];
        std::memset(name, 0, sizeof(name));

        Symbol symbol(id, name, type, multiplier);

        ErrorCode result = market.AddOrderBook(symbol);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add book' command: " << result << std::endl;

        return;
    }

    std::cerr << "Invalid 'add book' command: " << command << std::endl;
}

int main(int argc, char** argv)
{
    MyMarketHandler market_handler = MyMarketHandler();
    MarketManager market(market_handler);
    int id = market_handler.last_index("orders");
    uint64_t account_id;
    cout << "id: " << id << endl;
    int price;
    int quantity;
    // long start_time;
    long txn_no = 1000;
    Order order;
    ErrorCode result;

    uint32_t idSymbol = 1;
    char name[8]{"BTCUSDT"};

    SymbolType type = SymbolType::VANILLAPERP;
    uint64_t multiplier = 2;

    Symbol symbol(idSymbol, name, type, multiplier);

    result = market.AddSymbol(symbol);
    if (result != ErrorCode::OK)
        std::cerr << "Failed 'add symbol' command: " << result << std::endl;

    result = market.AddOrderBook(symbol);
    if (result != ErrorCode::OK)
        std::cerr << "Failed 'add book' command: " << result << std::endl;
    
    
    market.EnableMatching();

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();

    for (int i=0;i<txn_no;i++)
    {
        
        price = (int)(rand() * 100.0 / RAND_MAX);
        quantity = (int)(rand() * 100.0 / RAND_MAX) + 1;
        order = Order::BuyLimit(id, 1, price, quantity);
        account_id = (int)(rand() * 10.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        price = 200 - (int)(rand() * 100.0 / RAND_MAX);
        quantity = (int)(rand() * 100.0 / RAND_MAX) + 1;
        order = Order::SellLimit(id, 1, price, quantity);
        account_id = (int)(rand() * 10.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        // price = 200 - (int)(rand() * 100.0 / RAND_MAX);
        quantity = (int)(rand() * 100.0 / RAND_MAX) + 1;
        order = Order::BuyMarket(id, 1, quantity);
        account_id = (int)(rand() * 10.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
        quantity = (int)(rand() * 100.0 / RAND_MAX) + 1;
        order = Order::SellMarket(id, 1, quantity);
        account_id = (int)(rand() * 10.0 / RAND_MAX);
        order.AccountId = account_id;
        result = market.AddOrder(order);
        if (result != ErrorCode::OK)
            std::cerr << "Failed 'add limit' command: " << result << std::endl;
        id++;
    }
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

    int64_t time_diff = std::chrono::duration_cast<std::chrono::nanoseconds> (end - begin).count();
    std::cout << "Time difference = " << time_diff / 4.0 / txn_no  << "[ns]" << std::endl;
    cout << "TPS: " << txn_no * 4.0 / time_diff * 1e9 << endl;

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