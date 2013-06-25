/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>

#if __BIONIC__

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <sys/_system_properties.h>

extern void *__system_property_area__;

struct LocalPropertyTestState {
    LocalPropertyTestState() : valid(false) {
        char dir_template[] = "/data/nativetest/prop-XXXXXX";
        char *dirname = mkdtemp(dir_template);
        if (!dirname) {
            perror("making temp file for test state failed (is /data/nativetest writable?)");
            return;
        }

        old_pa = __system_property_area__;
        __system_property_area__ = NULL;

        pa_dirname = dirname;
        pa_filename = pa_dirname + "/__properties__";

        __system_property_set_filename(pa_filename.c_str());
        __system_property_area_init();
        valid = true;
    }

    ~LocalPropertyTestState() {
        if (!valid)
            return;

        __system_property_area__ = old_pa;

        __system_property_set_filename(PROP_FILENAME);
        unlink(pa_filename.c_str());
        rmdir(pa_dirname.c_str());
    }
public:
    bool valid;
private:
    std::string pa_dirname;
    std::string pa_filename;
    void *old_pa;
};

TEST(properties, add) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);

    char propvalue[PROP_VALUE_MAX];

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    ASSERT_EQ(0, __system_property_add("other_property", 14, "value2", 6));
    ASSERT_EQ(0, __system_property_add("property_other", 14, "value3", 6));

    ASSERT_EQ(6, __system_property_get("property", propvalue));
    ASSERT_STREQ(propvalue, "value1");

    ASSERT_EQ(6, __system_property_get("other_property", propvalue));
    ASSERT_STREQ(propvalue, "value2");

    ASSERT_EQ(6, __system_property_get("property_other", propvalue));
    ASSERT_STREQ(propvalue, "value3");
}

TEST(properties, update) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);

    char propvalue[PROP_VALUE_MAX];
    prop_info *pi;

    ASSERT_EQ(0, __system_property_add("property", 8, "oldvalue1", 9));
    ASSERT_EQ(0, __system_property_add("other_property", 14, "value2", 6));
    ASSERT_EQ(0, __system_property_add("property_other", 14, "value3", 6));

    pi = (prop_info *)__system_property_find("property");
    ASSERT_NE((prop_info *)NULL, pi);
    __system_property_update(pi, "value4", 6);

    pi = (prop_info *)__system_property_find("other_property");
    ASSERT_NE((prop_info *)NULL, pi);
    __system_property_update(pi, "newvalue5", 9);

    pi = (prop_info *)__system_property_find("property_other");
    ASSERT_NE((prop_info *)NULL, pi);
    __system_property_update(pi, "value6", 6);

    ASSERT_EQ(6, __system_property_get("property", propvalue));
    ASSERT_STREQ(propvalue, "value4");

    ASSERT_EQ(9, __system_property_get("other_property", propvalue));
    ASSERT_STREQ(propvalue, "newvalue5");

    ASSERT_EQ(6, __system_property_get("property_other", propvalue));
    ASSERT_STREQ(propvalue, "value6");
}

TEST(properties, fill) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);
    char prop_name[PROP_NAME_MAX];
    char prop_value[PROP_VALUE_MAX];
    char prop_value_ret[PROP_VALUE_MAX];
    int count = 0;
    int ret;

    while (true) {
        ret = snprintf(prop_name, PROP_NAME_MAX - 1, "property_%d", count);
        memset(prop_name + ret, 'a', PROP_NAME_MAX - 1 - ret);
        ret = snprintf(prop_value, PROP_VALUE_MAX - 1, "value_%d", count);
        memset(prop_value + ret, 'b', PROP_VALUE_MAX - 1 - ret);
        prop_name[PROP_NAME_MAX - 1] = 0;
        prop_value[PROP_VALUE_MAX - 1] = 0;

        ret = __system_property_add(prop_name, PROP_NAME_MAX - 1, prop_value, PROP_VALUE_MAX - 1);
        if (ret < 0)
            break;

        count++;
    }

    // For historical reasons at least 247 properties must be supported
    ASSERT_GE(count, 247);

    for (int i = 0; i < count; i++) {
        ret = snprintf(prop_name, PROP_NAME_MAX - 1, "property_%d", i);
        memset(prop_name + ret, 'a', PROP_NAME_MAX - 1 - ret);
        ret = snprintf(prop_value, PROP_VALUE_MAX - 1, "value_%d", i);
        memset(prop_value + ret, 'b', PROP_VALUE_MAX - 1 - ret);
        prop_name[PROP_NAME_MAX - 1] = 0;
        prop_value[PROP_VALUE_MAX - 1] = 0;
        memset(prop_value_ret, '\0', PROP_VALUE_MAX);

        ASSERT_EQ(PROP_VALUE_MAX - 1, __system_property_get(prop_name, prop_value_ret));
        ASSERT_EQ(0, memcmp(prop_value, prop_value_ret, PROP_VALUE_MAX));
    }
}

