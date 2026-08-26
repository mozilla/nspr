/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
** File:            ptsynch.c
** Descritpion:        Implemenation for thread synchronization using pthreads
** Exports:            prlock.h, prcvar.h, prmon.h, prcmon.h
*/

#if defined(_PR_PTHREADS)

#include "primpl.h"

#include <string.h>
#include <pthread.h>
#include <sys/time.h>

static pthread_mutexattr_t _pt_mattr;
static pthread_condattr_t _pt_cvar_attr;

#if defined(DEBUG)
extern PTDebug pt_debug; /* this is shared between several modules */
#endif                   /* defined(DEBUG) */

#if defined(FREEBSD)
/*
 * On older versions of FreeBSD, pthread_mutex_trylock returns EDEADLK.
 * Newer versions return EBUSY.  We still need to support both.
 */
static int
pt_pthread_mutex_is_locked(pthread_mutex_t* m)
{
    int rv = pthread_mutex_trylock(m);
    return (EBUSY == rv || EDEADLK == rv);
}
#endif

/**************************************************************/
/**************************************************************/
/*****************************LOCKS****************************/
/**************************************************************/
/**************************************************************/

void
_PR_InitLocks(void)
{
    int rv;
    rv = _PT_PTHREAD_MUTEXATTR_INIT(&_pt_mattr);
    PR_ASSERT(0 == rv);

#if (defined(LINUX) &&                                               \
     (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 2))) || \
    (defined(FREEBSD) && __FreeBSD_version > 700055)
    rv = pthread_mutexattr_settype(&_pt_mattr, PTHREAD_MUTEX_ADAPTIVE_NP);
    PR_ASSERT(0 == rv);
#endif

    rv = _PT_PTHREAD_CONDATTR_INIT(&_pt_cvar_attr);
    PR_ASSERT(0 == rv);
}

static void
pt_PostNotifies(PRLock* lock, PRBool unlock)
{
    PRIntn index, rv;
    _PT_Notified post;
    _PT_Notified *notified, *prev = NULL;
    /*
     * Time to actually notify any conditions that were affected
     * while the lock was held. Get a copy of the list that's in
     * the lock structure and then zero the original. If it's
     * linked to other such structures, we own that storage.
     */
    post = lock->notified; /* a safe copy; we own the lock */

#if defined(DEBUG)
    memset(&lock->notified, 0, sizeof(_PT_Notified)); /* reset */
#else
    lock->notified.length = 0; /* these are really sufficient */
    lock->notified.link = NULL;
#endif

    /* should (may) we release lock before notifying? */
    if (unlock) {
        rv = pthread_mutex_unlock(&lock->mutex);
        PR_ASSERT(0 == rv);
    }

    notified = &post; /* this is where we start */
    do {
        for (index = 0; index < notified->length; ++index) {
            PRCondVar* cv = notified->cv[index].cv;
            PR_ASSERT(NULL != cv);
            PR_ASSERT(0 != notified->cv[index].times);
            if (-1 == notified->cv[index].times) {
                rv = pthread_cond_broadcast(&cv->cv);
                PR_ASSERT(0 == rv);
            } else {
                while (notified->cv[index].times-- > 0) {
                    rv = pthread_cond_signal(&cv->cv);
                    PR_ASSERT(0 == rv);
                }
            }
#if defined(DEBUG)
            pt_debug.cvars_notified += 1;
            if (0 > PR_ATOMIC_DECREMENT(&cv->notify_pending)) {
                pt_debug.delayed_cv_deletes += 1;
                PR_DestroyCondVar(cv);
            }
#else  /* defined(DEBUG) */
            if (0 > PR_ATOMIC_DECREMENT(&cv->notify_pending)) {
                PR_DestroyCondVar(cv);
            }
#endif /* defined(DEBUG) */
        }
        prev = notified;
        notified = notified->link;
        if (&post != prev) {
            PR_DELETE(prev);
        }
    } while (NULL != notified);
} /* pt_PostNotifies */

PR_IMPLEMENT(PRLock*)
PR_NewLock(void)
{
    PRIntn rv;
    PRLock* lock;

    if (!_pr_initialized) {
        _PR_ImplicitInitialization();
    }

    lock = PR_NEWZAP(PRLock);
    if (lock != NULL) {
        rv = _PT_PTHREAD_MUTEX_INIT(lock->mutex, _pt_mattr);
        PR_ASSERT(0 == rv);
    }
#if defined(DEBUG)
    pt_debug.locks_created += 1;
#endif
    return lock;
} /* PR_NewLock */

