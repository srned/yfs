// the caching lock server implementation

#include "lock_server_cache_rsm.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
revokethread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->retryer();
  return 0;
}

lock_server_cache_rsm::lock_server_cache_rsm(class rsm *_rsm) 
  : rsm (_rsm)
{
/*  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  VERIFY (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  VERIFY (r == 0);
  */
    pthread_mutex_init(&lmap_mutex, NULL);
    pthread_cond_init(&lmap_state_cv, NULL);
    pthread_t retryer_thread, releaser_thread;
    VERIFY(pthread_mutex_init(&retry_mutex,0) == 0);
    VERIFY(pthread_cond_init(&retry_cv, NULL) == 0);
    VERIFY(pthread_mutex_init(&releaser_mutex,0) == 0);
    VERIFY(pthread_cond_init(&releaser_cv, NULL) == 0);
    rsm->set_state_transfer(this);

    if (pthread_create(&retryer_thread, NULL, &retrythread, (void *) this))
        tprintf("Error in creating retryer thread\n");
    if (pthread_create(&releaser_thread, NULL, &revokethread, (void *)this))
        tprintf("Error in creating releaser thread\n");

}

lock_server_cache_rsm::~lock_server_cache_rsm()
{
    pthread_mutex_destroy(&lmap_mutex);
    pthread_cond_destroy(&lmap_state_cv);
    pthread_mutex_destroy(&retry_mutex);
    pthread_cond_destroy(&retry_cv);
    pthread_mutex_destroy(&releaser_mutex);
    pthread_cond_destroy(&releaser_cv);
}

lock_server_cache_rsm::lock_cache_value* lock_server_cache_rsm::get_lock_obj
(lock_protocol::lockid_t lid) 
{
    lock_cache_value *lock_cache_obj;
    if (tLockMap.count(lid) > 0) 
        lock_cache_obj = tLockMap[lid];
    else {
        lock_cache_obj = new lock_cache_value();
        tLockMap[lid] = lock_cache_obj;
    }
    return lock_cache_obj;
}

void
lock_server_cache_rsm::revoker()
{

    // This method should be a continuous loop, that sends revoke
    // messages to lock holders whenever another client wants the
    // same lock
    int r;
    lock_protocol::xid_t xid = 0;
    rlock_protocol::status r_ret;
    while(true) {
        pthread_mutex_lock(&releaser_mutex);
        pthread_cond_wait(&releaser_cv, &releaser_mutex);
        while (!revoke_list.empty()) {
            client_info c_info = revoke_list.front();
            revoke_list.pop_front();
            if (rsm->amiprimary()) {
                handle h(c_info.client_id);
                if (h.safebind()) 
                    r_ret = h.safebind()->call(rlock_protocol::revoke, 
                            c_info.lid, xid, r);
                if (!h.safebind() || r_ret != rlock_protocol::OK) 
                    tprintf("revoke RPC failed\n");
            }
        }
        pthread_mutex_unlock(&releaser_mutex);
    }

}


void
lock_server_cache_rsm::retryer()
{

    // This method should be a continuous loop, waiting for locks
    // to be released and then sending retry messages to those who
    // are waiting for it.
    int r;
    rlock_protocol::status r_ret;
    lock_protocol::xid_t xid = 0;
    while(true) {
        pthread_mutex_lock(&retry_mutex);
        pthread_cond_wait(&retry_cv, &retry_mutex);
        while (!retry_list.empty()) {
            client_info c_info = retry_list.front();
            retry_list.pop_front();
            if(rsm->amiprimary()) {
                handle h(c_info.client_id);
                if (h.safebind()) 
                    r_ret = h.safebind()->call(rlock_protocol::retry, 
                            c_info.lid, xid, r);
                if (!h.safebind() || r_ret != rlock_protocol::OK) 
                    tprintf("retry RPC failed\n");
            }
        }
        pthread_mutex_unlock(&retry_mutex);
    }

}


