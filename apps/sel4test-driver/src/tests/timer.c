/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <autoconf.h>
#include <sel4test-driver/gen_config.h>
#include <sel4/sel4.h>
#include <vka/object.h>
#include <sel4test/macros.h>

#include "../timer.h"

#include <utils/util.h>

static bool test_finished;

typedef struct timer_test_data {
    int curr_count;
    int goal_count;
} timer_test_data_t;

static int test_callback(uintptr_t token)
{
    assert(token);
    timer_test_data_t *test_data = (timer_test_data_t *) token;
    test_data->curr_count++;
    if (test_data->curr_count == test_data->goal_count) {
        test_finished = true;
    }
    return 0;
}

int test_timer_increment(driver_env_t env)
{
    uint64_t end = 0;
    uint64_t start = timestamp(env);
    /* Looping 10000 times seems a good trade off for the timer to increment.
     * Increment the value if it is not enough in a certain platform an leave
     * a brief note.
     */
    for (int i = 0; i < 100000; i++) {
        end = timestamp(env);
        if (end > start) {
            break;
        }
    }

    test_gt(end, start);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0001, "Test basic timer increment",
                      test_timer_increment, config_set(CONFIG_HAVE_TIMER))

int test_timer_timeouts(driver_env_t env)
{
    const uint64_t timeouts_ms[] = {500, 200, 100, 50, 20, 10, 5, 2, 1};

    int error = tm_alloc_id_at(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    for (int i = 0; i < ARRAY_SIZE(timeouts_ms); i++) {
        uint64_t timeout_ms = timeouts_ms[i];
        uint64_t start = timestamp(env);
        /* Use convenience function on top of TIMER_ID. */
        timeout(env, timeout_ms * NS_IN_MS, TIMEOUT_RELATIVE);
        wait_for_timer_interrupt(env);
        uint64_t end = timestamp(env);
        timer_reset(env);
        uint64_t delta = end - start;
        ZF_LOGD("timeout of %"PRIu64" ms took %"PRIu64".%06"PRIu64" ms",
                timeout_ms, delta / NS_IN_MS, delta % NS_IN_MS);
    }

    error = tm_free_id(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    error = ltimer_reset(&env->ltimer);
    test_assert_fatal(!error);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0002, "Test various timeouts",
                      test_timer_timeouts, config_set(CONFIG_HAVE_TIMER))

int test_ltimer_periodic(driver_env_t env)
{
    uint64_t timeout_ms = 100;
    int error = ltimer_set_timeout(&env->ltimer, timeout_ms * NS_IN_MS,
                                   TIMEOUT_PERIODIC);
    test_assert_fatal(!error);

    for (int i = 0; i < 10; i++) {
        wait_for_timer_interrupt(env);
        uint64_t now_ns = timestamp(env);
        /* printing is uncritical with a 100 ms tick */
        ZF_LOGD("%"PRIu64" ms tick, timestamp %"PRIu64".%09"PRIu64" sec",
                timeout_ms, now_ns / NS_IN_S, now_ns % NS_IN_S);
    }

    error = ltimer_reset(&env->ltimer);
    test_assert_fatal(!error);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0003, "Test periodic ltimer",
                      test_ltimer_periodic, config_set(CONFIG_HAVE_TIMER))

int test_timer_periodic_callback(driver_env_t env)
{
    test_finished = false;
    timer_test_data_t test_data = { .goal_count = 5 };

    int error = tm_alloc_id_at(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    error = tm_register_cb(&env->tm, TIMEOUT_PERIODIC, 1 * NS_IN_S, 0, TIMER_ID,
                           test_callback, (uintptr_t) &test_data);
    test_assert_fatal(!error);

    while (!test_finished) {
        wait_for_timer_interrupt(env);
        uint64_t now_ns = timestamp(env);
        error = tm_update(&env->tm); /* invokes test_callback() */
        test_assert_fatal(!error);
        /* printing is uncritical with a 1 second tick */
        ZF_LOGD("Tick, timestamp %"PRIu64".%09"PRIu64" sec", now_ns / NS_IN_S, now_ns % NS_IN_S);
    }

    error = tm_free_id(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    error = ltimer_reset(&env->ltimer);
    test_assert_fatal(!error);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0004, "Test periodic timer callback",
                      test_timer_periodic_callback, config_set(CONFIG_HAVE_TIMER))

int test_time_manager_alert_in_the_past(driver_env_t env)
{
    int error = tm_alloc_id_at(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    uint64_t start = timestamp(env);
    while (timestamp(env) <= start) {
        /* Loop until the timer has incremented. */
    }
    /*
     * The time manager implementation does not allow setting a timeeout in
     * the past, it returns ETIME instead.
     */
    error = tm_register_cb(&env->tm, TIMEOUT_ABSOLUTE, start, 0, TIMER_ID, NULL, 0);
    test_eq(error, ETIME);

    error = tm_free_id(&env->tm, TIMER_ID);
    test_assert_fatal(!error);

    error = ltimer_reset(&env->ltimer);
    test_assert_fatal(!error);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0005, "Setting a time manager alert in the past fails",
                      test_time_manager_alert_in_the_past, config_set(CONFIG_HAVE_TIMER))

int test_ltimer_alert_in_the_past(driver_env_t env)
{
    uint64_t start = timestamp(env);
    while (timestamp(env) <= start) {
        /* Loop until the timer has incremented. */
    }
    /*
     * The logical timer implementation is a wrapper around the actual hardware
     * timer. To avoid race conditions, it must either ensure the interrupt is
     * triggered immediately if the timeout is in the past. Or it return ETIME
     * if it can properly detect timeouts in the past.
     */
    int error = ltimer_set_timeout(&env->ltimer, start, TIMEOUT_ABSOLUTE);
    if (error) {
        test_eq(error, ETIME);
        ZF_LOGI("ltimer implementation does not support setting timestamp in the past.");
    } else {
        uint64_t start = timestamp(env);
        wait_for_timer_interrupt(env);
        uint64_t end = timestamp(env);
        error = tm_update(&env->tm);
        test_assert_fatal(!error);
        uint64_t delta = end - start;
        ZF_LOGD("waiting time %"PRIu64".%06"PRIu64" ms", delta / NS_IN_MS, delta % NS_IN_MS);
        /*
         * Tests have shown that we should allow up to 10 ms to pass. This copes
         * with QEMU issues and timers running barely at a ms resolution. Note
         * that this basic  test is just about getting the interrpt in general,
         * not about timer accuracy.
         */
        //test_lt(timestamp(env), start + 10 * NS_IN_MS);
    }

    error = ltimer_reset(&env->ltimer);
    test_assert_fatal(!error);

    return sel4test_get_result();
}

DEFINE_TEST_BOOTSTRAP(TIMER0006, "Set logical timer alert in the past",
                      test_ltimer_alert_in_the_past, config_set(CONFIG_HAVE_TIMER))