PR_IMPLEMENT(void)
PR_DestroyLock(PRLock* lock)
{
    PRIntn rv;
    PR_ASSERT(NULL != lock);
    PR_ASSERT(PR_FALSE == lock->locked);
    PR_ASSERT(0 == lock->notified.length);
    PR_ASSERT(NULL == lock->notified.link);
    rv = pthread_mutex_destroy(&lock->mutex);
    PR_ASSERT(0 == rv);
#if defined(DEBUG)
    memset(lock, 0xaf, sizeof(PRLock));
    pt_debug.locks_destroyed += 1;
#endif
    PR_Free(lock);
} /* PR_DestroyLock */

PR_IMPLEMENT(void)
PR_Lock(PRLock* lock)
{
    /* Nb: PR_Lock must not call PR_GetCurrentThread to access the |id| or
     * |tid| field of the current thread's PRThread structure because
     * _pt_root calls PR_Lock before setting thred->id and thred->tid. */
    PRIntn rv;
    PR_ASSERT(lock != NULL);
    rv = pthread_mutex_lock(&lock->mutex);
    PR_ASSERT(0 == rv);
    PR_ASSERT(0 == lock->notified.length);
    PR_ASSERT(NULL == lock->notified.link);
    PR_ASSERT(PR_FALSE == lock->locked);
    /* Nb: the order of the next two statements is not critical to
     * the correctness of PR_AssertCurrentThreadOwnsLock(), but
     * this particular order makes the assertion more likely to
     * catch errors. */
    lock->owner = pthread_self();
    lock->locked = PR_TRUE;
#if defined(DEBUG)
    pt_debug.locks_acquired += 1;
#endif
} /* PR_Lock */

PR_IMPLEMENT(PRStatus)
PR_Unlock(PRLock* lock)
{
    pthread_t self = pthread_self();
    PRIntn rv;

    PR_ASSERT(lock != NULL);
    PR_ASSERT(_PT_PTHREAD_MUTEX_IS_LOCKED(lock->mutex));
    PR_ASSERT(PR_TRUE == lock->locked);
    PR_ASSERT(pthread_equal(lock->owner, self));

    if (!lock->locked || !pthread_equal(lock->owner, self)) {
        return PR_FAILURE;
    }

    lock->locked = PR_FALSE;
    if (0 == lock->notified.length) /* shortcut */
    {
        rv = pthread_mutex_unlock(&lock->mutex);
        PR_ASSERT(0 == rv);
    } else {
        pt_PostNotifies(lock, PR_TRUE);
    }

#if defined(DEBUG)
    pt_debug.locks_released += 1;
#endif
    return PR_SUCCESS;
} /* PR_Unlock */

PR_IMPLEMENT(void)
PR_AssertCurrentThreadOwnsLock(PRLock* lock)
{
    /* Nb: the order of the |locked| and |owner==me| checks is not critical
     * to the correctness of PR_AssertCurrentThreadOwnsLock(), but
     * this particular order makes the assertion more likely to
     * catch errors. */
    PR_ASSERT(lock->locked && pthread_equal(lock->owner, pthread_self()));
}

/**************************************************************/
/**************************************************************/
/***************************CONDITIONS*************************/
/**************************************************************/
/**************************************************************/

/*
 * This code is used to compute the absolute time for the wakeup.
 * It's moderately ugly, so it's defined here and called in a
 * couple of places.
 */
#define PT_NANOPERMICRO 1000UL
#define PT_BILLION 1000000000UL

static PRIntn
pt_TimedWait(pthread_cond_t* cv, pthread_mutex_t* ml,
             PRIntervalTime timeout)
{
    int rv;
    struct timeval now;
    struct timespec tmo;
    PRUint32 ticks = PR_TicksPerSecond();

    tmo.tv_sec = (PRInt32)(timeout / ticks);
    tmo.tv_nsec = (PRInt32)(timeout - (tmo.tv_sec * ticks));
    tmo.tv_nsec =
        (PRInt32)PR_IntervalToMicroseconds(PT_NANOPERMICRO * tmo.tv_nsec);

    /* pthreads wants this in absolute time, off we go ... */
    (void)GETTIMEOFDAY(&now);
    /* that one's usecs, this one's nsecs - grrrr! */
    tmo.tv_sec += now.tv_sec;
    tmo.tv_nsec += (PT_NANOPERMICRO * now.tv_usec);
    tmo.tv_sec += tmo.tv_nsec / PT_BILLION;
    tmo.tv_nsec %= PT_BILLION;

    rv = pthread_cond_timedwait(cv, ml, &tmo);

    /* NSPR doesn't report timeouts */
    return (rv == ETIMEDOUT) ? 0 : rv;
} /* pt_TimedWait */

