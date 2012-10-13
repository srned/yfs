// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "tprintf.h"

// The calls assume that the caller holds a lock on the extent

extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
  make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    tprintf("extent_client: bind failed\n");
  }
}

extent_protocol::status 
extent_client::load(extent_protocol::extentid_t eid) {
    extent_protocol::status ret = extent_protocol::OK;
    // Get Attributes first
    extent_protocol::attr attr;
    ret = cl->call(extent_protocol::getattr, eid, attr);
    if (ret == extent_protocol::OK) {
        extent_value *extent_obj;
        extent_obj = new extent_value();
        extent_obj->ext_attr.size = attr.size;
        extent_obj->ext_attr.atime = attr.atime;
        extent_obj->ext_attr.mtime = attr.mtime;
        extent_obj->ext_attr.ctime = attr.ctime;
        extent_obj->dirty = false;
        tprintf("extent cache: load, get from server for %016llx \n", eid);
        ret = cl->call(extent_protocol::get, eid, 0, attr.size, extent_obj->data);
        if (ret != extent_protocol::OK) {
            tprintf("extent cache: load, server returned error for %016llx \n", eid);
            delete(extent_obj);
            return ret;
        }
        tprintf("extent cache: load, get from server for %016llx data %s\n", eid, 
                extent_obj->data.c_str());
        extent_cache[eid] = extent_obj;
    }
    return ret;
}

extent_protocol::status
extent_client::flush(extent_protocol::extentid_t eid) 
{
    extent_protocol::status ret = extent_protocol::OK;
    int r;
    ScopedLock ml(&extcache_mutex);
    tprintf("extent_client:flush for %016llx \n", eid);
    extent_value *extent_obj;
    if (extent_cache.count(eid) > 0) {
        extent_obj = extent_cache[eid];
        if (extent_obj->dirty) {
            tprintf("flush:Calling put for %016llx with value %s\n", eid, extent_obj->data.c_str());
            ret = cl->call(extent_protocol::put, eid, -1, extent_obj->data, r);
        }
        tprintf("flush: not dirty, so just deleting in local\n");
        delete(extent_cache[eid]);
        extent_cache.erase(eid);
    } else {
        tprintf("flush: not in extent cache, calling remove for %016llx\n", eid);
        ret = cl->call(extent_protocol::remove, eid, r);
    }
    return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, int off, unsigned int size, std::string &buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    ScopedLock ml(&extcache_mutex);
    extent_value *extent_obj;
    while(true) {
        if (extent_cache.count(eid) > 0) {
            tprintf("extent_client::get entry present for %016llx returning back! \n", eid);
            extent_obj = extent_cache[eid];
            if (off < 0)
                buf = extent_obj->data;
            else if (extent_obj->ext_attr.size < off)
                buf = '\0';
            else 
                buf = extent_obj->data.substr(off, size);

            extent_obj->ext_attr.atime = time(NULL);
            extent_cache[eid] = extent_obj;
            tprintf("extent_client:: from cache get buf:%s\n", buf.c_str());
            return extent_protocol::OK;
        }
        else {
            tprintf("extent_client::get entry not present for %016llx loading now\n", eid);
            ret = load(eid);
            if (ret == extent_protocol::OK) continue;
            break;
        }
    }
    return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
    extent_protocol::status ret = extent_protocol::OK;
    ScopedLock ml(&extcache_mutex);
    //extent_value *extent_obj;
    while(true) {
        if (extent_cache.count(eid) > 0) {
            //extent_obj = extent_store[eid];
            attr = extent_cache[eid]->ext_attr;
            return extent_protocol::OK;
        }
        else {
            tprintf("extent_client::getattr entry not present for %016llx loading now\n", eid);
            ret = load(eid);
            if (ret == extent_protocol::OK) continue;
            break;
        }
    }
    return ret;
}

extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, int off, std::string buf)
{
    extent_protocol::status ret = extent_protocol::OK;
    ScopedLock ml(&extcache_mutex);
    extent_value *extent_obj;
    while(true) {
        if (extent_cache.count(eid) > 0) {
            extent_obj = extent_cache[eid];
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
            extent_obj->dirty = true;
            extent_cache[eid] = extent_obj;
            tprintf("extent_client::put, extent_store: eid %016llx, buf %s\n", eid, buf.c_str());
            return extent_protocol::OK;
        }
        else {
            tprintf("extent_client::put entry not present for %016llx loading now\n", eid);
            ret = load(eid);
            if (ret == extent_protocol::OK) continue;
            else {
                //int r;
                //ret = cl->call(extent_protocol::put, eid, off, buf, r);
                //ret = load(eid);
                extent_obj = new extent_value();
                extent_cache[eid] = extent_obj;
                tprintf("extent_client::put entry not present for %016llx local put\n", eid);
                continue;
            }
            break;
        }
    }
    return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  extent_protocol::status ret = extent_protocol::OK;
  ScopedLock ml(&extcache_mutex);
  tprintf("Removing file for %016llx \n", eid);
  delete(extent_cache[eid]);
  extent_cache.erase(eid);
  //int r;
  //ret = cl->call(extent_protocol::remove, eid, r);
  return ret;
}


