#ifndef CPPTRADER_REDIS_DB_H
#define CPPTRADER_REDIS_DB_H

#include <iostream>
#include <thread>
#include <regex>
#include <string>
#include <chrono>
#include <sw/redis++/redis++.h>


class MyRedis
{
protected:
    std::string _redis_host = "127.0.0.1:6379";
    sw::redis::Redis redis = sw::redis::Redis("tcp://" + _redis_host);

public:

    std::string _get_db(const std::string& key);

    bool _del_db(const std::string& key);

    std::vector<std::string> _lrange_db(const std::string& key);

    uint64_t _get_last_index(const std::string& key);
    bool _set_db(const std::string& key, const std::string& val);
    bool _append_db(const std::string& key, const std::vector<std::string> val);
    bool _append_db(const std::string& key, const std::vector<uint64_t> val);
    bool _hmset_db(const std::string& key, const std::unordered_map<std::string, std::string> val);
    std::unordered_map<std::string, std::string> _hgetall_db(const std::string& key);

};


#endif // CPPTRADER_REDIS_DB_H