/*
 * Notifies just get posted to the protecting mutex. The
 * actual notification is done when the lock is released so that
 * MP systems don't contend for a lock that they can't have.
 */
static void
pt_PostNotifyToCvar(PRCondVar* cvar, PRBool broadcast)
{
    PRIntn index = 0;
    _PT_Notified* notified = &cvar->lock->notified;

    PR_ASSERT(PR_TRUE == cvar->lock->locked);
    PR_ASSERT(pthread_equal(cvar->lock->owner, pthread_self()));
    PR_ASSERT(_PT_PTHREAD_MUTEX_IS_LOCKED(cvar->lock->mutex));

    while (1) {
        for (index = 0; index < notified->length; ++index) {
            if (notified->cv[index].cv == cvar) {
                if (broadcast) {
                    notified->cv[index].times = -1;
                } else if (-1 != notified->cv[index].times) {
                    notified->cv[index].times += 1;
                }
                return; /* we're finished */
            }
        }
        /* if not full, enter new CV in this array */
        if (notified->length < PT_CV_NOTIFIED_LENGTH) {
            break;
        }

        /* if there's no link, create an empty array and link it */
        if (NULL == notified->link) {
            notified->link = PR_NEWZAP(_PT_Notified);
        }
        notified = notified->link;
    }

    /* A brand new entry in the array */
    (void)PR_ATOMIC_INCREMENT(&cvar->notify_pending);
    notified->cv[index].times = (broadcast) ? -1 : 1;
    notified->cv[index].cv = cvar;
    notified->length += 1;
} /* pt_PostNotifyToCvar */

PR_IMPLEMENT(PRCondVar*)
PR_NewCondVar(PRLock* lock)
{
    PRCondVar* cv = PR_NEW(PRCondVar);
    PR_ASSERT(lock != NULL);
    if (cv != NULL) {
        int rv = _PT_PTHREAD_COND_INIT(cv->cv, _pt_cvar_attr);
        PR_ASSERT(0 == rv);
        if (0 == rv) {
            cv->lock = lock;
            cv->notify_pending = 0;
#if defined(DEBUG)
            pt_debug.cvars_created += 1;
#endif
        } else {
            PR_DELETE(cv);
            cv = NULL;
        }
    }
    return cv;
} /* PR_NewCondVar */

PR_IMPLEMENT(void)
PR_DestroyCondVar(PRCondVar* cvar)
{
    if (0 > PR_ATOMIC_DECREMENT(&cvar->notify_pending)) {
        PRIntn rv = pthread_cond_destroy(&cvar->cv);
#if defined(DEBUG)
        PR_ASSERT(0 == rv);
        memset(cvar, 0xaf, sizeof(PRCondVar));
        pt_debug.cvars_destroyed += 1;
#else
        (void)rv;
#endif
        PR_Free(cvar);
    }
} /* PR_DestroyCondVar */