static void foreach_test_callback(const prop_info *pi, void* cookie) {
    size_t *count = static_cast<size_t *>(cookie);

    ASSERT_NE((prop_info *)NULL, pi);
    (*count)++;
}

TEST(properties, foreach) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);
    size_t count = 0;

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    ASSERT_EQ(0, __system_property_add("other_property", 14, "value2", 6));
    ASSERT_EQ(0, __system_property_add("property_other", 14, "value3", 6));

    ASSERT_EQ(0, __system_property_foreach(foreach_test_callback, &count));
    ASSERT_EQ(3U, count);
}

TEST(properties, find_nth) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    ASSERT_EQ(0, __system_property_add("other_property", 14, "value2", 6));
    ASSERT_EQ(0, __system_property_add("property_other", 14, "value3", 6));

    ASSERT_NE((const prop_info *)NULL, __system_property_find_nth(0));
    ASSERT_NE((const prop_info *)NULL, __system_property_find_nth(1));
    ASSERT_NE((const prop_info *)NULL, __system_property_find_nth(2));

    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(3));
    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(4));
    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(5));
    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(100));
    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(200));
    ASSERT_EQ((const prop_info *)NULL, __system_property_find_nth(247));
}

TEST(properties, errors) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);
    char prop_value[PROP_NAME_MAX];

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    ASSERT_EQ(0, __system_property_add("other_property", 14, "value2", 6));
    ASSERT_EQ(0, __system_property_add("property_other", 14, "value3", 6));

    ASSERT_EQ(0, __system_property_find("property1"));
    ASSERT_EQ(0, __system_property_get("property1", prop_value));

    ASSERT_EQ(-1, __system_property_add("name", PROP_NAME_MAX, "value", 5));
    ASSERT_EQ(-1, __system_property_add("name", 4, "value", PROP_VALUE_MAX));
    ASSERT_EQ(-1, __system_property_update(NULL, "value", PROP_VALUE_MAX));
}

TEST(properties, serial) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);
    const prop_info *pi;
    unsigned int serial;

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    ASSERT_NE((const prop_info *)NULL, pi = __system_property_find("property"));
    serial = __system_property_serial(pi);
    ASSERT_EQ(0, __system_property_update((prop_info *)pi, "value2", 6));
    ASSERT_NE(serial, __system_property_serial(pi));
}

static void *PropertyWaitHelperFn(void *arg)
{
    int *flag = (int *)arg;
    prop_info *pi;
    pi = (prop_info *)__system_property_find("property");
    usleep(100000);

    *flag = 1;
    __system_property_update(pi, "value3", 6);

    return NULL;
}

TEST(properties, wait) {
    LocalPropertyTestState pa;
    ASSERT_TRUE(pa.valid);
    unsigned int serial;
    prop_info *pi;
    pthread_t t;
    int flag = 0;

    ASSERT_EQ(0, __system_property_add("property", 8, "value1", 6));
    serial = __system_property_wait_any(0);
    pi = (prop_info *)__system_property_find("property");
    ASSERT_NE((prop_info *)NULL, pi);
    __system_property_update(pi, "value2", 6);
    serial = __system_property_wait_any(serial);

    ASSERT_EQ(0, pthread_create(&t, NULL, PropertyWaitHelperFn, &flag));
    ASSERT_EQ(flag, 0);
    serial = __system_property_wait_any(serial);
    ASSERT_EQ(flag, 1);

    void* result;
    ASSERT_EQ(0, pthread_join(t, &result));
}

class KilledByFault {
    public:
        explicit KilledByFault() {};
        bool operator()(int exit_status) const;
};

bool KilledByFault::operator()(int exit_status) const {
    return WIFSIGNALED(exit_status) &&
        (WTERMSIG(exit_status) == SIGSEGV ||
         WTERMSIG(exit_status) == SIGBUS ||
         WTERMSIG(exit_status) == SIGABRT);
}

TEST(properties_DeathTest, read_only) {
      ::testing::FLAGS_gtest_death_test_style = "threadsafe";
      ASSERT_EXIT(__system_property_add("property", 8, "value", 5),
                  KilledByFault(), "");
}
#endif
