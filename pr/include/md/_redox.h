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
#else
#error "Unknown CPU architecture"
#endif
#define PR_DLL_SUFFIX       ".so"

#define _PR_VMBASE              0x30000000
#define _PR_STACK_VMBASE    0x50000000
#define _MD_DEFAULT_STACK_SIZE  65536L
#define _MD_MMAP_FLAGS          MAP_PRIVATE

#undef  HAVE_STACK_GROWING_UP

/*
 * Elf redox supports dl* functions
 */
#define HAVE_DLL
#define USE_DLFCN

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

#define USE_SETJMP
#define _PR_USE_POLL
#define _PR_POLL_AVAILABLE
#define _PR_STAT_HAS_ONLY_ST_ATIME
#define _PR_HAVE_CLOCK_MONOTONIC
#define _PR_HAVE_GETHOSTBYNAME2
#define _PR_HAVE_GETADDRINFO
#define _PR_HAVE_INET_NTOP
#define _PR_INET6
#define _PR_INET6_PROBE
#define _PR_NO_LARGE_FILES

extern void _MD_CleanupBeforeExit(void);
extern void _MD_EarlyInit(void);
#define _MD_CLEANUP_BEFORE_EXIT _MD_CleanupBeforeExit
#define _MD_EARLY_INIT          _MD_EarlyInit
#define _MD_FINAL_INIT          _PR_UnixInit

#define _MD_SELECT select
#include <poll.h>
#define _MD_POLL poll

/* For writev() */
#include <sys/uio.h>

#endif /* nspr_redox_defs_h___ */