int lock_server_cache_rsm::acquire(lock_protocol::lockid_t lid, std::string id, 
             lock_protocol::xid_t xid, int &)
{
//  lock_protocol::status ret = lock_protocol::OK;
//  return ret;
    lock_protocol::status ret = lock_protocol::OK;
    lock_cache_value *lock_cache_obj;
    pthread_mutex_lock(&lmap_mutex);
    lock_cache_obj = get_lock_obj(lid);
    if ((lock_cache_obj->lock_state == LOCKFREE) || 
            (lock_cache_obj->lock_state == RETRYING && 
             lock_cache_obj->retrying_clientid == id)) {
        if (lock_cache_obj->lock_state != LOCKFREE) 
            lock_cache_obj->waiting_clientids.pop_front();

        lock_cache_obj->lock_state = LOCKED;
        lock_cache_obj->owner_clientid = id;
        lock_cache_obj->xid = xid;
        if (lock_cache_obj->waiting_clientids.size() > 0) {
            // Schedule a revoke
            lock_cache_obj->lock_state = REVOKING;
            pthread_mutex_lock(&releaser_mutex);
            client_info c_info;
            c_info.client_id = id;
            c_info.lid = lid;
            revoke_list.push_back(c_info);
            pthread_cond_signal(&releaser_cv);
            pthread_mutex_unlock(&releaser_mutex);
        }
    }
    else {
        lock_cache_obj->waiting_clientids.push_back(id);
        if (lock_cache_obj->lock_state == LOCKED) {
            // Schedule a revoke
            lock_cache_obj->lock_state = REVOKING;
            pthread_mutex_lock(&releaser_mutex);
            client_info c_info;
            c_info.client_id = lock_cache_obj->owner_clientid;
            c_info.lid = lid;
            revoke_list.push_back(c_info);
            pthread_cond_signal(&releaser_cv);
            pthread_mutex_unlock(&releaser_mutex);
        }

        ret = lock_protocol::RETRY;
    }
    pthread_mutex_unlock(&lmap_mutex);
    return ret;
}

int 
lock_server_cache_rsm::release(lock_protocol::lockid_t lid, std::string id, 
         lock_protocol::xid_t xid, int &r)
{
//  lock_protocol::status ret = lock_protocol::OK;
//  return ret;
    lock_protocol::status ret = lock_protocol::OK;
    lock_cache_value *lock_cache_obj;
    //tprintf("release: from %s for lid:%llu\n", id.c_str(), lid);
    pthread_mutex_lock(&lmap_mutex);

    lock_cache_obj = get_lock_obj(lid);
    lock_cache_obj->lock_state = LOCKFREE;
    if (lock_cache_obj->waiting_clientids.size() > 0) {
        lock_cache_obj->lock_state = RETRYING;
        client_info c_info;
        c_info.client_id = lock_cache_obj->waiting_clientids.front();
        c_info.lid = lid;
        lock_cache_obj->retrying_clientid = c_info.client_id;
        pthread_mutex_lock(&retry_mutex);
        retry_list.push_back(c_info);
        pthread_cond_signal(&retry_cv);
        pthread_mutex_unlock(&retry_mutex);
    }
    pthread_mutex_unlock(&lmap_mutex);

    return ret;

}

std::string
lock_server_cache_rsm::marshal_state()
{
    pthread_mutex_lock(&lmap_mutex);
    marshall rep;
    lock_cache_value *lock_cache_obj;

    rep << tLockMap.size();
    std::map<lock_protocol::lockid_t, lock_cache_value*>::iterator iter_lock;
    std::list<std::string>::iterator iter_waiting;
    for (iter_lock = tLockMap.begin(); iter_lock != tLockMap.end(); iter_lock++) {
        lock_protocol::lockid_t lock_id = iter_lock->first;
        lock_cache_obj = tLockMap[lock_id];
        rep << lock_id;
        rep << lock_cache_obj->lock_state;
        rep << lock_cache_obj->owner_clientid;
        rep << lock_cache_obj->retrying_clientid;
        rep << lock_cache_obj->waiting_clientids.size();
        for(iter_waiting = lock_cache_obj->waiting_clientids.begin(); 
                iter_waiting != lock_cache_obj->waiting_clientids.end(); iter_waiting++) 
            rep << *iter_waiting;
        rep << lock_cache_obj->xid;
    }
    pthread_mutex_unlock(&lmap_mutex);

    return rep.str();
}

void
lock_server_cache_rsm::unmarshal_state(std::string state)
{
    pthread_mutex_lock(&lmap_mutex);
    lock_cache_value *lock_cache_obj;
    unmarshall rep(state);
    unsigned int locks_size;
    unsigned int waiting_size;
    std::string waitinglockid; 
    rep >> locks_size;
    for (unsigned int i = 0; i < locks_size; i++) {
        lock_protocol::lockid_t lock_id;
        rep >> lock_id;
        lock_cache_obj = new lock_cache_value();
        rep >> lock_cache_obj->lock_state;
        rep >> lock_cache_obj->owner_clientid;
        rep >> lock_cache_obj->retrying_clientid;
        rep >> waiting_size;
        std::list<std::string> waitingids;
        for (unsigned int j = 0; j < waiting_size; j++) {
            rep >> waitinglockid;
            waitingids.push_back(waitinglockid);
        }
        lock_cache_obj->waiting_clientids = waitingids;
        rep >> lock_cache_obj->xid;
        tLockMap[lock_id] = lock_cache_obj;
    }

    pthread_mutex_unlock(&lmap_mutex);

}

lock_protocol::status
lock_server_cache_rsm::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

