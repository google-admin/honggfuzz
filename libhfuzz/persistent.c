#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../common.h"

int LLVMFuzzerTestOneInput(uint8_t * buf, size_t len) __attribute__ ((weak));
int LLVMFuzzerInitialize(int *argc, char ***argv) __attribute__ ((weak));

static inline ssize_t readFromFd(int fd, uint8_t * buf, size_t len)
{
    size_t readSz = 0;
    while (readSz < len) {
        ssize_t sz = read(fd, &buf[readSz], len - readSz);
        if (sz < 0 && errno == EINTR)
            continue;

        if (sz == 0)
            break;

        if (sz < 0)
            return -1;

        readSz += sz;
    }
    return (ssize_t) readSz;
}

static inline bool readFromFdAll(int fd, uint8_t * buf, size_t len)
{
    return (readFromFd(fd, buf, len) == (ssize_t) len);
}

static bool writeToFd(int fd, uint8_t * buf, size_t len)
{
    size_t writtenSz = 0;
    while (writtenSz < len) {
        ssize_t sz = write(fd, &buf[writtenSz], len - writtenSz);
        if (sz < 0 && errno == EINTR)
            continue;

        if (sz < 0)
            return false;

        writtenSz += sz;
    }
    return (writtenSz == len);
}

static uint8_t buf[_HF_PERF_BITMAP_SIZE_16M] = { 0 };

void HF_ITER(uint8_t ** buf_ptr, size_t * len_ptr)
{
    /*
     * Send the 'done' marker to the parent
     */
    static bool initialized = false;

    if (initialized == true) {
        uint8_t z = 'A';
        if (writeToFd(_HF_PERSISTENT_FD, &z, sizeof(z)) == false) {
            fprintf(stderr, "readFromFdAll() failed\n");
            _exit(1);
        }
    }
    initialized = true;

    uint32_t rlen;
    if (readFromFdAll(_HF_PERSISTENT_FD, (uint8_t *) & rlen, sizeof(rlen)) == false) {
        fprintf(stderr, "readFromFdAll(size) failed\n");
        _exit(1);
    }
    size_t len = (size_t) rlen;
    if (len > _HF_PERF_BITMAP_SIZE_16M) {
        fprintf(stderr, "len (%zu) > buf_size (%zu)\n", len, (size_t) _HF_PERF_BITMAP_SIZE_16M);
        _exit(1);
    }

    if (readFromFdAll(_HF_PERSISTENT_FD, buf, len) == false) {
        fprintf(stderr, "readFromFdAll(buf) failed\n");
        _exit(1);
    }

    *buf_ptr = buf;
    *len_ptr = len;
}

/*
 * Declare it 'weak', so it can be safely linked with regular binaries which
 * implement their own main()
 */
__attribute__ ((weak))
int main(int argc, char **argv)
{
    if (LLVMFuzzerInitialize) {
        LLVMFuzzerInitialize(&argc, &argv);
    }
    if (LLVMFuzzerTestOneInput == NULL) {
        fprintf(stderr, "Define 'int LLVMFuzzerTestOneInput(uint8_t * buf, size_t len)' in your "
                "code to make it work\n");
        exit(1);
    }

    for (;;) {
        size_t len;
        uint8_t *buf;

        HF_ITER(&buf, &len);

        int ret = LLVMFuzzerTestOneInput(buf, len);
        if (ret != 0) {
            fprintf(stderr, "LLVMFuzzerTestOneInput() returned '%d' instead of '0'\n", ret);
            exit(1);
        }
    }
}
