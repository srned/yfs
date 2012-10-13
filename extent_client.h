// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
    private:
        rpcc *cl;

    protected:
        struct extent_value {
            bool iscached;
            bool dirty;
            std::string data;
            extent_protocol::attr ext_attr;
        };

        typedef std::map<extent_protocol::extentid_t, extent_value *> Textent_cache;
        Textent_cache extent_cache;
        pthread_mutex_t extcache_mutex;

    public:
        extent_client(std::string dst);
        extent_protocol::status load(extent_protocol::extentid_t eid);
        extent_protocol::status flush(extent_protocol::extentid_t eid);
        extent_protocol::status get(extent_protocol::extentid_t eid,int off, unsigned int size, 
                std::string &buf);
        extent_protocol::status getattr(extent_protocol::extentid_t eid, 
                extent_protocol::attr &a);
        extent_protocol::status put(extent_protocol::extentid_t eid, int off, std::string buf);
        extent_protocol::status remove(extent_protocol::extentid_t eid);
};

#endif 