PR_IMPLEMENT(PRStatus)
PR_WaitCondVar(PRCondVar* cvar, PRIntervalTime timeout)
{
    PRIntn rv;
    PRThread* thred = PR_GetCurrentThread();

    PR_ASSERT(cvar != NULL);
    /* We'd better be locked */
    PR_ASSERT(_PT_PTHREAD_MUTEX_IS_LOCKED(cvar->lock->mutex));
    PR_ASSERT(PR_TRUE == cvar->lock->locked);
    /* and it better be by us */
    PR_ASSERT(pthread_equal(cvar->lock->owner, pthread_self()));

    if (_PT_THREAD_INTERRUPTED(thred)) {
        goto aborted;
    }

    /*
     * The thread waiting is used for PR_Interrupt
     */
    thred->waiting = cvar; /* this is where we're waiting */

    /*
     * If we have pending notifies, post them now.
     *
     * This is not optimal. We're going to post these notifies
     * while we're holding the lock. That means on MP systems
     * that they are going to collide for the lock that we will
     * hold until we actually wait.
     */
    if (0 != cvar->lock->notified.length) {
        pt_PostNotifies(cvar->lock, PR_FALSE);
    }

    /*
     * We're surrendering the lock, so clear out the locked field.
     */
    cvar->lock->locked = PR_FALSE;

    if (timeout == PR_INTERVAL_NO_TIMEOUT) {
        rv = pthread_cond_wait(&cvar->cv, &cvar->lock->mutex);
    } else {
        rv = pt_TimedWait(&cvar->cv, &cvar->lock->mutex, timeout);
    }

    /* We just got the lock back - this better be empty */
    PR_ASSERT(PR_FALSE == cvar->lock->locked);
    cvar->lock->locked = PR_TRUE;
    cvar->lock->owner = pthread_self();

    PR_ASSERT(0 == cvar->lock->notified.length);
    thred->waiting = NULL; /* and now we're not */
    if (_PT_THREAD_INTERRUPTED(thred)) {
        goto aborted;
    }
    if (rv != 0) {
        _PR_MD_MAP_DEFAULT_ERROR(rv);
        return PR_FAILURE;
    }
    return PR_SUCCESS;

aborted:
    PR_SetError(PR_PENDING_INTERRUPT_ERROR, 0);
    thred->state &= ~PT_THREAD_ABORTED;
    return PR_FAILURE;
} /* PR_WaitCondVar */

PR_IMPLEMENT(PRStatus)
PR_NotifyCondVar(PRCondVar* cvar)
{
    PR_ASSERT(cvar != NULL);
    pt_PostNotifyToCvar(cvar, PR_FALSE);
    return PR_SUCCESS;
} /* PR_NotifyCondVar */

PR_IMPLEMENT(PRStatus)
PR_NotifyAllCondVar(PRCondVar* cvar)
{
    PR_ASSERT(cvar != NULL);
    pt_PostNotifyToCvar(cvar, PR_TRUE);
    return PR_SUCCESS;
} /* PR_NotifyAllCondVar */

/**************************************************************/
/**************************************************************/
/***************************MONITORS***************************/
/**************************************************************/
/**************************************************************/

/*
 * Notifies just get posted to the monitor. The actual notification is done
 * when the monitor is fully exited so that MP systems don't contend for a
 * monitor that they can't enter.
 */
static void
pt_PostNotifyToMonitor(PRMonitor* mon, PRBool broadcast)
{
    PR_ASSERT(NULL != mon);
    PR_ASSERT_CURRENT_THREAD_IN_MONITOR(mon);

    /* mon->notifyTimes is protected by the monitor, so we don't need to
     * acquire mon->lock.
     */
    if (broadcast) {
        mon->notifyTimes = -1;
    } else if (-1 != mon->notifyTimes) {
        mon->notifyTimes += 1;
    }
} /* pt_PostNotifyToMonitor */

static void
pt_PostNotifiesFromMonitor(pthread_cond_t* cv, PRIntn times)
{
    PRIntn rv;

    /*
     * Time to actually notify any waits that were affected while the monitor
     * was entered.
     */
    PR_ASSERT(NULL != cv);
    PR_ASSERT(0 != times);
    if (-1 == times) {
        rv = pthread_cond_broadcast(cv);
        PR_ASSERT(0 == rv);
    } else {
        while (times-- > 0) {
            rv = pthread_cond_signal(cv);
            PR_ASSERT(0 == rv);
        }
    }
} /* pt_PostNotifiesFromMonitor */

PR_IMPLEMENT(PRMonitor*)
PR_NewMonitor(void)
{
    PRMonitor* mon;
    int rv;

    if (!_pr_initialized) {
        _PR_ImplicitInitialization();
    }

    mon = PR_NEWZAP(PRMonitor);
    if (mon == NULL) {
        PR_SetError(PR_OUT_OF_MEMORY_ERROR, 0);
        return NULL;
    }

    rv = _PT_PTHREAD_MUTEX_INIT(mon->lock, _pt_mattr);
    PR_ASSERT(0 == rv);
    if (0 != rv) {
        goto error1;
    }

    _PT_PTHREAD_INVALIDATE_THR_HANDLE(mon->owner);

    rv = _PT_PTHREAD_COND_INIT(mon->entryCV, _pt_cvar_attr);
    PR_ASSERT(0 == rv);
    if (0 != rv) {
        goto error2;
    }

    rv = _PT_PTHREAD_COND_INIT(mon->waitCV, _pt_cvar_attr);
    PR_ASSERT(0 == rv);
    if (0 != rv) {
        goto error3;
    }

    mon->notifyTimes = 0;
    mon->entryCount = 0;
    mon->refCount = 1;
    mon->name = NULL;
    return mon;

error3:
    pthread_cond_destroy(&mon->entryCV);
error2:
    pthread_mutex_destroy(&mon->lock);
error1:
    PR_Free(mon);
    _PR_MD_MAP_DEFAULT_ERROR(rv);
    return NULL;
} /* PR_NewMonitor */

