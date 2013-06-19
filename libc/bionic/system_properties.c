/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <stdbool.h>

#include <sys/mman.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

#include <sys/atomics.h>

struct prop_area {
    unsigned volatile count;
    unsigned volatile serial;
    unsigned magic;
    unsigned version;
    unsigned reserved[4];
    unsigned toc[1];
};

typedef struct prop_area prop_area;

struct prop_info {
    char name[PROP_NAME_MAX];
    unsigned volatile serial;
    char value[PROP_VALUE_MAX];
};

typedef struct prop_info prop_info;

static const char property_service_socket[] = "/dev/socket/" PROP_SERVICE_NAME;

static unsigned dummy_props = 0;

prop_area *__system_property_area__ = (void*) &dummy_props;

static int get_fd_from_env(void)
{
    char *env = getenv("ANDROID_PROPERTY_WORKSPACE");

    if (!env) {
        return -1;
    }

    return atoi(env);
}

void __system_property_area_init(void *data)
{
    prop_area *pa = data;
    memset(pa, 0, PA_SIZE);
    pa->magic = PROP_AREA_MAGIC;
    pa->version = PROP_AREA_VERSION;

    /* plug into the lib property services */
    __system_property_area__ = pa;
}

int __system_properties_init(void)
{
    bool fromFile = true;
    int result = -1;

    if(__system_property_area__ != ((void*) &dummy_props)) {
        return 0;
    }

    int fd = open(PROP_FILENAME, O_RDONLY | O_NOFOLLOW);

    if ((fd < 0) && (errno == ENOENT)) {
        /*
         * For backwards compatibility, if the file doesn't
         * exist, we use the environment to get the file descriptor.
         * For security reasons, we only use this backup if the kernel
         * returns ENOENT. We don't want to use the backup if the kernel
         * returns other errors such as ENOMEM or ENFILE, since it
         * might be possible for an external program to trigger this
         * condition.
         */
        fd = get_fd_from_env();
        fromFile = false;
    }

    if (fd < 0) {
        return -1;
    }

    struct stat fd_stat;
    if (fstat(fd, &fd_stat) < 0) {
        goto cleanup;
    }

    if ((fd_stat.st_uid != 0)
            || (fd_stat.st_gid != 0)
            || ((fd_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0)) {
        goto cleanup;
    }

    prop_area *pa = mmap(NULL, fd_stat.st_size, PROT_READ, MAP_SHARED, fd, 0);

    if (pa == MAP_FAILED) {
        goto cleanup;
    }

    if((pa->magic != PROP_AREA_MAGIC) || (pa->version != PROP_AREA_VERSION)) {
        munmap(pa, fd_stat.st_size);
        goto cleanup;
    }

    __system_property_area__ = pa;
    result = 0;

cleanup:
    if (fromFile) {
        close(fd);
    }

    return result;
}

const prop_info *__system_property_find_nth(unsigned n)
{
    prop_area *pa = __system_property_area__;

    if(n >= pa->count) {
        return 0;
    } else {
        return TOC_TO_INFO(pa, pa->toc[n]);
    }
}

const prop_info *__system_property_find(const char *name)
{
    prop_area *pa = __system_property_area__;
    unsigned count = pa->count;
    unsigned *toc = pa->toc;
    unsigned len = strlen(name);
    prop_info *pi;

    if (len >= PROP_NAME_MAX)
        return 0;
    if (len < 1)
        return 0;

    while(count--) {
        unsigned entry = *toc++;
        if(TOC_NAME_LEN(entry) != len) continue;

        pi = TOC_TO_INFO(pa, entry);
        if(memcmp(name, pi->name, len)) continue;

        return pi;
    }

    return 0;
}

int __system_property_read(const prop_info *pi, char *name, char *value)
{
    unsigned serial, len;

    for(;;) {
        serial = pi->serial;
        while(SERIAL_DIRTY(serial)) {
            __futex_wait((volatile void *)&pi->serial, serial, 0);
            serial = pi->serial;
        }
        len = SERIAL_VALUE_LEN(serial);
        memcpy(value, pi->value, len + 1);
        if(serial == pi->serial) {
            if(name != 0) {
                strcpy(name, pi->name);
            }
            return len;
        }
    }
}

int __system_property_get(const char *name, char *value)
{
    const prop_info *pi = __system_property_find(name);

    if(pi != 0) {
        return __system_property_read(pi, 0, value);
    } else {
        value[0] = 0;
        return 0;
    }
}


static int send_prop_msg(prop_msg *msg)
{
    struct pollfd pollfds[1];
    union {
        struct sockaddr_un addr;
        struct sockaddr addr_g;
    } addr;
    socklen_t alen;
    size_t namelen;
    int s;
    int r;
    int result = -1;

    s = socket(AF_LOCAL, SOCK_STREAM, 0);
    if(s < 0) {
        return result;
    }

    memset(&addr, 0, sizeof(addr));
    namelen = strlen(property_service_socket);
    strlcpy(addr.addr.sun_path, property_service_socket, sizeof addr.addr.sun_path);
    addr.addr.sun_family = AF_LOCAL;
    alen = namelen + offsetof(struct sockaddr_un, sun_path) + 1;

    if(TEMP_FAILURE_RETRY(connect(s, &addr.addr_g, alen) < 0)) {
        close(s);
        return result;
    }

    r = TEMP_FAILURE_RETRY(send(s, msg, sizeof(prop_msg), 0));

    if(r == sizeof(prop_msg)) {
        // We successfully wrote to the property server but now we
        // wait for the property server to finish its work.  It
        // acknowledges its completion by closing the socket so we
        // poll here (on nothing), waiting for the socket to close.
        // If you 'adb shell setprop foo bar' you'll see the POLLHUP
        // once the socket closes.  Out of paranoia we cap our poll
        // at 250 ms.
        pollfds[0].fd = s;
        pollfds[0].events = 0;
        r = TEMP_FAILURE_RETRY(poll(pollfds, 1, 250 /* ms */));
        if (r == 1 && (pollfds[0].revents & POLLHUP) != 0) {
            result = 0;
        } else {
            // Ignore the timeout and treat it like a success anyway.
            // The init process is single-threaded and its property
            // service is sometimes slow to respond (perhaps it's off
            // starting a child process or something) and thus this
            // times out and the caller thinks it failed, even though
            // it's still getting around to it.  So we fake it here,
            // mostly for ctl.* properties, but we do try and wait 250
            // ms so callers who do read-after-write can reliably see
            // what they've written.  Most of the time.
            // TODO: fix the system properties design.
            result = 0;
        }
    }

    close(s);
    return result;
}

int __system_property_set(const char *key, const char *value)
{
    int err;
    prop_msg msg;

    if(key == 0) return -1;
    if(value == 0) value = "";
    if(strlen(key) >= PROP_NAME_MAX) return -1;
    if(strlen(value) >= PROP_VALUE_MAX) return -1;

    memset(&msg, 0, sizeof msg);
    msg.cmd = PROP_MSG_SETPROP;
    strlcpy(msg.name, key, sizeof msg.name);
    strlcpy(msg.value, value, sizeof msg.value);

    err = send_prop_msg(&msg);
    if(err < 0) {
        return err;
    }

    return 0;
}

int __system_property_wait(const prop_info *pi)
{
    unsigned n;
    if(pi == 0) {
        prop_area *pa = __system_property_area__;
        n = pa->serial;
        do {
            __futex_wait(&pa->serial, n, 0);
        } while(n == pa->serial);
    } else {
        n = pi->serial;
        do {
            __futex_wait((volatile void *)&pi->serial, n, 0);
        } while(n == pi->serial);
    }
    return 0;
}

int __system_property_update(prop_info *pi, const char *value, unsigned int len)
{
    prop_area *pa = __system_property_area__;

    if (len >= PROP_VALUE_MAX)
        return -1;

    pi->serial = pi->serial | 1;
    memcpy(pi->value, value, len + 1);
    pi->serial = (len << 24) | ((pi->serial + 1) & 0xffffff);
    __futex_wake(&pi->serial, INT32_MAX);

    pa->serial++;
    __futex_wake(&pa->serial, INT32_MAX);

    return 0;
}

int __system_property_add(const char *name, unsigned int namelen,
            const char *value, unsigned int valuelen)
{
    prop_area *pa = __system_property_area__;
    prop_info *pa_info_array = (void*) (((char*) pa) + PA_INFO_START);
    prop_info *pi;

    if (pa->count == PA_COUNT_MAX)
        return -1;
    if (namelen >= PROP_NAME_MAX)
        return -1;
    if (valuelen >= PROP_VALUE_MAX)
        return -1;
    if (namelen < 1)
        return -1;

    pi = pa_info_array + pa->count;
    pi->serial = (valuelen << 24);
    memcpy(pi->name, name, namelen + 1);
    memcpy(pi->value, value, valuelen + 1);

    pa->toc[pa->count] =
        (namelen << 24) | (((unsigned) pi) - ((unsigned) pa));

    pa->count++;
    pa->serial++;
    __futex_wake(&pa->serial, INT32_MAX);

    return 0;
}

unsigned int __system_property_serial(const prop_info *pi)
{
    return pi->serial;
}

unsigned int __system_property_wait_any(unsigned int serial)
{
    prop_area *pa = __system_property_area__;

    do {
        __futex_wait(&pa->serial, serial, 0);
    } while(pa->serial == serial);

    return pa->serial;
}
