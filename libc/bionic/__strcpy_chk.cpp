/*
 * Copyright (C) 2012 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <string.h>
#include <stdlib.h>
#include "private/libc_logging.h"

/*
 * Runtime implementation of __builtin____strcpy_chk.
 *
 * See
 *   http://gcc.gnu.org/onlinedocs/gcc/Object-Size-Checking.html
 *   http://gcc.gnu.org/ml/gcc-patches/2004-09/msg02055.html
 * for details.
 *
 * This strcpy check is called if _FORTIFY_SOURCE is defined and
 * greater than 0.
 */
extern "C" char *__strcpy_chk (char *dest, const char *src, size_t dest_len) {
    // TODO: optimize so we don't scan src twice.
    size_t src_len = strlen(src) + 1;
    if (__predict_false(src_len > dest_len)) {
        __fortify_chk_fail("strcpy prevented write past end of buffer",
                             BIONIC_EVENT_STRCPY_BUFFER_OVERFLOW);
    }

    return strcpy(dest, src);
}
