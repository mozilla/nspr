/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
** File: nameshm1.c -- Test Named Shared Memory
**
** Description:
** nameshm1 tests Named Shared Memory. nameshm1 performs two tests of
** named shared memory, both as a single process. The basic test
** exercises all the API elements of the facility and attempts to write
** to all locations in the shared memory. The read-only test maps the
** segment read-only.
**
** Synopsis: nameshm1 [options] [name]
**
** Options:
** -d       Enables debug trace via PR_LOG()
** -v       Enables verbose mode debug trace via PR_LOG()
** -w       Causes the basic test to attempt to write to the segment
**          mapped as read-only. When this option is specified, the
**          test should crash with a seg-fault; this is a destructive
**          test and is considered successful when it seg-faults.
**
** -s <n>   Size, in KBytes (1024), of the shared memory segment.
**          Default: (10 * 1024)
**
** name     specifies the name of the shared memory segment to be used.
**          Default: /tmp/xxxNSPRshm
**
**
** See also: prshm.h
**
** /lth. Aug-1999.
*/

#include <plgetopt.h>
#include <nspr.h>
#include <stdlib.h>
#include <string.h>
#include <private/primpl.h>

#ifdef DEBUG
#define SEM_D "D"
#else
#define SEM_D
#endif
#ifdef IS_64
#define SEM_64 "64"
#else
#define SEM_64
#endif

#define OPT_NAME "/tmp/xxxNSPRshm" SEM_D SEM_64
#define SHM_MODE 0666

#define NameSize (1024)

PRIntn debug = 0;
PRIntn failed_already = 0;
PRLogModuleLevel msgLevel = PR_LOG_NONE;
PRLogModuleInfo* lm;

/* command line options */
PRIntn optDebug = 0;
PRIntn optVerbose = 0;
PRUint32 optWriteRO = 0; /* test write to read-only memory. should crash  */
PRUint32 optCreate = 1;
PRUint32 optAttachRW = 1;
PRUint32 optAttachRO = 1;
PRUint32 optClose = 1;
PRUint32 optDelete = 1;
PRUint32 optSize = (10 * 1024);
char optName[NameSize] = OPT_NAME;

char buf[1024] = "";

static void
BasicTest(void)
{
    PRSharedMemory* shm;
    char* addr; /* address of shared memory segment */
    PRUint32 i;
    PRInt32 rc;

    PR_LOG(lm, msgLevel, ("nameshm1: Begin BasicTest"));

    if (PR_FAILURE == PR_DeleteSharedMemory(optName)) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: Initial PR_DeleteSharedMemory() failed. No problem"));
    } else
        PR_LOG(lm, msgLevel, ("nameshm1: Initial PR_DeleteSharedMemory() success"));

    shm = PR_OpenSharedMemory(optName, optSize, (PR_SHM_CREATE | PR_SHM_EXCL),
                              SHM_MODE);
    if (NULL == shm) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RW Create: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RW Create: success: %p", shm));

    addr = PR_AttachSharedMemory(shm, 0);
    if (NULL == addr) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RW Attach: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RW Attach: success: %p", addr));

    /* fill memory with i */
    for (i = 0; i < optSize; i++) {
        *(addr + i) = i;
    }

    rc = PR_DetachSharedMemory(shm, addr);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RW Detach: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RW Detach: success: "));

    rc = PR_CloseSharedMemory(shm);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RW Close: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RW Close: success: "));

    rc = PR_DeleteSharedMemory(optName);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RW Delete: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RW Delete: success: "));

    PR_LOG(lm, msgLevel, ("nameshm1: BasicTest(): Passed"));

    return;
} /* end BasicTest() */

static void
ReadOnlyTest(void)
{
    PRSharedMemory* shm;
    char* roAddr; /* read-only address of shared memory segment */
    PRInt32 rc;

    PR_LOG(lm, msgLevel, ("nameshm1: Begin ReadOnlyTest"));

    shm = PR_OpenSharedMemory(optName, optSize, (PR_SHM_CREATE | PR_SHM_EXCL),
                              SHM_MODE);
    if (NULL == shm) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RO Create: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RO Create: success: %p", shm));

    roAddr = PR_AttachSharedMemory(shm, PR_SHM_READONLY);
    if (NULL == roAddr) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RO Attach: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RO Attach: success: %p", roAddr));

    if (optWriteRO) {
        *roAddr = 0x00; /* write to read-only memory */
        failed_already = 1;
        PR_LOG(lm, msgLevel, ("nameshm1: Wrote to read-only memory segment!"));
        return;
    }

    rc = PR_DetachSharedMemory(shm, roAddr);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RO Detach: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RO Detach: success: "));

    rc = PR_CloseSharedMemory(shm);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RO Close: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RO Close: success: "));

    rc = PR_DeleteSharedMemory(optName);
    if (PR_FAILURE == rc) {
        PR_LOG(lm, msgLevel,
               ("nameshm1: RO Destroy: Error: %ld. OSError: %ld", PR_GetError(),
                PR_GetOSError()));
        failed_already = 1;
        return;
    }
    PR_LOG(lm, msgLevel, ("nameshm1: RO Destroy: success: "));

    PR_LOG(lm, msgLevel, ("nameshm1: ReadOnlyTest(): Passed"));

    return;
} /* end ReadOnlyTest() */

int
main(int argc, char** argv)
{
    {
        /*
        ** Get command line options
        */
        PLOptStatus os;
        PLOptState* opt = PL_CreateOptState(argc, argv, "dvw:s:");

        while (PL_OPT_EOL != (os = PL_GetNextOpt(opt))) {
            if (PL_OPT_BAD == os) {
                continue;
            }
            switch (opt->option) {
                case 'v': /* debug mode */
                    optVerbose = 1;
                /* no break! fall into debug option */
                case 'd': /* debug mode */
                    debug = 1;
                    msgLevel = PR_LOG_DEBUG;
                    break;
                case 'w': /* try writing to memory mapped read-only */
                    optWriteRO = 1;
                    break;
                case 's':
                    optSize = atol(opt->value) * 1024;
                    break;
                default:
                    strcpy(optName, opt->value);
                    break;
            }
        }
        PL_DestroyOptState(opt);
    }

    lm = PR_NewLogModule("Test"); /* Initialize logging */

    PR_LOG(lm, msgLevel, ("nameshm1: Starting"));

    BasicTest();
    if (failed_already == 0) {
        ReadOnlyTest();
    }

    if (debug) {
        printf("%s\n", (failed_already) ? "FAIL" : "PASS");
    }
    return ((failed_already) ? 1 : 0);
} /* main() */
/* end instrumt.c */
