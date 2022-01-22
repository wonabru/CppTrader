#ifndef CPPTRADER_KDB_DB_H
#define CPPTRADER_KDB_DB_H
#define KXVER 3

#include <iostream>
#include <thread>
#include <regex>
#include <string>
#include <chrono>
#include "../kdb/c/c/k.h"


class Kdbp
{
protected:
    I _kdb;

public:
    Kdbp(I kdb){
        _kdb = kdb; 
    }
    J castTime(struct tm *x);
    
    int insertMultRow(const std::string& query, const std::string& table, K rows);
    int insertRow(const std::string& query, const std::string& table, K row);
    int deleteRow(const std::string& query, const std::string& table, K row);
    int executeQuery(const std::string& query);
    K readQuery(const std::string& query);
    K printitem(K x, int index);
    K printtable(K x);
    void fmt_time(char *str, time_t time, int adjusted);
    K printatom(K x);
    K printq(K x);
    K printdict(K x);
    K printlist(K x);
    K getitem(K x, int index);
};


#endif // CPPTRADER_KDB_DB_H
