// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <string>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"
#include <map>
#include <pthread.h>

class lock_server {

 protected:
  int nacquire;
  enum lock_state { LOCKFREE, LOCKED };
  typedef int l_state;
  typedef std::map<lock_protocol::lockid_t, l_state> TLockStateMap;
  TLockStateMap tLockMap;

 public:
  lock_server();
  ~lock_server();
  lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);

};

#endif 







