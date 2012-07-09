// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"

class extent_server {

 public:
  extent_server();
  ~extent_server();
 protected:

 struct extent_value {
 std::string data;
 extent_protocol::attr ext_attr;
 };

 typedef std::map<extent_protocol::extentid_t, extent_value *> Textent_store;
 Textent_store extent_store;
 pthread_mutex_t extstore_mutex;
  public:
  int put(extent_protocol::extentid_t id, int off, std::string, int &);
  int get(extent_protocol::extentid_t id, int off, unsigned int size, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);
};

#endif 







