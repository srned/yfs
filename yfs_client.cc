// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include "lock_client_cache.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <vector>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client_cache(lock_dst);
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

int limited_rand(int limit)
{
  int r, d = RAND_MAX / limit;
  limit *= d;
  do { r = rand(); } while (r >= limit);
  return r / d;
}

yfs_client::inum 
yfs_client::new_inum(bool isfile)
{
  yfs_client::inum finum;
  int rand_num = limited_rand(0x7FFFFFFF);
  if (isfile) 
    finum = rand_num | 0x0000000080000000;
//  else
//  finum = rand_num & 0x000000007FFFFFFF;
  printf("new_inum %016llx \n",finum);
  return finum;
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the file lock

  printf("yfs_client::getfile %016llx\n", inum);
  lc->acquire(inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("yfs_client::getfile %016llx -> sz %llu\n", inum, fin.size);

 release:
  lc->release(inum);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;
  // You modify this function for Lab 3
  // - hold and release the directory lock

  printf("yfs_client::getdir %016llx\n", inum);
  lc->acquire(inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  lc->release(inum);
  return r;
}

int
yfs_client::setattr(inum inum, fileinfo &fin)
{
  int r = OK;
  std::string file_buf;
  printf("yfs_client::setattr %016llx, fin.size %lld \n", inum, fin.size);
  lc->acquire(inum);
/*  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  if (fin.size == 0) {
    if (ec->put(file_inum, -1, 0, file_buf) != extent_protocol::OK) {
      r = IOERR;
      goto release;
    } 
  } else */
    if (ec->put(inum, fin.size, file_buf) != extent_protocol::OK) {
      r = IOERR;
      goto release;
    }

    
  
/*

if (fin.size < a.size) {
      file_buf.assign((a.size - fin.size), '\0');
      r = write(inum, file_buf.c_str(), fin.size, (a.size - fin.size));
  } else if (fin.size > a.size) {
      r = write(inum, file_buf.c_str(), a.size, (fin.size - a.size));

  }
  */
  r = OK; 
 
 release:
   lc->release(inum);
   return r;
}

int
yfs_client::createfile(inum p_inum, const char *name, inum &c_num, bool isfile)
{
  int r = OK, count = 0;
  std::string p_buf;
  char *cstr, *p;
  inum file_inum;
  bool child_lock = false;
  std::string file_buf("");
  lc->acquire(p_inum);
// Read Parent Dir and check if name already exists
  if (ec->get(p_inum, -1, 0, p_buf) != extent_protocol::OK) {
     printf("yfs_client::createfile %016llx parent dir not exist\n", p_inum);
     r = NOENT;
     goto release;
  }  
  cstr = new char[p_buf.size()+1];
  strcpy(cstr, p_buf.c_str());
  p=strtok (cstr, "/");
  while (p!=NULL) {
    printf("createfile: p %c\n", *p);
    // Skip its own dir name & inum
    if(count!=1 && count%2 == 1) {
       if((strlen(p) == strlen(name)) && (!strncmp(p, name, strlen(name)))) {
         delete[] cstr;
         r = EXIST;
         goto release;
      }
    }
  p=strtok(NULL,"/");
  count++;
  }
  delete[] cstr;
  file_inum = new_inum(isfile);
  lc->acquire(file_inum);   //Acquire for child
  child_lock = true;
  if(!isfile)
     file_buf.append('/'+filename(file_inum)+"/"+name);
  //Create an empty extent for ino
  if (ec->put(file_inum, -1, file_buf) != extent_protocol::OK) {
     r = IOERR;
     goto release;
  }
  //Add a <name, ino> entry into @parent
  p_buf.append('/'+filename(file_inum)+"/"+name);
  if (ec->put(p_inum,-1, p_buf) != extent_protocol::OK) {
     r = IOERR;
     goto release;
  }
  c_num = file_inum;
 
 release:
  if (child_lock)
      lc->release(file_inum);
  lc->release(p_inum);
  return r;
}

/* Generic Format: /1/rootname/inum1/name1/inum2/name2  */

int
yfs_client::createroot(inum in_inum, const char *name) {
  int r = OK;
  std::string buf('/'+filename(in_inum)+'/'+name);
  lc->acquire(in_inum);
  if (ec->put(in_inum, -1, buf) != extent_protocol::OK) {
    r = IOERR;
    return r;
  }
 lc->release(in_inum);
 return r;
}

int
yfs_client::lookup(inum p_inum, const char *name, inum &c_inum) {
  int r = OK, count = 0;
  std::string p_buf, inum_buf;
  char *cstr, *p;
  inum curr_inum;
// Read Parent Dir and check if name already exists
  lc->acquire(p_inum);
  if (ec->get(p_inum, -1, 0, p_buf) != extent_protocol::OK) {
     r = NOENT;
     goto release;
  }
  cstr = new char[p_buf.size()+1];
  strcpy(cstr, p_buf.c_str());
  p=strtok (cstr, "/");
  while (p!=NULL) {
    // Skip its own dir name & inum
    if(count!=1 && count%2 == 1) {
	if((strlen(p) == strlen(name)) && (!strncmp(p, name, strlen(name)))) {
         delete[] cstr;
         r = OK;
	 c_inum = curr_inum;
         goto release;
      }
    }
    else {
      inum_buf = p;
      curr_inum = n2i(inum_buf);
    }
  p=strtok(NULL,"/");
  count++;
  }
  delete[] cstr;
  r = NOENT; 
  release:
    lc->release(p_inum);
    return r;
}

int
yfs_client::readdir(inum p_inum, std::vector<dirent> &r_dirent) {
  int r = OK, count = 0;
  std::string p_buf, inum_buf;
  char *cstr, *p;
  dirent curr_dirent;
// Read Parent Dir and check if name already exists
  lc->acquire(p_inum);
  if (ec->get(p_inum, -1, 0, p_buf) != extent_protocol::OK) {
     r = NOENT;
     goto release;
  }
  cstr = new char[p_buf.size()+1];
  strcpy(cstr, p_buf.c_str());
  p=strtok (cstr, "/");
  while (p!=NULL) {
    // Skip its own dir name & inum
    if(count && count!=1) {
      if(count%2 == 1) {
      curr_dirent.name = p;
      r_dirent.push_back(curr_dirent);
      }
      else {
      inum_buf = p;
      curr_dirent.inum = n2i(inum_buf);
      }
    }
    p=strtok(NULL,"/");
    count++;
  }
  delete[] cstr;
  r = OK;
  release:
    lc->release(p_inum);
    return r;
}

int
yfs_client::read(inum in_inum, off_t off, size_t size, std::string &buf) {
   int r = OK;
   printf("yfs_client::read %016llx, off %ld size %u \n", in_inum, (long int) off, size);
   lc->acquire(in_inum);
  /* fileinfo fin;

   if (getfile(in_inum, fin) != OK) {
     r = IOERR;
     goto release;
   }

   if (off > fin.size) {
      buf = '\0';
      r = OK;
      goto release;
   }

   if ((off + size) > fin.size) 
	size = fin.size - off;
    */ 
   if (ec->get(in_inum, (int) off, (unsigned int) size, buf) != extent_protocol::OK) {
          r = IOERR;
          goto release;
   }
    
   r = OK;

 release:
   lc->release(in_inum);
   return r; 
}

int
yfs_client::write(inum in_inum, const char *buf, off_t off, size_t size) {
   std::string file_buf;
   int r = OK;
   printf("yfs_client::write %016llx, off %ld size %u \n", in_inum, (long int) off, size);     lc->acquire(in_inum);
/*   fileinfo fin;

   if (getfile(in_inum, fin) != OK) {
     r = IOERR;
     goto release;
   }

   if (off > fin.size) {
      file_buf.assign((off - fin.size), '\0');
      size = size + off - fin.size;
      off = fin.size;
   }
*/
   file_buf.append(buf, size);
   if (ec->put(in_inum, (int) off, file_buf) != extent_protocol::OK) {
      r = IOERR;
      goto release;
   }

   r = OK;

 release:
  lc->release(in_inum);
  return r;
}


// Remove the file named @name from directory @parent.
// Free the file's extent.
// If the file doesn't exist, indicate error ENOENT.
//
// Do *not* allow unlinking of a directory.
//

int 
yfs_client::unlink(inum p_inum, const char *name) {
 int r = NOENT, count = 0;
 std::string p_buf, inum_buf, unlink_buf;
 char *cstr, *p;
 inum curr_inum, c_inum;
 bool child_lock = false;
// Read Parent Dir and check if name already exists
  lc->acquire(p_inum);
  if (ec->get(p_inum, -1, 0, p_buf) != extent_protocol::OK) {
     r = NOENT;
     goto release;
  }
  cstr = new char[p_buf.size()+1];
  strcpy(cstr, p_buf.c_str());
  p=strtok (cstr, "/");
  while (p!=NULL) {
    // Skip its own dir name & inum
    if(count!=1 && count%2 == 1) {
      if((strlen(p) == strlen(name)) && (!strncmp(p, name, strlen(name)))) {
         c_inum = curr_inum;
         r = OK;
         break;
      }
    }
    else {
      inum_buf = p;
      curr_inum = n2i(inum_buf);
    }
  p=strtok(NULL,"/");
  count++;
  }
  delete[] cstr;
  if (r == NOENT)
    goto release;

  if (isdir(c_inum)) {
    r = IOERR;
    goto release;
  }

//Should delete the file now..
  lc->acquire(c_inum);
  child_lock = true;
  if (ec->remove(c_inum) != extent_protocol::OK) {
     r = IOERR;
     goto release;
  }

//update parent dir entry
   unlink_buf = "/" + inum_buf + "/" + name;
   p_buf.erase(p_buf.find(unlink_buf), unlink_buf.length());
   
   if (ec->put(p_inum,-1, p_buf) != extent_protocol::OK) {
     r = IOERR;
     goto release;
  }
  r = OK;

release:
  if (child_lock)
      lc->release(c_inum);
  lc->release(p_inum);
  return r;

}

