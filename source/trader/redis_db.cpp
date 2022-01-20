#include "trader/redis_db.h"

#include <iostream>
#include <thread>
#include <regex>
#include <string>
#include <chrono>
#include <sw/redis++/redis++.h>

using namespace sw::redis;

using namespace std;


string MyRedis::_get_db(const string& key)
    {
    
        try {
            auto val = this->redis.get(key);
            if (val) {
                return *val;
            }
        }catch (const Error &e) {
            cout << e.what() << endl;
            return NULL;
        }
        return NULL;
    }

bool MyRedis::_del_db(const string& key)
    {
    
        try {
            this->redis.del(key);
        }catch (const Error &e) {
            cout << e.what() << endl;
            return false;
        }
        return true;
    }

std::vector<string> MyRedis::_lrange_db(const string& key)
    {
    
        try {
            
            std::vector<string> vec;
            this->redis.lrange(key, 0, -1, std::back_inserter(vec));
            return vec;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return {};
        }
    }

uint64_t MyRedis::_get_last_index(const string& key)
    {
        try {            
            std::vector<string> vec;
            this->redis.lrange(key, -1, -1, std::back_inserter(vec));
            
            if (!vec.empty())
            {
                cout << vec.back() << endl;
                return std::stoi(vec.back()) + 1;
            }else{
                cout << "Empty table: " << key << endl;
            }
        }catch (const Error &e) {
            cout << e.what() << endl;
            return 1;
        }     
        return 1;   
    }

bool MyRedis::_set_db(const string& key, const string& val)
    {
        try {

            this->redis.set(key, val);
            return true;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return false;
        }
    }

bool MyRedis::_append_db(const string& key, const vector<std::string> val)
    {
        try {

            this->redis.rpush(key, val.begin(), val.end());
            return true;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return false;
        }
    }

bool MyRedis::_append_db(const string& key, const vector<uint64_t> val)
    {
        try {

            this->redis.rpush(key, val.begin(), val.end());
            return true;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return false;
        }
    }

bool MyRedis::_hmset_db(const string& key, const std::unordered_map<std::string, std::string> val)
    {
        try {

            this->redis.hmset(key, val.begin(), val.end());
            return true;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return false;
        }
    }


std::unordered_map<std::string, std::string> MyRedis::_hgetall_db(const string& key)
    {
        try {
            vector<std::string> val;
            this->redis.hgetall(key, std::inserter(val, val.begin()));

            std::unordered_map<std::string, std::string> val_ret = {};
            string key_ret;
            string value;
            int i = 0;
            for (std::vector<string>::iterator it = val.begin(); it != val.end(); ++it)
            {
                if (i % 2 == 0)
                {
                    key_ret = *it;
                    continue;
                }
                value = *it;
                val_ret[key_ret] = value;    
                i++;
            }

            return val_ret;
        }catch (const Error &e) {
            cout << e.what() << endl;
            return {};
        }
    }

