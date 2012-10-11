// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
releasethread(void *x)
{
    lock_server_cache *cc = (lock_server_cache *) x;
    cc->releaser();
    return 0;
}

static void *
retryerthread(void *x)
{
    lock_server_cache *cc = (lock_server_cache *) x;
    cc->retryer();
    return 0;
}

lock_server_cache::lock_server_cache():
    nacquire (0)
{
    pthread_mutex_init(&lmap_mutex, NULL);
    pthread_cond_init(&lmap_state_cv, NULL);
    pthread_t retryer_thread, releaser_thread;
    VERIFY(pthread_mutex_init(&retry_mutex,0) == 0);
    VERIFY(pthread_cond_init(&retry_cv, NULL) == 0);
    VERIFY(pthread_mutex_init(&releaser_mutex,0) == 0);
    VERIFY(pthread_cond_init(&releaser_cv, NULL) == 0);

    if (pthread_create(&retryer_thread, NULL, &retryerthread, (void *) this))
        tprintf("Error in creating retryer thread\n");
    if (pthread_create(&releaser_thread, NULL, &releasethread, (void *)this))
        tprintf("Error in creating releaser thread\n");
}

lock_server_cache::~lock_server_cache()
{
    pthread_mutex_destroy(&lmap_mutex);
    pthread_cond_destroy(&lmap_state_cv);
    pthread_mutex_destroy(&retry_mutex);
    pthread_cond_destroy(&retry_cv);
    pthread_mutex_destroy(&releaser_mutex);
    pthread_cond_destroy(&releaser_cv);
}


int lock_server_cache::acquire(std::string id, lock_protocol::lockid_t lid, 
                               int &)
{
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

lock_server_cache::lock_cache_value* lock_server_cache::get_lock_obj(lock_protocol::lockid_t lid) 
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

int 
lock_server_cache::release(std::string id, lock_protocol::lockid_t lid, 
         int &r)
{
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

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

void
lock_server_cache::retryer(void) {
    int r;
    rlock_protocol::status r_ret;
    while(true) {
        pthread_mutex_lock(&retry_mutex);
        pthread_cond_wait(&retry_cv, &retry_mutex);
        while (!retry_list.empty()) {
            client_info c_info = retry_list.front();
            retry_list.pop_front();
            handle h(c_info.client_id);
            if (h.safebind()) 
                r_ret = h.safebind()->call(rlock_protocol::retry, 
                        c_info.lid, r);
            if (!h.safebind() || r_ret != rlock_protocol::OK) 
                tprintf("retry RPC failed\n");
        }
        pthread_mutex_unlock(&retry_mutex);
    }
}

void 
lock_server_cache::releaser(void) {
    int r;
    rlock_protocol::status r_ret;
    while(true) {
        pthread_mutex_lock(&releaser_mutex);
        pthread_cond_wait(&releaser_cv, &releaser_mutex);
        while (!revoke_list.empty()) {
            client_info c_info = revoke_list.front();
            revoke_list.pop_front();
            handle h(c_info.client_id);
            if (h.safebind()) 
                r_ret = h.safebind()->call(rlock_protocol::revoke, 
                        c_info.lid, r);
            if (!h.safebind() || r_ret != rlock_protocol::OK) 
                tprintf("revoke RPC failed\n");
        }
        pthread_mutex_unlock(&releaser_mutex);
    }
}
