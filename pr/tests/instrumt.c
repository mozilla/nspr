/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
** File:    instrumt.c
** Description: This test is for the NSPR debug aids defined in
** prcountr.h
**
** The test case tests the counter facility in NSPR:
**
** Diagnostic messages can be enabled using "instrumt -v 6"
** This sets the msgLevel to something that PR_LOG() likes.
** Also define in the environment "NSPR_LOG_MODULES=Test:6"
**
** CounterTest() tests the counter facility. This test
** creates 4 threads. Each thread either increments, decrements,
** adds to or subtracts from a counter, depending on an argument
** passed to the thread at thread-create time. Each of these threads
** does COUNT_LIMIT iterations doing its thing. When all 4 threads
** are done, the result of the counter is evaluated. If all was atomic,
** the the value of the counter should be zero.
*/

#include <stdio.h>
#include <stdlib.h>
#include <plstr.h>
#include <prclist.h>
#include <plgetopt.h>
#include <prlog.h>
#include <prmon.h>
#include <prthread.h>
#include <pratom.h>
#include <prcountr.h>

#define COUNT_LIMIT (10 * (1024))

PRLogModuleLevel msgLevel = PR_LOG_ALWAYS;

PRBool help = PR_FALSE;
PRBool failed = PR_FALSE;

PRLogModuleInfo* lm;
PRMonitor* mon;
PRInt32 activeThreads = 0;
PR_DEFINE_COUNTER(hCounter);

static void
Help(void)
{
    printf("Help? ... Ha!\n");
}

static void
ListCounters(void)
{
    PR_DEFINE_COUNTER(qh);
    PR_DEFINE_COUNTER(rh);
    const char *qn, *rn, *dn;
    const char **qname = &qn, **rname = &rn, **desc = &dn;
    PRUint32 tCtr;

    PR_INIT_COUNTER_HANDLE(qh, NULL);
    PR_FIND_NEXT_COUNTER_QNAME(qh, qh);
    while (qh != NULL) {
        PR_INIT_COUNTER_HANDLE(rh, NULL);
        PR_FIND_NEXT_COUNTER_RNAME(rh, rh, qh);
        while (rh != NULL) {
            PR_GET_COUNTER_NAME_FROM_HANDLE(rh, qname, rname, desc);
            PR_GET_COUNTER(tCtr, rh);
            PR_LOG(
                lm, msgLevel,
                ("QName: %s  RName: %s  Desc: %s  Value: %ld\n", qn, rn, dn, tCtr));
            PR_FIND_NEXT_COUNTER_RNAME(rh, rh, qh);
        }
        PR_FIND_NEXT_COUNTER_QNAME(qh, qh);
    }
    return;
} /* end ListCounters() */

static PRInt32 one = 1;
static PRInt32 two = 2;
static PRInt32 three = 3;
static PRInt32 four = 4;

/*
** Thread to iteratively count something.
*/
static void PR_CALLBACK
CountSomething(void* arg)
{
    PRInt32 switchVar = *((PRInt32*)arg);
    PRInt32 i;

    PR_LOG(lm, msgLevel, ("CountSomething: begin thread %ld", switchVar));

    for (i = 0; i < COUNT_LIMIT; i++) {
        switch (switchVar) {
            case 1:
                PR_INCREMENT_COUNTER(hCounter);
                break;
            case 2:
                PR_DECREMENT_COUNTER(hCounter);
                break;
            case 3:
                PR_ADD_TO_COUNTER(hCounter, 1);
                break;
            case 4:
                PR_SUBTRACT_FROM_COUNTER(hCounter, 1);
                break;
            default:
                PR_ASSERT(0);
                break;
        }
    } /* end for() */

    PR_LOG(lm, msgLevel, ("CounterSomething: end thread %ld", switchVar));

    PR_EnterMonitor(mon);
    --activeThreads;
    PR_Notify(mon);
    PR_ExitMonitor(mon);

    return;
} /* end CountSomething() */

/*
** Create the counter threads.
*/
static void
CounterTest(void)
{
    PRThread *t1, *t2, *t3, *t4;
    PRIntn i = 0;
    PR_DEFINE_COUNTER(tc);
    PR_DEFINE_COUNTER(zCounter);

    PR_LOG(lm, msgLevel, ("Begin CounterTest"));

    /*
    ** Test Get and Set of a counter.
    **
    */
    PR_CREATE_COUNTER(zCounter, "Atomic", "get/set test",
                      "test get and set of counter");
    PR_SET_COUNTER(zCounter, 9);
    PR_GET_COUNTER(i, zCounter);
    if (i != 9) {
        failed = PR_TRUE;
        PR_LOG(lm, msgLevel, ("Counter set/get failed"));
    }

    activeThreads += 4;
    PR_CREATE_COUNTER(hCounter, "Atomic", "SMP Tests",
                      "test atomic nature of counter");

    PR_GET_COUNTER_HANDLE_FROM_NAME(tc, "Atomic", "SMP Tests");
    PR_ASSERT(tc == hCounter);

    t1 = PR_CreateThread(PR_USER_THREAD, CountSomething, &one, PR_PRIORITY_NORMAL,
                         PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 0);
    PR_ASSERT(t1);

    t2 = PR_CreateThread(PR_USER_THREAD, CountSomething, &two, PR_PRIORITY_NORMAL,
                         PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 0);
    PR_ASSERT(t2);

    t3 = PR_CreateThread(PR_USER_THREAD, CountSomething, &three,
                         PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                         PR_UNJOINABLE_THREAD, 0);
    PR_ASSERT(t3);

    t4 =
        PR_CreateThread(PR_USER_THREAD, CountSomething, &four, PR_PRIORITY_NORMAL,
                        PR_GLOBAL_THREAD, PR_UNJOINABLE_THREAD, 0);
    PR_ASSERT(t4);

    PR_LOG(lm, msgLevel, ("Counter Threads started"));

    ListCounters();
    return;
} /* end CounterTest() */

int
main(int argc, char** argv)
{
#if defined(DEBUG) || defined(FORCE_NSPR_COUNTERS)
    PRUint32 counter;
    PLOptStatus os;
    PLOptState* opt = PL_CreateOptState(argc, argv, "hdv:");
    lm = PR_NewLogModule("Test");

    while (PL_OPT_EOL != (os = PL_GetNextOpt(opt))) {
        if (PL_OPT_BAD == os) {
            continue;
        }
        switch (opt->option) {
            case 'v': /* verbose mode */
                msgLevel = (PRLogModuleLevel)atol(opt->value);
                break;
            case 'h': /* help message */
                Help();
                help = PR_TRUE;
                break;
            default:
                break;
        }
    }
    PL_DestroyOptState(opt);

    mon = PR_NewMonitor();
    PR_EnterMonitor(mon);

    CounterTest();

    /* Wait for all threads to exit */
    while (activeThreads > 0) {
        PR_Wait(mon, PR_INTERVAL_NO_TIMEOUT);
        PR_GET_COUNTER(counter, hCounter);
    }
    PR_ExitMonitor(mon);

    /*
    ** Evaluate results
    */
    PR_GET_COUNTER(counter, hCounter);
    if (counter != 0) {
        failed = PR_TRUE;
        PR_LOG(lm, msgLevel, ("Expected counter == 0, found: %ld", counter));
        printf("FAIL\n");
    } else {
        printf("PASS\n");
    }

    PR_DESTROY_COUNTER(hCounter);

    PR_DestroyMonitor(mon);
#else
    printf("Test not defined\n");
#endif
    return 0;
} /* main() */
/* end instrumt.c */