PR_IMPLEMENT(PRMonitor*)
PR_NewNamedMonitor(const char* name)
{
    PRMonitor* mon = PR_NewMonitor();
    if (mon) {
        mon->name = name;
    }
    return mon;
}

PR_IMPLEMENT(void)
PR_DestroyMonitor(PRMonitor* mon)
{
    int rv;

    PR_ASSERT(mon != NULL);
    if (PR_ATOMIC_DECREMENT(&mon->refCount) == 0) {
        rv = pthread_cond_destroy(&mon->waitCV);
        PR_ASSERT(0 == rv);
        rv = pthread_cond_destroy(&mon->entryCV);
        PR_ASSERT(0 == rv);
        rv = pthread_mutex_destroy(&mon->lock);
        PR_ASSERT(0 == rv);
#if defined(DEBUG)
        memset(mon, 0xaf, sizeof(PRMonitor));
#endif
        PR_Free(mon);
    }
} /* PR_DestroyMonitor */

/* The GC uses this; it is quite arguably a bad interface.  I'm just
 * duplicating it for now - XXXMB
 */
PR_IMPLEMENT(PRIntn)
PR_GetMonitorEntryCount(PRMonitor* mon)
{
    pthread_t self = pthread_self();
    PRIntn rv;
    PRIntn count = 0;

    rv = pthread_mutex_lock(&mon->lock);
    PR_ASSERT(0 == rv);
    if (pthread_equal(mon->owner, self)) {
        count = mon->entryCount;
    }
    rv = pthread_mutex_unlock(&mon->lock);
    PR_ASSERT(0 == rv);
    return count;
}

PR_IMPLEMENT(void)
PR_AssertCurrentThreadInMonitor(PRMonitor* mon)
{
#if defined(DEBUG) || defined(FORCE_PR_ASSERT)
    PRIntn rv;

    rv = pthread_mutex_lock(&mon->lock);
    PR_ASSERT(0 == rv);
    PR_ASSERT(mon->entryCount != 0 && pthread_equal(mon->owner, pthread_self()));
    rv = pthread_mutex_unlock(&mon->lock);
    PR_ASSERT(0 == rv);
#endif
}

PR_IMPLEMENT(void)
PR_EnterMonitor(PRMonitor* mon)
{
    pthread_t self = pthread_self();
    PRIntn rv;

    PR_ASSERT(mon != NULL);
    rv = pthread_mutex_lock(&mon->lock);
    PR_ASSERT(0 == rv);
    if (mon->entryCount != 0) {
        if (pthread_equal(mon->owner, self)) {
            goto done;
        }
        while (mon->entryCount != 0) {
            rv = pthread_cond_wait(&mon->entryCV, &mon->lock);
            PR_ASSERT(0 == rv);
        }
    }
    /* and now I have the monitor */
    PR_ASSERT(0 == mon->notifyTimes);
    PR_ASSERT(_PT_PTHREAD_THR_HANDLE_IS_INVALID(mon->owner));
    _PT_PTHREAD_COPY_THR_HANDLE(self, mon->owner);

done:
    mon->entryCount += 1;
    rv = pthread_mutex_unlock(&mon->lock);
    PR_ASSERT(0 == rv);
} /* PR_EnterMonitor */

