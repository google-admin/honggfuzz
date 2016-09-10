/*
 *
 * honggfuzz - architecture dependent code (POSIX / SIGNAL)
 * -----------------------------------------
 *
 * Author: Robert Swiecki <swiecki@google.com>
 *
 * Copyright 2010-2015 by Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 */

#include "../common.h"
#include "../arch.h"

#include <poll.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/cdefs.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../files.h"
#include "../log.h"
#include "../sancov.h"
#include "../subproc.h"
#include "../util.h"

/*  *INDENT-OFF* */
struct {
    bool important;
    const char *descr;
} arch_sigs[NSIG] = {
    [0 ... (NSIG - 1)].important = false,
    [0 ... (NSIG - 1)].descr = "UNKNOWN",

    [SIGILL].important = true,
    [SIGILL].descr = "SIGILL",
    [SIGFPE].important = true,
    [SIGFPE].descr = "SIGFPE",
    [SIGSEGV].important = true,
    [SIGSEGV].descr = "SIGSEGV",
    [SIGBUS].important = true,
    [SIGBUS].descr = "SIGBUS",
    [SIGABRT].important = true,
    [SIGABRT].descr = "SIGABRT"
};
/*  *INDENT-ON* */

/*
 * Returns true if a process exited (so, presumably, we can delete an input
 * file)
 */
static bool arch_analyzeSignal(honggfuzz_t * hfuzz, int status, fuzzer_t * fuzzer)
{
    /*
     * Resumed by delivery of SIGCONT
     */
    if (WIFCONTINUED(status)) {
        return false;
    }

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        sancov_Analyze(hfuzz, fuzzer);
    }

    /*
     * Boring, the process just exited
     */
    if (WIFEXITED(status)) {
        LOG_D("Process (pid %d) exited normally with status %d", fuzzer->pid, WEXITSTATUS(status));
        return true;
    }

    /*
     * Shouldn't really happen, but, well..
     */
    if (!WIFSIGNALED(status)) {
        LOG_E("Process (pid %d) exited with the following status %d, please report that as a bug",
              fuzzer->pid, status);
        return true;
    }

    int termsig = WTERMSIG(status);
    LOG_D("Process (pid %d) killed by signal %d '%s'", fuzzer->pid, termsig, strsignal(termsig));
    if (!arch_sigs[termsig].important) {
        LOG_D("It's not that important signal, skipping");
        return true;
    }

    char localtmstr[PATH_MAX];
    util_getLocalTime("%F.%H:%M:%S", localtmstr, sizeof(localtmstr), time(NULL));

    char newname[PATH_MAX];

    /* If dry run mode, copy file with same name into workspace */
    if (hfuzz->origFlipRate == 0.0L && hfuzz->useVerifier) {
        snprintf(newname, sizeof(newname), "%s", fuzzer->origFileName);
    } else {
        snprintf(newname, sizeof(newname), "%s/%s.PID.%d.TIME.%s.%s",
                 hfuzz->workDir, arch_sigs[termsig].descr, fuzzer->pid, localtmstr,
                 hfuzz->fileExtn);
    }

    LOG_I("Ok, that's interesting, saving the '%s' as '%s'", fuzzer->fileName, newname);

    /*
     * All crashes are marked as unique due to lack of information in POSIX arch
     */
    ATOMIC_POST_INC(hfuzz->crashesCnt);
    ATOMIC_POST_INC(hfuzz->uniqueCrashesCnt);

    if (files_writeBufToFile
        (newname, fuzzer->dynamicFile, fuzzer->dynamicFileSz,
         O_CREAT | O_EXCL | O_WRONLY) == false) {
        LOG_E("Couldn't copy '%s' to '%s'", fuzzer->fileName, fuzzer->crashFileName);
    }

    return true;
}

pid_t arch_fork(honggfuzz_t * hfuzz UNUSED, fuzzer_t * fuzzer UNUSED)
{
    return fork();
}

bool arch_launchChild(honggfuzz_t * hfuzz, char *fileName)
{
#define ARGS_MAX 512
    char *args[ARGS_MAX + 2];
    char argData[PATH_MAX] = { 0 };
    int x;

    for (x = 0; x < ARGS_MAX && hfuzz->cmdline[x]; x++) {
        if (!hfuzz->fuzzStdin && strcmp(hfuzz->cmdline[x], _HF_FILE_PLACEHOLDER) == 0) {
            args[x] = fileName;
        } else if (!hfuzz->fuzzStdin && strstr(hfuzz->cmdline[x], _HF_FILE_PLACEHOLDER)) {
            const char *off = strstr(hfuzz->cmdline[x], _HF_FILE_PLACEHOLDER);
            snprintf(argData, PATH_MAX, "%.*s%s", (int)(off - hfuzz->cmdline[x]), hfuzz->cmdline[x],
                     fileName);
            args[x] = argData;
        } else {
            args[x] = hfuzz->cmdline[x];
        }
    }

    args[x++] = NULL;

    LOG_D("Launching '%s' on file '%s'", args[0], fileName);

    execvp(args[0], args);
    return false;
}

