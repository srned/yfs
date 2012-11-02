yfs
===

http://pdos.csail.mit.edu/6.824/labs/index.html


----------------------------------
4. Caching Locks
----------------------------------

Design
======
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

                Lock Server Cache
                -----------------
                Server is generally designed to be non-blocking and care is taken not to make RPC calls within acquire/release calls.

                tLockMap: Stores the state of locks(as per below struct) in a Map.
                struct lock_cache_value {
                l_state lock_state;
                std::string owner_clientid; // used to send revoke
                std::string retrying_clientid; // used to match with incoming acquire request
                std::list<std::string> waiting_clientids; // need to send retry
                };
                lmap_mutex: Global lock to synchronize

                It supports acquire, release

                Releaser, Retryer Threads:
                -On init, we spawn two threads, releaser and retryer (mutex and CVs are dedicated for each for synchronization)
                -Releaser will listen on a list and will send revoke RPC to client who owns the lock.
                -Retryer will listen on a list and will send retry RPC to client who is scheduled to own next

                Acquire:
                Lock is granted only if current state is
                1)LOCKFREE (or) (RETRYING and incoming id matches the retrying_clientid so
                that it is granted to the appropriate client. Also, waiting_clientids is probed and if there is any client waiting,
                we schedule a revoke request to be sent by Releaser thread to avoid starvation
                2) LOCKED
                we schedule a revoke request to be sent by Releaser thread and client to added to waiting_clientids list

                Release:
                State is set to LOCKFREE and if there are clients waiting for this lock, then we schedule a retry request
                to be sent to the scheduled owner

                Changes to rpc/rpc.cc
                ---------------------
                This is to fix the solution that addresses atmost once delivery of RPC on server side.
                Essentially, we respond back with INPROGRESS if cb_present is false.

                Validation
                ==========
                It passes all tests from lab3/lab4 scripts with/witout RPC LOSSY



-----------------------------------
5. Caching Extent
-----------------------------------

Caching at Extent Client
------------------------
In this exercise, we cache extent data/attributes at client end to avoid roundtrips to extent server. We use lock server/client to acquire locks before caching at the extent client side.

get():
If the inode is not present in the hash map, then we invoke load which gets the attributes and data from extent server and populates
struct extent_value {
bool dirty;  // To track puts
std::string data;
extent_protocol::attr ext_attr;
};

put():
If the inode is not present in extent server, we simply cache it in client and finally if the inode is still around, then we dump it back to server on flush() call.

remove():
We remove the inode from hash map

flush():
On release of lock from releaser thread in lock_client_cache, the hook dorelease invokes flush() in extent client. If the data is dirty, we "put" it to server, if not, we "remove" it from the server and delete the entry from client cache if needed.

BUGS FIXED:
1) getattr on extent server was returning OK in all scenarios even if the inode is not in the system
2) Out of Order revoke/retry messages. On lock client, dorelease will be called only "if" the locked has been acquired atleast "once".  doflush flag is set to flag in NONE case when lock state is ACQUIRING. It is then set to true once the state changes to LOCKED state. This ensures that extent_client infact has acquired the lock and so it is valid to call doflush()

test.sh
=======
This script simply does regression test using all test scripts with LOSSY set to 0 and 5.


-------------------------------
6. Paxos
-------------------------------
Design
======
In this exercise, we implement PAXOS algorithm. The stack consists of RSM, Config and Paxos module.
Config runs an heartbeat thread to monitor other nodes and invokes remove if necessary.
One important convention that is used is that higher layers can hold mutex while calling down but not vice versa. 

Paxos Module
============
This consists of Proposer and Acceptor.

Proposer
--------
Implements below algorithm
----------------------------------------------------------------
n(prop_t):  proposal number
v:          value to be agreed on (list of alive nodes)
instance#:  unique instance number
n_h:        highest prepare seen (set during prepare and accept)
instance_h: highest instance accepted (set during decide)
n_a, v_a:   highest accept seen
----------------------------------------------------------------

proposer run(instance, v):
    choose n, unique and higher than any n seen so far
    send prepare(instance, n) to all servers including self
    if oldinstance(instance, instance_value) from any node:
        commit to the instance_value locally
        else if prepare_ok(n_a, v_a) from majority:
            v' = v_a with highest n_a; choose own v otherwise
            send accept(instance, n, v') to all
            if accept_ok(n) from majority:
                send decided(instance, v') to all

Acceptor
--------

acceptor prepare(instance, n) handler:
    if instance <= instance_h
        reply oldinstance(instance, instance_value)
    else if n > n_h
        n_h = n
        reply prepare_ok(n_a, v_a)
    else
        reply prepare_reject

acceptor accept(instance, n, v) handler:
    if n >= n_h
        n_a = n
        v_a = v
        reply accept_ok(n)
    else
        reply accept_reject

acceptor decide(instance, v) handler:
    paxos_commit(instance, v)

---------------
7. RSM
---------------

Design
======
Replicated State Machine (RSM)
-----------------
In this exercise, we implement RSM version of lock service. 

1) Replicable caching lock server
---------------------------------
In this step, we ported the previous cache implementation. This guarantees there will not be any deadlock caused by RSM layer
So, we dont hold locks across RPC calls. Use background threads with retryer/releaser threads to update state on server side
that needs response from client

2) RSM without failures
-----------------------
-The RSM client sends its request to the master by calling the invoke RPC.(handle if RSM is in view change undergoing paxos or if this is not primary anymore)
-The master assigns the client request the next viewstamp in sequence, and sends the invoke RPC to each slave
-If the request has the expected viewstamp, the slave executes the request locally and replies OK to the master.
-The master executes the request locally, and replies back to the client.

Only the primary lock_server_cache_rsm will communicate directly to clients. 

3) Cope with Backup Failures and Implement state transfer
---------------------------------------------------------
Upon detecting failure(heartbeat thread in config layer)/addition of new node(recovery thread in rsm layer), paxos will be kicked off and once it settles,
each replica should get state from new master. Master will hold still until all backups have synced (implemented with cond var).
-We serialize the state in master and implement marshal_state and unmarshal_state methods. This will be used on master and replica ends
-sync_with_backups in recovery thread in primary will wait for all replicas to sync up.
-sync_with_primary in recovery thread in backups will sync seperately to master.
-statetransferdone is called from backup to convey that sync is done
-transferdonereq is handler for above on primary

4) Cope with Primary Failures
-----------------------------
-Clients call init_members() if primary returns NOTPRIMARY response. this way we reinitialize the view
-If there is no response from primary at all, then, we assume the next member from the view as primary and try until succeeds.

5) Complicated Failures
-----------------------
-Master fails after 1 slave gets the request and second slave is in process of updating state.
-Fail one of slave before responding

=========
Conclusion
==========
With this, we conclude this series of exercises. Happy Coding!

Best,
srned



