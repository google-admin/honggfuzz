/*
 *
 * honggfuzz - core structures and macros
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

#ifndef _HF_COMMON_H_
#define _HF_COMMON_H_

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <time.h>

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif

#define PROG_NAME "honggfuzz"
#define PROG_VERSION "0.8rc"
#define PROG_AUTHORS "Robert Swiecki <swiecki@google.com> et al.,\nCopyright 2010-2015 by Google Inc. All Rights Reserved."

/* Go-style defer implementation */
#define __STRMERGE(a, b) a##b
#define _STRMERGE(a, b) __STRMERGE(a, b)

#ifdef __clang__
static void __attribute__ ((unused)) __clang_cleanup_func(void (^*dfunc) (void))
{
    (*dfunc) ();
}

#define defer void (^_STRMERGE(__defer_f_, __COUNTER__))(void) __attribute__((cleanup(__clang_cleanup_func))) __attribute__((unused)) = ^
#else
#define __block
#define _DEFER(a, count) \
    auto void _STRMERGE(__defer_f_, count)(void *_defer_arg __attribute__((unused))); \
    int _STRMERGE(__defer_var_, count) __attribute__((cleanup(_STRMERGE(__defer_f_, count)))) __attribute__((unused)); \
    void _STRMERGE(__defer_f_, count)(void *_defer_arg __attribute__((unused)))
#define defer _DEFER(a, __COUNTER__)
#endif

/* Name of the template which will be replaced with the proper name of the file */
#define _HF_FILE_PLACEHOLDER "___FILE___"

/* Default name of the report created with some architectures */
#define _HF_REPORT_FILE "HONGGFUZZ.REPORT.TXT"

/* Default stack-size of created threads. Must be bigger then _HF_DYNAMIC_FILE_MAX_SZ */
#define _HF_PTHREAD_STACKSIZE (1024 * 1024 * 8) /* 8MB */

/* Name of envvar which indicates sequential number of fuzzer */
#define _HF_THREAD_NO_ENV "HFUZZ_THREAD_NO"

/* Number of crash verifier iterations before tag crash as stable */
#define _HF_VERIFIER_ITER   5

/* Size (in bytes) for report data to be stored in stack before written to file */
#define _HF_REPORT_SIZE 8192

/* Perf bitmap size */
#define _HF_PERF_BITMAP_SIZE_16M (1024U * 1024U * 16U)
#define _HF_PERF_BITMAP_BITSZ_MASK 0x7ffffff

#if defined(__ANDROID__)
#define _HF_MONITOR_SIGABRT 0
#else
#define _HF_MONITOR_SIGABRT 1
#endif

#define ARRAYSIZE(x) (sizeof(x) / sizeof(*x))

/* Memory barriers */
#define rmb()	__asm__ __volatile__("":::"memory")
#define wmb()	__sync_synchronize()

/* FD used to pass feedback bitmap a process */
#define _HF_BITMAP_FD 1022
/* FD used to pass data to a persistent process */
#define _HF_PERSISTENT_FD 1023

typedef enum {
    _HF_DYNFILE_NONE = 0x0,
    _HF_DYNFILE_INSTR_COUNT = 0x1,
    _HF_DYNFILE_BRANCH_COUNT = 0x2,
    _HF_DYNFILE_BTS_BLOCK = 0x8,
    _HF_DYNFILE_BTS_EDGE = 0x10,
    _HF_DYNFILE_IPT_BLOCK = 0x20,
    _HF_DYNFILE_CUSTOM = 0x40,
    _HF_DYNFILE_SOFT = 0x80,
} dynFileMethod_t;

typedef struct {
    uint64_t cpuInstrCnt;
    uint64_t cpuBranchCnt;
    uint64_t customCnt;
    uint64_t bbCnt;
    uint64_t newBBCnt;
    uint64_t softCntPc;
    uint64_t softCntCmp;
} hwcnt_t;

/* Sanitizer coverage specific data structures */
typedef struct {
    uint64_t hitBBCnt;
    uint64_t totalBBCnt;
    uint64_t dsoCnt;
    uint64_t iDsoCnt;
    uint64_t newBBCnt;
    uint64_t crashesCnt;
} sancovcnt_t;

typedef struct {
    uint32_t capacity;
    uint32_t *pChunks;
    uint32_t nChunks;
} bitmap_t;

/* Memory map struct */
typedef struct __attribute__ ((packed)) {
    uint64_t start;             // region start addr
    uint64_t end;               // region end addr
    uint64_t base;              // region base addr
    char mapName[NAME_MAX];     // bin/DSO name
    uint64_t bbCnt;
    uint64_t newBBCnt;
} memMap_t;

