/*!
    \file matching_engine.cpp
    \brief Matching engine example
    \author Ivan Shynkarenka
    \date 16.08.2017
    \copyright MIT License
*/
#define ORDER_INT_MAX 100000000UL
#define CHUNK_SIZE 10000UL
#define CLOCK_INTERVAL 10000UL

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

template <
    class result_t   = std::chrono::milliseconds,
    class clock_t    = std::chrono::steady_clock,
    class duration_t = std::chrono::milliseconds
>
auto since(std::chrono::time_point<clock_t, duration_t> const& start)
{
    return std::chrono::duration_cast<result_t>(clock_t::now() - start);
}

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

    uint64_t count_time;
    uint64_t count_orders_chunk;
    uint64_t count_transactions_chunk;
    uint64_t count_positions_chunk;
    
    Kdbp _kdb;
    CheckTime ct = CheckTime();
    unordered_map<uint32_t, Symbol> symbols = {};
    unordered_map<uint64_t, string> users = {};
    unordered_map<string, Position> usersStats = {};

    vector<Order> orders_chunk;
    vector<Order> transactions_chunk;
    vector<Position> positions_chunk;
    vector<uint64_t> executedPrice;
    vector<uint64_t> executedQuantity;

    uint64_t last_index(const string &table)
    {
        //should be last index of rows
        count_time = 0UL;
        count_orders_chunk = 0UL;
        count_transactions_chunk = 0UL;
        count_positions_chunk = 0UL;
        K count = _kdb.readQuery("count " + table);
        return count->j;
        
    }

    MyMarketHandler(I kdb): _kdb(Kdbp(kdb)){}
    MyMarketHandler() noexcept = delete;

    //First needs to be defined symbols
    void addUser(uint64_t accountId, string name)
    {
        users[accountId] = name;
        for (auto& it: symbols) {
            Position pos = Position();
            pos.Id = last_index("positions");
            pos.AccountId = accountId;
            pos.SymbolId = it.second.Id;

            usersStats[to_string(it.second.Id) + "+" + to_string(accountId)] = pos;
            count_positions_chunk = CHUNK_SIZE + 1;
            appendPositionsChunk("upsert", pos);
        }
    }

    void createTables(const Symbol &symbol)
    {

        string _query = "meta symbols:([] Time:`long$(); Id:`short$(); Name:`symbol$(); Type:`short$(); Multiplier:`int$())";

        _kdb.executeQuery(_query);

        _query = "meta prices:([] Time:`long$(); SymbolId:`short$(); MarkPrice:`long$(); IndexPrice:`long$(); BestBid:`long$(); BestAsk:`long$(); RiskZ:`float$(); RiskC:`float$())";

        _kdb.executeQuery(_query);

        _query = "meta orders:([Id#:`long$()] SymbolId:`short$(); ExecutedQuantity:`long$(); LeavesQuantity:`long$(); MaxVisibleQuantity:`long$(); ";
        _query += "Price:`long$(); Quantity:`long$(); Side:`short$(); Slippage:`long$(); StopPrice:`long$(); TimeInForce:`short$(); TrailingDistance:`long$(); ";
        _query += "TrailingStep:`long$(); Type:(); Time:`long$(); AccountId:`long$(); CurrentExecutedPrice:`long$(); CurrentExecutedQuantity:`long$())";
        _kdb.executeQuery(_query);

        _query = "meta transactions:([Id:`long$()] SymbolId:`short$(); ExecutedQuantity:`long$(); LeavesQuantity:`long$(); MaxVisibleQuantity:`long$(); ";
        _query += "Price:`long$(); Quantity:`long$(); Side:`short$(); Slippage:`long$(); StopPrice:`long$(); TimeInForce:`short$(); TrailingDistance:`long$(); ";
        _query += "TrailingStep:`long$(); Type:(); Time:`long$(); AccountId:`long$(); CurrentExecutedPrice:`long$(); CurrentExecutedQuantity:`long$())";
        _kdb.executeQuery(_query);

        _query = "meta positions:([Id#:`long$()] SymbolId:`short$(); AvgEntryPrice:`long$(); Quantity:`long$(); Side:`short$(); ";
        _query += "Time:`long$(); AccountId:`long$(); RiskZ:`float$(); RiskC:`float$(); Funding:`float$(); MarkPrice:`long$(); IndexPrice:`long$(); ";
        _query += "RealizedPnL:`float$(); UnrealizedPnL:`float$())";
        _kdb.executeQuery(_query);

    }

