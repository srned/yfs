// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {
   VERIFY(pthread_mutex_init(&extstore_mutex,0) == 0);
}

extent_server::~extent_server() {
   VERIFY(pthread_mutex_destroy(&extstore_mutex) == 0);
}

int extent_server::put(extent_protocol::extentid_t id, int off, std::string buf, int &r)
{
  // You fill this in for Lab 2.
  // New Entry addedi
  r = extent_protocol::OK;
  ScopedLock ml(&extstore_mutex); 
  extent_value *extent_obj;
  if (extent_store.count(id) <= 0) 
     extent_obj = new extent_value();
  else 
     extent_obj = extent_store[id];
  
  if (off < 0) 
     extent_obj->data = buf;
  else {
     if (off > extent_obj->ext_attr.size) {
         extent_obj->data.resize(off);
         extent_obj->data.append(buf);
     }
     else if (buf != "") 
	extent_obj->data.replace(off,buf.size(), buf);
     else
	extent_obj->data.resize(off);
  }
  extent_obj->ext_attr.mtime = extent_obj->ext_attr.ctime = time(NULL);
  extent_obj->ext_attr.size = extent_obj->data.size();
  extent_store[id] = extent_obj;
  printf("extent_server::put, extent_store: id %016llx, buf %s\n", id, buf.c_str());

  //return extent_protocol::IOERR;
  return extent_protocol::OK;
}

int extent_server::get(extent_protocol::extentid_t id, int off, unsigned int size, std::string &buf)
{
  // You fill this in for Lab 2.
  ScopedLock ml(&extstore_mutex);
  extent_value *extent_obj;
  if (extent_store.count(id) > 0) {
    printf("extent_server::get entry present for %016llx returning back! \n", id);
    extent_obj = extent_store[id];
    if (off < 0)
       buf = extent_obj->data;
    else if (extent_obj->ext_attr.size < off)
       buf = '\0';
    else 
       buf = extent_obj->data.substr(off, size);

    extent_obj->ext_attr.atime = time(NULL);
    extent_store[id] = extent_obj;
    printf("extent_server:: get buf:%s\n", buf.c_str());
    return extent_protocol::OK;
  }
  printf("extent_server::get entry not present for %016llx \n", id);
  return extent_protocol::NOENT;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
  extent_value *extent_obj;
  if (extent_store.count(id) > 0) {
  printf("extent_server::getattr returning from store for %016llx \n", id);
  extent_obj = extent_store[id];
  a.size = extent_obj->ext_attr.size;
  a.atime = extent_obj->ext_attr.atime;
  a.mtime = extent_obj->ext_attr.mtime;
  a.ctime = extent_obj->ext_attr.ctime;
  } else {
  //change once confident
  a.size = 0;
  a.atime = 0;
  a.mtime = 0;
  a.ctime = 0;
  }

  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  // You fill this in for Lab 2.
  ScopedLock ml(&extstore_mutex);
  printf("Removing file for %016llx \n", id);
  delete(extent_store[id]);
  extent_store.erase(id); 
//  return extent_protocol::IOERR;
  return extent_protocol::OK;
}

