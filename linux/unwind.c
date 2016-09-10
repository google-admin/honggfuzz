/*
 *
 * honggfuzz - architecture dependent code (LINUX/UNWIND)
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
#include "unwind.h"

#include <endian.h>
#include <libunwind-ptrace.h>

#include "../log.h"

/*
 * WARNING: Ensure that _UPT-info structs are not shared between threads
 * http://www.nongnu.org/libunwind/man/libunwind-ptrace(3).html
 */

// libunwind error codes used for debugging
static const char *UNW_ER[] = {
    "UNW_ESUCCESS",             /* no error */
    "UNW_EUNSPEC",              /* unspecified (general) error */
    "UNW_ENOMEM",               /* out of memory */
    "UNW_EBADREG",              /* bad register number */
    "UNW_EREADONLYREG",         /* attempt to write read-only register */
    "UNW_ESTOPUNWIND",          /* stop unwinding */
    "UNW_EINVALIDIP",           /* invalid IP */
    "UNW_EBADFRAME",            /* bad frame */
    "UNW_EINVAL",               /* unsupported operation or bad value */
    "UNW_EBADVERSION",          /* unwind info has unsupported version */
    "UNW_ENOINFO"               /* no unwind info found */
};

#ifndef __ANDROID__
size_t arch_unwindStack(pid_t pid, funcs_t * funcs)
{
    size_t num_frames = 0;

    unw_addr_space_t as = unw_create_addr_space(&_UPT_accessors, __BYTE_ORDER);
    if (!as) {
        LOG_E("[pid='%d'] unw_create_addr_space failed", pid);
        return num_frames;
    }
    defer {
        unw_destroy_addr_space(as);
    };

    void *ui = _UPT_create(pid);
    if (ui == NULL) {
        LOG_E("[pid='%d'] _UPT_create failed", pid);
        return num_frames;
    }
    defer {
        _UPT_destroy(ui);
    };

    unw_cursor_t c;
    int ret = unw_init_remote(&c, as, ui);
    if (ret < 0) {
        LOG_E("[pid='%d'] unw_init_remote failed (%s)", pid, UNW_ER[-ret]);
        return num_frames;
    }

    for (num_frames = 0; unw_step(&c) > 0 && num_frames < _HF_MAX_FUNCS; num_frames++) {
        unw_word_t ip;
        ret = unw_get_reg(&c, UNW_REG_IP, &ip);
        if (ret < 0) {
            LOG_E("[pid='%d'] [%zd] failed to read IP (%s)", pid, num_frames, UNW_ER[-ret]);
            funcs[num_frames].pc = 0;
        } else
            funcs[num_frames].pc = (void *)ip;
    }

    return num_frames;
}

#else                           /* !defined(__ANDROID__) */
size_t arch_unwindStack(pid_t pid, funcs_t * funcs)
{
    size_t num_frames = 0;
    unw_addr_space_t as = unw_create_addr_space(&_UPT_accessors, __BYTE_ORDER);
    if (!as) {
        LOG_E("[pid='%d'] unw_create_addr_space failed", pid);
        return num_frames;
    }
    defer {
        unw_destroy_addr_space(as);
    };

    struct UPT_info *ui = (struct UPT_info *)_UPT_create(pid);
    if (ui == NULL) {
        LOG_E("[pid='%d'] _UPT_create failed", pid);
        return num_frames;
    }
    defer {
        _UPT_destroy(ui);
    };

    unw_cursor_t cursor;
    int ret = unw_init_remote(&cursor, as, ui);
    if (ret < 0) {
        LOG_E("[pid='%d'] unw_init_remote failed (%s)", pid, UNW_ER[-ret]);
        return num_frames;
    }

    do {
        unw_word_t pc = 0, offset = 0;
        char buf[_HF_FUNC_NAME_SZ] = { 0 };

        ret = unw_get_reg(&cursor, UNW_REG_IP, &pc);
        if (ret < 0) {
            LOG_E("[pid='%d'] [%zd] failed to read IP (%s)", pid, num_frames, UNW_ER[-ret]);
            // We don't want to try to extract info from an arbitrary IP
            // TODO: Maybe abort completely (goto out))
            goto skip_frame_info;
        }

        unw_proc_info_t frameInfo;
        ret = unw_get_proc_info(&cursor, &frameInfo);
        if (ret < 0) {
            LOG_D("[pid='%d'] [%zd] unw_get_proc_info (%s)", pid, num_frames, UNW_ER[-ret]);
            // Not safe to keep parsing frameInfo
            goto skip_frame_info;
        }

        ret = unw_get_proc_name(&cursor, buf, sizeof(buf), &offset);
        if (ret < 0) {
            LOG_D("[pid='%d'] [%zd] unw_get_proc_name() failed (%s)", pid, num_frames,
                  UNW_ER[-ret]);
            buf[0] = '\0';
        }

 skip_frame_info:
        // Compared to bfd, line var plays the role of offset from func_name
        // Reports format is adjusted accordingly to reflect in saved file
        funcs[num_frames].line = offset;
        funcs[num_frames].pc = (void *)pc;
        memcpy(funcs[num_frames].func, buf, sizeof(funcs[num_frames].func));

        num_frames++;

        ret = unw_step(&cursor);
    } while (ret > 0 && num_frames < _HF_MAX_FUNCS);

    return num_frames;
}
#endif                          /* defined(__ANDROID__) */
