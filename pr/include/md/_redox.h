/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#ifndef nspr_redox_defs_h___
#define nspr_redox_defs_h___

#include "prthread.h"

/*
 * Internal configuration macros
 */

#define PR_LINKER_ARCH  "redox"
#define _PR_SI_SYSNAME  "REDOX"
#if defined(__x86_64__)
#define _PR_SI_ARCHITECTURE "x86-64"
#elif defined(__i686__)
#define _PR_SI_ARCHITECTURE "x86"
#elif defined(__aarch64__)
#define _PR_SI_ARCHITECTURE "aarch64"
#elif defined(__riscv) && (__riscv_xlen == 64)
#define _PR_SI_ARCHITECTURE "riscv64"
#else
#error "Unknown CPU architecture"
#endif
#define PR_DLL_SUFFIX       ".so"

#define _PR_VMBASE              0x30000000
#define _PR_STACK_VMBASE    0x50000000
#define _MD_DEFAULT_STACK_SIZE  65536L
#define _MD_MMAP_FLAGS          MAP_PRIVATE

#if defined(__aarch64__)
#define _MD_MINIMUM_STACK_SIZE  0x20000
#endif

#undef  HAVE_STACK_GROWING_UP

/*
 * Elf redox supports dl* functions
 */
#define HAVE_DLL
#define USE_DLFCN

#if defined(__i686__)
#define _PR_HAVE_ATOMIC_OPS
#define _MD_INIT_ATOMIC()
extern PRInt32 _PR_x86_AtomicIncrement(PRInt32 *val);
#define _MD_ATOMIC_INCREMENT          _PR_x86_AtomicIncrement
extern PRInt32 _PR_x86_AtomicDecrement(PRInt32 *val);
#define _MD_ATOMIC_DECREMENT          _PR_x86_AtomicDecrement
extern PRInt32 _PR_x86_AtomicAdd(PRInt32 *ptr, PRInt32 val);
#define _MD_ATOMIC_ADD                _PR_x86_AtomicAdd
extern PRInt32 _PR_x86_AtomicSet(PRInt32 *val, PRInt32 newval);
#define _MD_ATOMIC_SET                _PR_x86_AtomicSet
#endif

#if defined(__x86_64__)
#define _PR_HAVE_ATOMIC_OPS
#define _MD_INIT_ATOMIC()
extern PRInt32 _PR_x86_64_AtomicIncrement(PRInt32 *val);
#define _MD_ATOMIC_INCREMENT          _PR_x86_64_AtomicIncrement
extern PRInt32 _PR_x86_64_AtomicDecrement(PRInt32 *val);
#define _MD_ATOMIC_DECREMENT          _PR_x86_64_AtomicDecrement
extern PRInt32 _PR_x86_64_AtomicAdd(PRInt32 *ptr, PRInt32 val);
#define _MD_ATOMIC_ADD                _PR_x86_64_AtomicAdd
extern PRInt32 _PR_x86_64_AtomicSet(PRInt32 *val, PRInt32 newval);
#define _MD_ATOMIC_SET                _PR_x86_64_AtomicSet
#endif


#if defined(__arm__) || defined(__aarch64__)
#if defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4)
/* Use GCC built-in functions */
#define _PR_HAVE_ATOMIC_OPS
#define _MD_INIT_ATOMIC()

#define _MD_ATOMIC_INCREMENT(ptr) __sync_add_and_fetch(ptr, 1)
#define _MD_ATOMIC_DECREMENT(ptr) __sync_sub_and_fetch(ptr, 1)
#define _MD_ATOMIC_SET(ptr, nv) __sync_lock_test_and_set(ptr, nv)
#define _MD_ATOMIC_ADD(ptr, i) __sync_add_and_fetch(ptr, i)
#endif
#endif /* __arm__ */

#define USE_SETJMP
#define _PR_POLL_AVAILABLE
#undef _PR_USE_POLL
#define _PR_STAT_HAS_ONLY_ST_ATIME
#define _PR_NO_LARGE_FILES
#define _PR_INET6
#define _PR_HAVE_INET_NTOP
#define _PR_HAVE_GETHOSTBYNAME2
#define _PR_HAVE_GETADDRINFO
#define _PR_INET6_PROBE

