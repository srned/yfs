#ifndef lock_server_cache_rsm_h
#define lock_server_cache_rsm_h

#include <string>

#include "lock_protocol.h"
#include "rpc.h"
#include "rsm_state_transfer.h"
#include "rsm.h"

/*
Lock Server Cache
-----------------
Server is generally designed to be non-blocking and care is taken not to make RPC calls within acquire/release calls.

tLockMap: Stores the state of locks(as per below struct) in a Map.
struct lock_cache_value {
    l_state lock_state;
    std::string owner_clientid; // used to send revoke
    std::string retrying_clientid; // used to match with incoming acquire request
    std::list<std::string> waiting_clientids; // need to send retry
};
lmap_mutex: Global lock to synchronize

It supports acquire, release

Releaser, Retryer Threads:
-On init, we spawn two threads, releaser and retryer (mutex and CVs are dedicated for each for synchronization)
-Releaser will listen on a list and will send revoke RPC to client who owns the lock.
-Retryer will listen on a list and will send retry RPC to client who is scheduled to own next

Acquire:
Lock is granted only if current state is
1)LOCKFREE (or) (RETRYING and incoming id matches the retrying_clientid so
that it is granted to the appropriate client. Also, waiting_clientids is probed and if there is any client waiting,
we schedule a revoke request to be sent by Releaser thread to avoid starvation
2) LOCKED
we schedule a revoke request to be sent by Releaser thread and client to added to waiting_clientids list

Release:
State is set to LOCKFREE and if there are clients waiting for this lock, then we schedule a retry request
to be sent to the scheduled owner
*/

class lock_server_cache_rsm : public rsm_state_transfer {
 private:
  int nacquire;
  class rsm *rsm;

    protected:
        enum lock_state { LOCKFREE, LOCKED, REVOKING, RETRYING };
        typedef int l_state;
        struct lock_cache_value {
            l_state lock_state;
            std::string owner_clientid; // used to send revoke
            std::string retrying_clientid; // used to match with incoming acquire request
            std::list<std::string> waiting_clientids; // need to send retry
            lock_protocol::xid_t xid;

        };
        typedef std::map<lock_protocol::lockid_t, lock_cache_value*> TLockStateMap;
        TLockStateMap tLockMap;
        struct client_info {
            std::string client_id;
            lock_protocol::lockid_t lid;
        };
        std::list<client_info> retry_list;
        std::list<client_info> revoke_list;
        lock_cache_value* get_lock_obj(lock_protocol::lockid_t lid);
        pthread_mutex_t lmap_mutex;
        pthread_cond_t lmap_state_cv;
        pthread_mutex_t retry_mutex;
        pthread_cond_t retry_cv;
        pthread_mutex_t releaser_mutex;
        pthread_cond_t releaser_cv;



 public:
  lock_server_cache_rsm(class rsm *rsm = 0);
  ~lock_server_cache_rsm();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  std::string marshal_state();
  void unmarshal_state(std::string state);
  int acquire(lock_protocol::lockid_t, std::string id, 
	      lock_protocol::xid_t, int &);
  int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
	      int &);
};

#endif