/* Trie node data struct */
typedef struct __attribute__ ((packed)) {
    bitmap_t *pBM;
} trieData_t;

/* Trie node struct */
typedef struct node {
    char key;
    trieData_t data;
    struct node *next;
    struct node *prev;
    struct node *children;
    struct node *parent;
} node_t;

/* EOF Sanitizer coverage specific data structures */

typedef struct {
    char *asanOpts;
    char *msanOpts;
    char *ubsanOpts;
} sanOpts_t;

typedef enum {
    _HF_STATE_UNSET = 0,
    _HF_STATE_STATIC = 1,
    _HF_STATE_DYNAMIC_PRE = 2,
    _HF_STATE_DYNAMIC_MAIN = 3,
} fuzzState_t;

struct dynfile_t {
    uint8_t *data;
    size_t size;
     TAILQ_ENTRY(dynfile_t) pointers;
};

/* Maximum number of active fuzzing threads */
#define _HF_THREAD_MAX 1024U
typedef struct {
    uint8_t bbMapPc[_HF_PERF_BITMAP_SIZE_16M];
    uint8_t bbMapCmp[_HF_PERF_BITMAP_SIZE_16M];
    uint64_t pidFeedbackPc[_HF_THREAD_MAX];
    uint64_t pidFeedbackCmp[_HF_THREAD_MAX];
} feedback_t;

typedef struct {
    char **cmdline;
    char cmdline_txt[PATH_MAX];
    char *inputFile;
    bool nullifyStdio;
    bool fuzzStdin;
    bool saveUnique;
    bool useScreen;
    bool useVerifier;
    time_t timeStart;
    char *fileExtn;
    char *workDir;
    double origFlipRate;
    char *externalCommand;
    const char *dictionaryFile;
    char **dictionary;
    const char *blacklistFile;
    uint64_t *blacklist;
    size_t blacklistCnt;
    long tmOut;
    size_t dictionaryCnt;
    size_t mutationsMax;
    size_t threadsMax;
    size_t threadsFinished;
    size_t maxFileSz;
    char *reportFile;
    uint64_t asLimit;
    char **files;
    size_t fileCnt;
    size_t lastFileIndex;
    size_t doneFileIndex;
    bool clearEnv;
    char *envs[128];
    bool persistent;

    fuzzState_t state;
    feedback_t *feedback;
    int bbFd;
    size_t dynfileqCnt;
    pthread_mutex_t dynfileq_mutex;
     TAILQ_HEAD(dynfileq_t, dynfile_t) dynfileq;

    size_t mutationsCnt;
    size_t crashesCnt;
    size_t uniqueCrashesCnt;
    size_t verifiedCrashesCnt;
    size_t blCrashesCnt;
    size_t timeoutedCnt;

    dynFileMethod_t dynFileMethod;
    sancovcnt_t sanCovCnts;
    pthread_mutex_t sanCov_mutex;
    sanOpts_t sanOpts;
    size_t dynFileIterExpire;
    bool useSanCov;
    node_t *covMetadata;
    bool msanReportUMRS;

    pthread_mutex_t report_mutex;

    /* For the Linux code */
    struct {
        hwcnt_t hwCnts;
        uint64_t dynamicCutOffAddr;
        bool disableRandomization;
        void *ignoreAddr;
        size_t numMajorFrames;
        pid_t pid;
        const char *pidFile;
        char *pidCmd;
    } linux;
} honggfuzz_t;

typedef struct {
    pid_t pid;
    pid_t persistentPid;
    int64_t timeStartedMillis;
    const char *origFileName;
    char fileName[PATH_MAX];
    char crashFileName[PATH_MAX];
    uint64_t pc;
    uint64_t backtrace;
    uint64_t access;
    int exception;
    char report[_HF_REPORT_SIZE];
    bool mainWorker;
    float flipRate;
    uint8_t *dynamicFile;
    size_t dynamicFileSz;
    uint32_t fuzzNo;
    int persistentSock;
#if !defined(_HF_ARCH_DARWIN)
    timer_t timerId;
#endif                          // !defined(_HF_ARCH_DARWIN)

    sancovcnt_t sanCovCnts;

    struct {
        /* For Linux code */
        uint8_t *perfMmapBuf;
        uint8_t *perfMmapAux;
        hwcnt_t hwCnts;
        pid_t attachedPid;
        int cpuInstrFd;
        int cpuBranchFd;
        int cpuIptBtsFd;
    } linux;
} fuzzer_t;

#endif