#define gethostbyname2(name, af) gethostbyname(name)

#ifdef _PR_PTHREADS

extern void _MD_CleanupBeforeExit(void);
#define _MD_CLEANUP_BEFORE_EXIT _MD_CleanupBeforeExit

#else  /* ! _PR_PTHREADS */

#include <setjmp.h>

#define PR_CONTEXT_TYPE sigjmp_buf

#define CONTEXT(_th) ((_th)->md.context)

#if defined(__i686__)
/* Intel based Redox */
#define _MD_GET_SP(_t) (_t)->md.context[0].__jmpbuf[JB_SP]
#define _MD_SET_FP(_t, val) ((_t)->md.context[0].__jmpbuf[JB_BP] = val)
#define _MD_GET_SP_PTR(_t) &(_MD_GET_SP(_t))
#define _MD_GET_FP_PTR(_t) (&(_t)->md.context[0].__jmpbuf[JB_BP])
#define _MD_SP_TYPE int

#elif defined(__arm__)
/* ARM/Redox */
#ifdef __ARM_EABI__
/* EABI */
#define _MD_GET_SP(_t) (_t)->md.context[0].__jmpbuf[8]
#define _MD_SET_FP(_t, val) ((_t)->md.context[0].__jmpbuf[7] = (val))
#define _MD_GET_SP_PTR(_t) &(_MD_GET_SP(_t))
#define _MD_GET_FP_PTR(_t) (&(_t)->md.context[0].__jmpbuf[7])
#define _MD_SP_TYPE __ptr_t
#else /* __ARM_EABI__ */
/* old ABI */
#define _MD_GET_SP(_t) (_t)->md.context[0].__jmpbuf[20]
#define _MD_SET_FP(_t, val) ((_t)->md.context[0].__jmpbuf[19] = (val))
#define _MD_GET_SP_PTR(_t) &(_MD_GET_SP(_t))
#define _MD_GET_FP_PTR(_t) (&(_t)->md.context[0].__jmpbuf[19])
#define _MD_SP_TYPE __ptr_t
#endif /* __ARM_EABI__ */
#endif /* __i686__ */


#define _MD_SWITCH_CONTEXT(_thread)  \
    if (!sigsetjmp(CONTEXT(_thread), 1)) {  \
    (_thread)->md.errcode = errno;  \
    _PR_Schedule();  \
    }

/*
** Restore a thread context, saved by _MD_SWITCH_CONTEXT
*/
#define _MD_RESTORE_CONTEXT(_thread) \
{   \
    errno = (_thread)->md.errcode;  \
    _MD_SET_CURRENT_THREAD(_thread);  \
    siglongjmp(CONTEXT(_thread), 1);  \
}

/* Machine-dependent (MD) data structures */

struct _MDThread {
    PR_CONTEXT_TYPE context;
    void *sp;
    void *fp;
    int id;
    int errcode;
};

struct _MDThreadStack {
    PRInt8 notused;
};

struct _MDLock {
    PRInt8 notused;
};

struct _MDSemaphore {
    PRInt8 notused;
};

struct _MDCVar {
    PRInt8 notused;
};

struct _MDSegment {
    PRInt8 notused;
};

/*
 * md-specific cpu structure field
 */
#include <sys/time.h>  /* for FD_SETSIZE */
#define _PR_MD_MAX_OSFD FD_SETSIZE

struct _MDCPU_Unix {
    PRCList ioQ;
    PRUint32 ioq_timeout;
    PRInt32 ioq_max_osfd;
    PRInt32 ioq_osfd_cnt;
#ifndef _PR_USE_POLL
    fd_set fd_read_set, fd_write_set, fd_exception_set;
    PRInt16 fd_read_cnt[_PR_MD_MAX_OSFD],fd_write_cnt[_PR_MD_MAX_OSFD],
            fd_exception_cnt[_PR_MD_MAX_OSFD];
#else
    struct pollfd *ioq_pollfds;
    int ioq_pollfds_size;
#endif  /* _PR_USE_POLL */
};