PR_IMPLEMENT(PRStatus)
PR_ExitMonitor(PRMonitor* mon)
{
    pthread_t self = pthread_self();
    PRIntn rv;
    PRBool notifyEntryWaiter = PR_FALSE;
    PRIntn notifyTimes = 0;

    PR_ASSERT(mon != NULL);
    rv = pthread_mutex_lock(&mon->lock);
    PR_ASSERT(0 == rv);
    /* the entries should be > 0 and we'd better be the owner */
    PR_ASSERT(mon->entryCount > 0);
    PR_ASSERT(pthread_equal(mon->owner, self));
    if (mon->entryCount == 0 || !pthread_equal(mon->owner, self)) {
        rv = pthread_mutex_unlock(&mon->lock);
        PR_ASSERT(0 == rv);
        return PR_FAILURE;
    }

    mon->entryCount -= 1; /* reduce by one */
    if (mon->entryCount == 0) {
        /* and if it transitioned to zero - notify an entry waiter */
        /* make the owner unknown */
        _PT_PTHREAD_INVALIDATE_THR_HANDLE(mon->owner);
        notifyEntryWaiter = PR_TRUE;
        notifyTimes = mon->notifyTimes;
        mon->notifyTimes = 0;
        /* We will access the members of 'mon' after unlocking mon->lock.
         * Add a reference. */
        PR_ATOMIC_INCREMENT(&mon->refCount);
    }
    rv = pthread_mutex_unlock(&mon->lock);
    PR_ASSERT(0 == rv);
    if (notifyEntryWaiter) {
        if (notifyTimes) {
            pt_PostNotifiesFromMonitor(&mon->waitCV, notifyTimes);
        }
        rv = pthread_cond_signal(&mon->entryCV);
        PR_ASSERT(0 == rv);
        /* We are done accessing the members of 'mon'. Release the
         * reference. */
        PR_DestroyMonitor(mon);
    }
    return PR_SUCCESS;
} /* PR_ExitMonitor */

PR_IMPLEMENT(PRStatus)
PR_Wait(PRMonitor* mon, PRIntervalTime timeout)
{
    PRStatus rv;
    PRUint32 saved_entries;
    pthread_t saved_owner;

    PR_ASSERT(mon != NULL);
    rv = pthread_mutex_lock(&mon->lock);
    PR_ASSERT(0 == rv);
    /* the entries better be positive */
    PR_ASSERT(mon->entryCount > 0);
    /* and it better be owned by us */
    PR_ASSERT(pthread_equal(mon->owner, pthread_self()));

    /* tuck these away 'till later */
    saved_entries = mon->entryCount;
    mon->entryCount = 0;
    _PT_PTHREAD_COPY_THR_HANDLE(mon->owner, saved_owner);
    _PT_PTHREAD_INVALIDATE_THR_HANDLE(mon->owner);
    /*
     * If we have pending notifies, post them now.
     *
     * This is not optimal. We're going to post these notifies
     * while we're holding the lock. That means on MP systems
     * that they are going to collide for the lock that we will
     * hold until we actually wait.
     */
    if (0 != mon->notifyTimes) {
        pt_PostNotifiesFromMonitor(&mon->waitCV, mon->notifyTimes);
        mon->notifyTimes = 0;
    }
    rv = pthread_cond_signal(&mon->entryCV);
    PR_ASSERT(0 == rv);

    if (timeout == PR_INTERVAL_NO_TIMEOUT) {
        rv = pthread_cond_wait(&mon->waitCV, &mon->lock);
    } else {
        rv = pt_TimedWait(&mon->waitCV, &mon->lock, timeout);
    }
    PR_ASSERT(0 == rv);

    while (mon->entryCount != 0) {
        rv = pthread_cond_wait(&mon->entryCV, &mon->lock);
        PR_ASSERT(0 == rv);
    }
    PR_ASSERT(0 == mon->notifyTimes);
    /* reinstate the interesting information */
    mon->entryCount = saved_entries;
    _PT_PTHREAD_COPY_THR_HANDLE(saved_owner, mon->owner);

    rv = pthread_mutex_unlock(&mon->lock);
    PR_ASSERT(0 == rv);
    return rv;
} /* PR_Wait */

PR_IMPLEMENT(PRStatus)
PR_Notify(PRMonitor* mon)
{
    pt_PostNotifyToMonitor(mon, PR_FALSE);
    return PR_SUCCESS;
} /* PR_Notify */

PR_IMPLEMENT(PRStatus)
PR_NotifyAll(PRMonitor* mon)
{
    pt_PostNotifyToMonitor(mon, PR_TRUE);
    return PR_SUCCESS;
} /* PR_NotifyAll */

#endif /* defined(_PR_PTHREADS) */

/* ptsynch.c */
