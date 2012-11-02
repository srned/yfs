// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache_rsm.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"

#include "rsm_client.h"

static void *
releasethread(void *x)
{
  lock_client_cache_rsm *cc = (lock_client_cache_rsm *) x;
  cc->releaser();
  return 0;
}

static void *
retrythread(void *x)
{
      lock_client_cache_rsm *cc = (lock_client_cache_rsm *) x;
        cc->retryer();
          return 0;
}

int lock_client_cache_rsm::last_port = 0;

lock_client_cache_rsm::lock_client_cache_rsm(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // VERIFY(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache_rsm::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache_rsm::retry_handler);
  xid = 0;
  // You fill this in Step Two, Lab 7
  // - Create rsmc, and use the object to do RPC 
  //   calls instead of the rpcc object of lock_client
  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  VERIFY (r == 0);
  rsmc = new rsm_client(xdst); 


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

}

void 
lock_client_cache_rsm::retryer(void) {
    int r;
    int ret;
    while(true) {
        pthread_mutex_lock(&client_retry_mutex);
        pthread_cond_wait(&client_retry_cv, &client_retry_mutex);
        while (!retry_list.empty()) {
            lock_protocol::lockid_t lid = retry_list.front();
            retry_list.pop_front();
            lock_cache_value *lock_cache_obj = get_lock_obj(lid);
            tprintf("retryer: for lid:%llu\n", lid);
            pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
            ret = rsmc->call(lock_protocol::acquire, lid, id, xid, r);
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
lock_client_cache_rsm::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.
    int r,ret;
//    bool doflush=true;
    while(true) {
        pthread_mutex_lock(&client_releaser_mutex);
        pthread_cond_wait(&client_releaser_cv, &client_releaser_mutex);
        while (!revoke_list.empty()) {
            lock_protocol::lockid_t lid = revoke_list.front();
            revoke_list.pop_front();
            lock_cache_value *lock_cache_obj = get_lock_obj(lid);
            pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
            tprintf("releaser: got lock checking\n");
            if (lock_cache_obj->lock_cache_state == LOCKED) {
                lock_cache_obj->lock_cache_state = RELEASING;
                tprintf("releaser: waiting in releasing state\n");
                pthread_cond_wait(&lock_cache_obj->client_revoke_cv,
                        &lock_cache_obj->client_lock_mutex);
            }
  //          else if (lock_cache_obj->lock_cache_state == ACQUIRING) {
    //            tprintf("releaser: calling server release, id: ACQUIRING\n");
      //          doflush = false;
        //    }
            tprintf("releaser: calling server release, id: %s\n", id.c_str());
            if(lock_cache_obj->doflush) {
                tprintf("releaser: state: %d\n", lock_cache_obj->lock_cache_state);
                if (lu != NULL)
                    lu->dorelease(lid);
            }
            ret = rsmc->call(lock_protocol::release, lid, id, xid, r);
            lock_cache_obj->lock_cache_state = NONE;
            pthread_cond_signal(&lock_cache_obj->client_lock_cv);
            pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);
        }
        pthread_mutex_unlock(&client_releaser_mutex);
    }

}


lock_protocol::status
lock_client_cache_rsm::acquire(lock_protocol::lockid_t lid)
{
 // int ret = lock_protocol::OK;

 // return ret;
    int ret = lock_protocol::OK, r;
    lock_cache_value *lock_cache_obj;
    //tprintf("In Client Acquire for lid:%llu,tid: %lu, id: %s\n", lid, pthread_self(), id.c_str());
    lock_cache_obj = get_lock_obj(lid);
    pthread_mutex_lock(&lock_cache_obj->client_lock_mutex);
    while(true) {
        if (lock_cache_obj->lock_cache_state == NONE) {
            // send acquire rpc
            tprintf("in None case,tid: %lu\n", pthread_self());
            lock_cache_obj->lock_cache_state = ACQUIRING;
            lock_cache_obj->doflush = false;
            tprintf("Called server for Acquire, tid: %lu, id: %s\n", pthread_self(), id.c_str());
            ret = rsmc->call(lock_protocol::acquire, lid, id, xid, r);
            tprintf("Responded back, tid:%lu\n", pthread_self());
            if (ret == lock_protocol::OK) { 
                lock_cache_obj->lock_cache_state = LOCKED;
                break;
            }
            else if (ret == lock_protocol::RETRY) {
                // wait on condition to be notified by retry RPC
                pthread_cond_wait(&lock_cache_obj->client_lock_cv,                                     
                &lock_cache_obj->client_lock_mutex);
                tprintf("client_cache-acquire: out from condition\n");
                if (FREE == lock_cache_obj->lock_cache_state) {
                    lock_cache_obj->lock_cache_state = LOCKED;
                    break;
                }
                else continue;
            }
        }
        else if (lock_cache_obj->lock_cache_state == FREE) {
            tprintf("in FREE case,tid: %lu\n", pthread_self());
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
    lock_cache_obj->doflush = true;
    pthread_mutex_unlock(&lock_cache_obj->client_lock_mutex);

    return lock_protocol::OK;



}

lock_protocol::status
lock_client_cache_rsm::release(lock_protocol::lockid_t lid)
{
//  return lock_protocol::OK;
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

lock_client_cache_rsm::lock_cache_value* 
lock_client_cache_rsm::get_lock_obj(lock_protocol::lockid_t lid)
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

rlock_protocol::status
lock_client_cache_rsm::revoke_handler(lock_protocol::lockid_t lid, 
			          lock_protocol::xid_t xid, int &)
{
//  int ret = rlock_protocol::OK;
//  return ret;
  int ret = rlock_protocol::OK;
  tprintf("got revoke from server for lid:%llu\n", lid);
  pthread_mutex_lock(&client_releaser_mutex);
  revoke_list.push_back(lid);
  pthread_cond_signal(&client_releaser_cv);
  pthread_mutex_unlock(&client_releaser_mutex);

  return ret;

}

rlock_protocol::status
lock_client_cache_rsm::retry_handler(lock_protocol::lockid_t lid, 
			         lock_protocol::xid_t xid, int &)
{
//  int ret = rlock_protocol::OK;
//  return ret;

  int ret = rlock_protocol::OK;
  // Push the lid to the retry list
  tprintf("got retry from server for lid:%llu\n", lid);
  pthread_mutex_lock(&client_retry_mutex);
  retry_list.push_back(lid);
  pthread_cond_signal(&client_retry_cv);
  pthread_mutex_unlock(&client_retry_mutex);

  return ret;

}


