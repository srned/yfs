#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client_cache.h"

class lock_release_impl : public lock_release_user {
    private:
        class extent_client *ec;
    public:
    lock_release_impl(extent_client *ec);
    virtual ~lock_release_impl() {};
    void dorelease(lock_protocol::lockid_t lid);
};

class yfs_client {
  extent_client *ec;
  lock_client_cache *lc;
  lock_release_impl *lu;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  static inum new_inum(bool isfile);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  int setattr(inum, fileinfo &);
  int createfile(inum, const char *, inum &, bool isfile);
  int createroot(inum, const char *);
  int lookup(inum, const char *, inum &);
  int readdir(inum, std::vector<dirent> &);
  int read(inum, off_t off, size_t size, std::string &);
  int write(inum, const char *, off_t off, size_t size);
  int unlink(inum, const char *);
};

#endif 
