// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


static void *
releasethread(void *x)
{
      lock_client_cache *cc = (lock_client_cache *) x;
        cc->releaser();
          return 0;
}

static void *
retrythread(void *x)
{
      lock_client_cache *cc = (lock_client_cache *) x;
        cc->retryer();
          return 0;
}


lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
  VERIFY(pthread_mutex_init(&client_cache_mutex,0) == 0);
  VERIFY(pthread_cond_init(&client_cache_cv, NULL) == 0);
  // Create retryer and releaser threads
  pthread_t retryer_thread, releaser_thread;
  VERIFY(pthread_mutex_init(&client_retry_mutex,0) == 0);
  VERIFY(pthread_cond_init(&client_retry_cv, NULL) == 0);
  VERIFY(pthread_mutex_init(&client_releaser_mutex,0) == 0);
  VERIFY(pthread_cond_init(&client_releaser_cv, NULL) == 0);

  if (pthread_create(&retryer_thread, NULL, &retrythread,(void *) this)) 
      tprintf("Error in creating retryer thread\n");
  if (pthread_create(&releaser_thread, NULL, &releasethread,(void *) this))
      tprintf("Error in creating releaser thread\n");
  // TODO: destroy these
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
    int ret = lock_protocol::OK, r;
    lock_cache_value *lock_cache_obj;
    //tprintf("In Client Acquire for lid:%llu,tid: %lu, id: %s\n", lid, pthread_self(), id.c_str());
    lock_cache_obj = get_lock_obj(lid);
    pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
    while(true) {
        if (lock_cache_obj->lock_cache_state == NONE) {
            // send acquire rpc
            //tprintf("in None case,tid: %lu\n", pthread_self());
            lock_cache_obj->lock_cache_state = ACQUIRING;
            //tprintf("Called server for Acquire, tid: %lu, id: %s\n", pthread_self(), id.c_str());
            ret = cl->call(lock_protocol::acquire, id, lid, r);
            //tprintf("Responded back, tid:%lu\n", pthread_self());
            if (ret == lock_protocol::OK) { 
                lock_cache_obj->lock_cache_state = LOCKED;
                break;
            }
            else if (ret == lock_protocol::RETRY) {
                // wait on condition to be notified by retry RPC
                pthread_cond_wait(&lock_cache_obj->client_lock_cv,                                     
                &lock_cache_obj->client_lock_mutex);
                if (FREE == lock_cache_obj->lock_cache_state) {
                    lock_cache_obj->lock_cache_state = LOCKED;
                    break;
                }
                else continue;
            }
        }
        else if (lock_cache_obj->lock_cache_state == FREE) {
            //tprintf("in FREE case,tid: %lu\n", pthread_self());
            lock_cache_obj->lock_cache_state = LOCKED;
            break;
        }
        else {
                pthread_cond_wait(&lock_cache_obj->client_lock_cv,                                     
                &lock_cache_obj->client_lock_mutex);
                if (FREE == lock_cache_obj->lock_cache_state) {
                    lock_cache_obj->lock_cache_state = LOCKED;
                    break;
                }
                else continue;
        }
    }
    pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);

    return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
    lock_cache_value *lock_cache_obj = get_lock_obj(lid);
    pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
    if (lock_cache_obj->lock_cache_state == RELEASING)
        pthread_cond_signal(&lock_cache_obj->client_revoke_cv);
    else if (lock_cache_obj->lock_cache_state == LOCKED) {
        lock_cache_obj->lock_cache_state = FREE;
        pthread_cond_signal(&lock_cache_obj->client_lock_cv);
    }
    else 
        tprintf("unknown state in release\n");
    pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);
    
    return lock_protocol::OK;
}

lock_client_cache::lock_cache_value* 
lock_client_cache::get_lock_obj(lock_protocol::lockid_t lid)
{
    lock_cache_value *lock_cache_obj;
    pthread_mutex_lock(&client_cache_mutex);
    if (clientcacheMap.count(lid) > 0) {
        lock_cache_obj = clientcacheMap[lid];
        //printf("found!!!!\n");
    }
    else {
        lock_cache_obj = new lock_cache_value();
        VERIFY(pthread_mutex_init(&lock_cache_obj->client_lock_mutex,0) == 0);
        VERIFY(pthread_cond_init(&lock_cache_obj->client_lock_cv, NULL) == 0);
        VERIFY(pthread_cond_init(&lock_cache_obj->client_revoke_cv, NULL) == 0);
        clientcacheMap[lid] = lock_cache_obj;
        //printf("allocating new\n");
    }
    pthread_mutex_unlock(&client_cache_mutex);
    return lock_cache_obj;
}

void 
lock_client_cache::retryer(void) {
    int r;
    int ret;
    while(true) {
        pthread_mutex_lock(&client_retry_mutex);
        pthread_cond_wait(&client_retry_cv, &client_retry_mutex);
        while (!retry_list.empty()) {
            lock_protocol::lockid_t lid = retry_list.front();
            retry_list.pop_front();
            lock_cache_value *lock_cache_obj = get_lock_obj(lid);
            //tprintf("retryer: for lid:%llu\n", lid);
            pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
            ret = cl->call(lock_protocol::acquire, id, lid, r);
            if (ret == lock_protocol::OK) {
                lock_cache_obj->lock_cache_state = FREE;
                pthread_cond_signal(&lock_cache_obj->client_lock_cv);
                pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);
            }
            else 
                tprintf("retry fail, should never happen, ret:%d\n", ret);
        }
        pthread_mutex_unlock(&client_retry_mutex);
    }
}

void
lock_client_cache::releaser(void) {
    int r,ret;
    while(true) {
        pthread_mutex_lock(&client_releaser_mutex);
        pthread_cond_wait(&client_releaser_cv, &client_releaser_mutex);
        while (!revoke_list.empty()) {
            lock_protocol::lockid_t lid = revoke_list.front();
            revoke_list.pop_front();
            lock_cache_value *lock_cache_obj = get_lock_obj(lid);
            pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
            if (lock_cache_obj->lock_cache_state == LOCKED) {
                lock_cache_obj->lock_cache_state = RELEASING;
                //tprintf("releaser: waiting in releasing state\n");
                pthread_cond_wait(&lock_cache_obj->client_revoke_cv,
                        &lock_cache_obj->client_lock_mutex);
            }
            //tprintf("releaser: calling server release, id: %s\n", id.c_str());
            ret = cl->call(lock_protocol::release, id, lid, r);
            lock_cache_obj->lock_cache_state = NONE;
            pthread_cond_signal(&lock_cache_obj->client_lock_cv);
            pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);
        }
        pthread_mutex_unlock(&client_releaser_mutex);
    }
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
  int ret = rlock_protocol::OK;
  //tprintf("got revoke from server for lid:%llu\n", lid);
  pthread_mutex_lock(&client_releaser_mutex);
  revoke_list.push_back(lid);
  pthread_cond_signal(&client_releaser_cv);
  pthread_mutex_unlock(&client_releaser_mutex);

  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
  int ret = rlock_protocol::OK;
  // Push the lid to the retry list
  //tprintf("got retry from server for lid:%llu\n", lid);
  pthread_mutex_lock(&client_retry_mutex);
  retry_list.push_back(lid);
  pthread_cond_signal(&client_retry_cv);
  pthread_mutex_unlock(&client_retry_mutex);

  return ret;
}
