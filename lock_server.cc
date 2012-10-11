// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>


/* Constructor */
lock_server::lock_server():
  nacquire (0)
{
 pthread_mutex_init(&lmap_mutex, NULL);
 pthread_cond_init(&lmap_state_cv, NULL);
}

/* Destructor */
lock_server::~lock_server()
{
 pthread_mutex_destroy(&lmap_mutex);
 pthread_cond_destroy(&lmap_state_cv);
}

/* Statistics */
lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

/* Acquire */
lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  l_state lock_state = LOCKFREE;
  pthread_mutex_lock(&lmap_mutex);
  if (tLockMap.count(lid) > 0) {
     while (lock_state != tLockMap[lid]) 
	pthread_cond_wait(&lmap_state_cv, &lmap_mutex);
   }
   tLockMap[lid] = LOCKED;
   pthread_mutex_unlock(&lmap_mutex);

   return ret;

}

/* Release */
lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  pthread_mutex_lock(&lmap_mutex);
  if ((tLockMap.count(lid) > 0) && (tLockMap[lid] == LOCKED)) {
     tLockMap[lid] = LOCKFREE;
     pthread_cond_broadcast(&lmap_state_cv);
     pthread_mutex_unlock(&lmap_mutex);
  }
  else {
  pthread_mutex_unlock(&lmap_mutex);
  ret = lock_protocol::NOENT;
  }

  return ret;
}