protected:
    void onAddSymbol(const Symbol &symbol) override
    {
        std::cout << "Add symbol: " << symbol << std::endl;
        time_t currentTime;
        struct tm *ctime;
        string _query = "insert";
        string _table = "symbols";
        time(&currentTime);
        ctime = localtime(&currentTime);
        K row = knk(5,
                    kj(_kdb.castTime(ctime)),
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
        if (++count_time < CLOCK_INTERVAL)
        {
            return;
        }
        count_time = 0UL;
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

    K order_prep(const vector<Order> &orders, vector<uint64_t> currentExecutedPrice, vector<uint64_t> currentExecutedQuantity)
    {
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::milliseconds(1));

        vector<K> lists({ktn(KJ, orders.size()),
                      ktn(KH, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KH, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KH, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KH, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size()),
                      ktn(KJ, orders.size())});
        for (uint64_t i=0;i<orders.size();i++)
        {
            kJ(lists[0])[i] = orders[i].Id;
            kH(lists[1])[i] = orders[i].SymbolId;
            kJ(lists[2])[i] = orders[i].ExecutedQuantity;
            kJ(lists[3])[i] = orders[i].LeavesQuantity;
            kJ(lists[4])[i] = orders[i].MaxVisibleQuantity;
            kJ(lists[5])[i] = orders[i].Price;
            kJ(lists[6])[i] = orders[i].Quantity;
            kH(lists[7])[i] = (uint8_t)orders[i].Side;
            kJ(lists[8])[i] = orders[i].Slippage;
            kJ(lists[9])[i] = orders[i].StopPrice;
            kH(lists[10])[i] = (uint8_t)orders[i].TimeInForce;
            kJ(lists[11])[i] = orders[i].TrailingDistance;
            kJ(lists[12])[i] = orders[i].TrailingStep;
            kH(lists[13])[i] = (uint8_t)orders[i].Type;
            kJ(lists[14])[i] = _time;
            kJ(lists[15])[i] = orders[i].AccountId;
            kJ(lists[16])[i] = currentExecutedPrice[i];
            kJ(lists[17])[i] = currentExecutedQuantity[i];
        }
        K vals = knk(18,
                    lists[0],
                    lists[1],
                    lists[2],
                    lists[3],
                    lists[4],
                    lists[5],
                    lists[6],
                    lists[7],
                    lists[8],
                    lists[9],
                    lists[10],
                    lists[11],
                    lists[12],
                    lists[13],
                    lists[14],
                    lists[15],
                    lists[16],
                    lists[17]);
        return vals;
    }

    K position_prep(const vector<Position> &positions)
    {
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::milliseconds(1));

        vector<K> lists({ktn(KJ, positions.size()),
                      ktn(KH, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KH, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KF, positions.size()),
                      ktn(KF, positions.size()),
                      ktn(KF, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KJ, positions.size()),
                      ktn(KF, positions.size()),
                      ktn(KF, positions.size())});

        for (uint64_t i=0;i<positions.size();i++)
        {
            kJ(lists[0])[i] = positions[i].Id;
            kH(lists[1])[i] = positions[i].SymbolId;
            kJ(lists[2])[i] = positions[i].AvgEntryPrice;
            kJ(lists[3])[i] = positions[i].Quantity;
            kH(lists[4])[i] = (uint8_t)positions[i].Side;
            kJ(lists[5])[i] = _time,
            kJ(lists[6])[i] = positions[i].AccountId;
            kF(lists[7])[i] = positions[i].RiskZ;
            kF(lists[8])[i] = positions[i].RiskC;
            kF(lists[9])[i] = positions[i].Funding;
            kJ(lists[10])[i] = positions[i].MarkPrice;
            kJ(lists[11])[i] = positions[i].IndexPrice;
            kF(lists[12])[i] = positions[i].RealizedPnL;
            kF(lists[13])[i] = positions[i].UnrealizedPnL;
        }
        K vals = knk(14,
                    lists[0],
                    lists[1],
                    lists[2],
                    lists[3],
                    lists[4],
                    lists[5],
                    lists[6],
                    lists[7],
                    lists[8],
                    lists[9],
                    lists[10],
                    lists[11],
                    lists[12],
                    lists[13]);
        return vals;
    }

    void appendOrdersChunk(const Order &order)
    {
        orders_chunk.push_back(order);
        if (++count_orders_chunk < CHUNK_SIZE)
        {
            return;
        }
        count_orders_chunk = 0UL;
        vector<uint64_t> prc; 
        vector<uint64_t> qty;
        prc.resize(orders_chunk.size());
        qty.resize(orders_chunk.size());
        K vals = order_prep(orders_chunk, prc, qty);
        _kdb.insertMultRow("insert", "orders", vals);
        orders_chunk.clear();
    }

    void onAddOrder(const Order &order) override
    {
        // std::cout << "Add order: " << order << std::endl;
        appendOrdersChunk(order);
    }

    void onUpdateOrder(const Order &order) override
    {
        // std::cout << "Update order: " << order << std::endl;
        // ct.start("prepare udate'");
        string _table = "orders";
        char *_query = new char[600];
        auto _time = (uint64_t)(std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));

        sprintf(_query, 
        "Id=%lu, TimeInForce=%d, TrailingDistance=%lu, TrailingStep=%lu, Type=%d, Time=%lu, ExecutedQuantity=%lu, LeavesQuantity=%lu, MaxVisibleQuantity=%lu, Price=%lu, Quantity=%lu, Side=%d, Slippage=%lu, StopPrice=%lu",
        order.Id,
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
        sprintf(_query2, "%s upsert %s", _table.c_str());//, order.Id);
        // ct.end();
        // ct.start("update order");
        _kdb.executeQuery(_query2);
        // ct.end();
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

    void appendTransactionsChunk(const Order &order, uint64_t price, uint64_t quantity)
    {
        transactions_chunk.push_back(order);
        executedPrice.push_back(price);
        executedQuantity.push_back(quantity);

        if (++count_transactions_chunk < CHUNK_SIZE)
        {
            return;
        }
        count_transactions_chunk = 0UL;

        K vals = order_prep(orders_chunk, executedPrice, executedQuantity);
        _kdb.insertMultRow("insert", "transactions", vals);
        orders_chunk.clear();
        executedQuantity.clear();
        executedQuantity.clear();
    }

    void appendPositionsChunk(const string& query, const Position &position)
    {
        positions_chunk.push_back(position);

        if (++count_positions_chunk < 10UL)
        {
            return;
        }
        count_positions_chunk = 0UL;

        K vals = position_prep(positions_chunk);
        _kdb.insertMultRow(query, "positions", vals);
        positions_chunk.clear();
    }

    void onExecuteOrder(const Order &order, uint64_t price, uint64_t quantity) override
    {
        // std::cout << "Execute order: " << order << std::endl;
        appendTransactionsChunk(order, price, quantity);

        // char *_query2 = new char[600];
        // sprintf(_query2, "select [-1] from positions where AccountId=%lu and SymbolId=%u", order.AccountId, order.SymbolId);
        // K data = _kdb.readQuery(_query2);
        // _kdb.printq(data);
        // last_pos = last_pos.ReadDbStructure(data, _kdb);

        Position last_pos, curr_pos;

        string _user_symbol = to_string(order.SymbolId) + "+" + to_string(order.AccountId);
        last_pos = usersStats[_user_symbol];
        curr_pos = last_pos.OrderExecuted(last_pos, order, price, quantity, symbols[order.SymbolId]);
        usersStats[_user_symbol] = curr_pos;

        appendPositionsChunk("update", curr_pos);
    }
    
};

void printStatsNumber(MyMarketHandler& market_handler, const string& query)
{
    K count_orders = market_handler._kdb.readQuery(query);
    cout << "Number of historical `"<< query << "`: " << (unsigned long)count_orders->j << endl;
}

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
    
    market_handler.addUser(0, "wonabru");
    market_handler.addUser(1, "chris");

    // exit(1);

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
    printStatsNumber(market_handler, "count prices");
    printStatsNumber(market_handler, "count orders");
    printStatsNumber(market_handler, "count transactions");
    printStatsNumber(market_handler, "count positions");
    kclose(kdb);
    return 0;
}