void arch_prepareChild(honggfuzz_t * hfuzz UNUSED, fuzzer_t * fuzzer UNUSED)
{
}

void arch_reapChild(honggfuzz_t * hfuzz, fuzzer_t * fuzzer)
{
    for (;;) {
        if (hfuzz->persistent) {
            struct pollfd pfd = {
                .fd = fuzzer->persistentSock,
                .events = POLLIN,
            };
            int r = poll(&pfd, 1, -1);
            if (r == -1 && errno != EINTR) {
                PLOG_F("poll(fd=%d)", fuzzer->persistentSock);
            }
        }
        if (subproc_persistentModeRoundDone(hfuzz, fuzzer) == true) {
            break;
        }

        int status;
        int flags = hfuzz->persistent ? WNOHANG : 0;
        int ret = waitpid(fuzzer->pid, &status, flags);
        if (ret == -1 && errno == EINTR) {
            continue;
        }
        if (ret == -1) {
            PLOG_W("wait4(pid=%d)", fuzzer->pid);
            continue;
        }
        if (ret != fuzzer->pid) {
            continue;
        }

        char strStatus[4096];
        if (hfuzz->persistent && ret == fuzzer->persistentPid
            && (WIFEXITED(status) || WIFSIGNALED(status))) {
            fuzzer->persistentPid = 0;
            LOG_W("Persistent mode: PID %d exited with status: %s", ret,
                  subproc_StatusToStr(status, strStatus, sizeof(strStatus)));
        }

        LOG_D("Process (pid %d) came back with status: %s", fuzzer->pid,
              subproc_StatusToStr(status, strStatus, sizeof(strStatus)));

        if (arch_analyzeSignal(hfuzz, status, fuzzer)) {
            break;
        }
    }
}

bool arch_archInit(honggfuzz_t * hfuzz UNUSED)
{
    return true;
}

void arch_sigFunc(int sig UNUSED)
{
    return;
}

static bool arch_setTimer(timer_t * timerid)
{
    /*
     * Kick in every 200ms, starting with the next second
     */
    const struct itimerspec ts = {
        .it_value = {.tv_sec = 0,.tv_nsec = 250000000,},
        .it_interval = {.tv_sec = 0,.tv_nsec = 250000000,},
    };
    if (timer_settime(*timerid, 0, &ts, NULL) == -1) {
        PLOG_E("timer_settime(arm) failed");
        timer_delete(*timerid);
        return false;
    }

    return true;
}

bool arch_setSig(int signo)
{
    sigset_t smask;
    sigemptyset(&smask);
    struct sigaction sa = {
        .sa_handler = arch_sigFunc,
        .sa_mask = smask,
        .sa_flags = 0,
    };

    if (sigaction(signo, &sa, NULL) == -1) {
        PLOG_W("sigaction(%d) failed", signo);
        return false;
    }

    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, signo);
    if (pthread_sigmask(SIG_UNBLOCK, &ss, NULL) != 0) {
        PLOG_W("pthread_sigmask(%d, SIG_UNBLOCK)", signo);
        return false;
    }

    return true;
}

bool arch_archThreadInit(honggfuzz_t * hfuzz UNUSED, fuzzer_t * fuzzer UNUSED)
{
    if (arch_setSig(SIGIO) == false) {
        LOG_E("arch_setSig(SIGIO)");
        return false;
    }
    if (arch_setSig(SIGCHLD) == false) {
        LOG_E("arch_setSig(SIGCHLD)");
        return false;
    }

    struct sigevent sevp = {
        .sigev_value.sival_ptr = &fuzzer->timerId,
        .sigev_signo = SIGIO,
        .sigev_notify = SIGEV_SIGNAL,
    };
    if (timer_create(CLOCK_REALTIME, &sevp, &fuzzer->timerId) == -1) {
        PLOG_E("timer_create(CLOCK_REALTIME) failed");
        return false;
    }
    if (arch_setTimer(&(fuzzer->timerId)) == false) {
        LOG_F("Couldn't set timer");
    }

    return true;
}
