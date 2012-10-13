// lock client interface.

#ifndef lock_client_cache_h

#define lock_client_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_client.h"
#include "lang/verify.h"
#include <map>

// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 5.
/*
Lock Client Cache
-----------------
clientcacheMap: Stores the state of locks(as per below struct) in a Map.
struct lock_cache_value {
l_cache_state lock_cache_state; // State of the lock cache
thread_mutex_t client_lock_mutex; // To protect this structure
pthread_cond_t client_lock_cv; // CV to be notified of state chang
pthread_cond_t client_revoke_cv; // CV to be notified if lock is revoked
};
client_cache_mutex: To protect clientcacheMap

It supports acquire, release and listens on revoke and retry.

Releaser, Retryer Threads:
-On init, we spawn two threads, releaser and retryer (mutex and CVs are dedicated for each for synchronization)
-Releaser will listen on a list and release the lock if it is in FREE state or set the state to RELEASING and
wait on client_revoke_cv to be set by the next release request from the client. Once it is released by the client,
it sends release request to server
-Retryer will listen on a list and sends acquire request to server for each lockid. Upon successful response, it notifies
threads that are waiting on client_lock_cv.

Acquire:
The course of action depends on the state of the lockid.
1)NONE: We send acquire to server and set the state to ACQUIRING. On OK response, state is set to LOCKED.
On RETRY response, we wait on client_lock_cv. Upon wakeup, if the state is not FREE, we loop back so that the next course of
action depends on the current state
2)FREE: State is set to LOCKED
3)Otherwise: we wait on client_lock_cv. Upon wakeup, if the state is not FREE, we loop back so that the next course of
action depends on the current state

Release:
If the state of lock is RELEASING, we signal client_revoke_cv. For LOCKED, we signal client_lock_cv.
*/

class lock_release_user {
 public:
  virtual void dorelease(lock_protocol::lockid_t) = 0;
  virtual ~lock_release_user() {};
};

class lock_client_cache : public lock_client {
 private:
  class lock_release_user *lu;
  int rlock_port;
  std::string hostname;
  std::string id;

 protected:
  enum lock_cache_state { NONE, FREE, LOCKED, ACQUIRING, RELEASING };
  typedef int l_cache_state;

  struct lock_cache_value {
      bool doflush; // This is used to track if the lock has been locked atleast once
      l_cache_state lock_cache_state; // State of the lock cache
      pthread_mutex_t client_lock_mutex; // To protect this structure
      pthread_cond_t client_lock_cv; // CV to be notified of state change
      pthread_cond_t client_revoke_cv; // CV to be notified if lock is revoked
  };

  typedef std::map<lock_protocol::lockid_t, lock_cache_value *> TLockCacheStateMap;
  TLockCacheStateMap clientcacheMap;
  pthread_mutex_t client_cache_mutex;
  pthread_cond_t client_cache_cv;
  pthread_mutex_t client_retry_mutex, client_releaser_mutex;
  pthread_cond_t client_retry_cv, client_releaser_cv;
  std::list<lock_protocol::lockid_t> retry_list;
  std::list<lock_protocol::lockid_t> revoke_list;
  lock_cache_value* get_lock_obj(lock_protocol::lockid_t lid);

 public:
  lock_client_cache(std::string xdst, class lock_release_user *l = 0);
  virtual ~lock_client_cache() {};
  lock_protocol::status acquire(lock_protocol::lockid_t);
  lock_protocol::status release(lock_protocol::lockid_t);
  rlock_protocol::status revoke_handler(lock_protocol::lockid_t, 
                                        int &);
  rlock_protocol::status retry_handler(lock_protocol::lockid_t, 
                                       int &);
  void retryer(void);
  void releaser(void);
  int acquire_from_server(long long unsigned int, lock_client_cache::lock_cache_value*);
  void wait_to_acquire(long long unsigned int, lock_client_cache::lock_cache_value*);
};


#endif