#define _PR_IOQ(_cpu)           ((_cpu)->md.md_unix.ioQ)
#define _PR_ADD_TO_IOQ(_pq, _cpu) PR_APPEND_LINK(&_pq.links, &_PR_IOQ(_cpu))
#define _PR_FD_READ_SET(_cpu)       ((_cpu)->md.md_unix.fd_read_set)
#define _PR_FD_READ_CNT(_cpu)       ((_cpu)->md.md_unix.fd_read_cnt)
#define _PR_FD_WRITE_SET(_cpu)      ((_cpu)->md.md_unix.fd_write_set)
#define _PR_FD_WRITE_CNT(_cpu)      ((_cpu)->md.md_unix.fd_write_cnt)
#define _PR_FD_EXCEPTION_SET(_cpu)  ((_cpu)->md.md_unix.fd_exception_set)
#define _PR_FD_EXCEPTION_CNT(_cpu)  ((_cpu)->md.md_unix.fd_exception_cnt)
#define _PR_IOQ_TIMEOUT(_cpu)       ((_cpu)->md.md_unix.ioq_timeout)
#define _PR_IOQ_MAX_OSFD(_cpu)      ((_cpu)->md.md_unix.ioq_max_osfd)
#define _PR_IOQ_OSFD_CNT(_cpu)      ((_cpu)->md.md_unix.ioq_osfd_cnt)
#define _PR_IOQ_POLLFDS(_cpu)       ((_cpu)->md.md_unix.ioq_pollfds)
#define _PR_IOQ_POLLFDS_SIZE(_cpu)  ((_cpu)->md.md_unix.ioq_pollfds_size)

#define _PR_IOQ_MIN_POLLFDS_SIZE(_cpu)  32

struct _MDCPU {
    struct _MDCPU_Unix md_unix;
};

#define _MD_INIT_LOCKS()
#define _MD_NEW_LOCK(lock) PR_SUCCESS
#define _MD_FREE_LOCK(lock)
#define _MD_LOCK(lock)
#define _MD_UNLOCK(lock)
#define _MD_INIT_IO()
#define _MD_IOQ_LOCK()
#define _MD_IOQ_UNLOCK()

extern PRStatus _MD_InitializeThread(PRThread *thread);

#define _MD_INIT_RUNNING_CPU(cpu)       _MD_unix_init_running_cpu(cpu)
#define _MD_INIT_THREAD                 _MD_InitializeThread
#define _MD_EXIT_THREAD(thread)
#define _MD_SUSPEND_THREAD(thread)      _MD_suspend_thread
#define _MD_RESUME_THREAD(thread)       _MD_resume_thread
#define _MD_CLEAN_THREAD(_thread)

extern PRStatus _MD_CREATE_THREAD(
    PRThread *thread,
    void (*start) (void *),
    PRThreadPriority priority,
    PRThreadScope scope,
    PRThreadState state,
    PRUint32 stackSize);
extern void _MD_SET_PRIORITY(struct _MDThread *thread, PRUintn newPri);
extern PRStatus _MD_WAIT(PRThread *, PRIntervalTime timeout);
extern PRStatus _MD_WAKEUP_WAITER(PRThread *);
extern void _MD_YIELD(void);

#endif /* ! _PR_PTHREADS */

extern void _MD_EarlyInit(void);

#define _MD_EARLY_INIT                  _MD_EarlyInit
#define _MD_FINAL_INIT                  _PR_UnixInit
#define _PR_HAVE_CLOCK_MONOTONIC

/*
 * We wrapped the select() call.  _MD_SELECT refers to the built-in,
 * unwrapped version.
 */
#define _MD_SELECT __select

#ifdef _PR_POLL_AVAILABLE
#include <sys/poll.h>
extern int __syscall_poll(struct pollfd *ufds, unsigned long int nfds,
                          int timeout);
#define _MD_POLL __syscall_poll
#endif

/* For writev() */
#include <sys/uio.h>

extern void _MD_redox_map_sendfile_error(int err);

#endif /* nspr_redox_defs_h___ */
