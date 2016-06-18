/*
 * Copyright (c) 2015 Eric Holland
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <err.h>
#include <debug.h>
#include <trace.h>
#include <kernel/event.h>
#include <arch/arm/cm.h>
#include <platform.h>
#include <platform/nrf52.h>
#include <platform/system_nrf52.h>
#include <platform/timer.h>
#include <sys/types.h>


static volatile uint64_t ticks;
static uint32_t tick_rate = 0;
static uint32_t tick_rate_mhz = 0;
static uint32_t tick_increment_us;
static uint32_t tick_increment;
static lk_time_t tick_interval_ms;

static platform_timer_callback cb;
static void *cb_args;

static event_t * cc1_event_p;

typedef enum handler_return (*platform_timer_callback)(void *arg, lk_time_t now);

status_t platform_set_periodic_timer(platform_timer_callback callback, void *arg, lk_time_t interval)
{

    cb = callback;
    cb_args = arg;

    cc1_event_p = NULL;

    tick_interval_ms = interval;

    tick_increment = tick_rate / ( 1000 / interval );
    tick_increment_us = tick_increment*1000000/tick_rate;

    NRF_CLOCK->LFCLKSRC =  CLOCK_LFCLKSRC_SRC_Xtal << CLOCK_LFCLKSRC_SRC_Pos;
    NRF_CLOCK->TASKS_LFCLKSTART = 1;

    NRF_RTC1->TASKS_STOP = 1;
    NRF_RTC1->TASKS_CLEAR = 1;

    NRF_RTC1->PRESCALER = 0;
    NRF_RTC1->CC[0] = tick_increment;

    NRF_RTC1->INTENSET  = RTC_INTENSET_COMPARE0_Enabled << RTC_INTENSET_COMPARE0_Pos;

    NRF_RTC1->EVENTS_COMPARE[0] = 0;
    NRF_RTC1->TASKS_START = 1;
    NVIC_EnableIRQ(RTC1_IRQn);

    return NO_ERROR;
}

lk_time_t current_time(void)
{
    uint64_t t;

    do {
        t = ticks;
    } while (ticks != t);

    return t * tick_interval_ms;
}

lk_bigtime_t current_time_hires(void)
{
    return current_time() * 1000;
}


void nrf_evt_timeout( event_t * event_p, uint32_t delay_ticks) {
    
    cc1_event_p = event_p;
    NRF_RTC1->CC[1] = NRF_RTC1->COUNTER + delay_ticks;
    NRF_RTC1->EVENTS_COMPARE[1] = 0;
    NRF_RTC1->INTENSET  = RTC_INTENSET_COMPARE1_Enabled << RTC_INTENSET_COMPARE1_Pos;
   
}

void nrf52_RTC1_IRQ(void)
{
    ticks += tick_increment_us;
    arm_cm_irq_entry();
    bool resched = false;

    if (NRF_RTC1->EVENTS_COMPARE[0] == 1 ) {
        NRF_RTC1->EVENTS_COMPARE[0] = 0;
        NRF_RTC1->CC[0] += tick_increment;

        if (cb) {
            lk_time_t now = current_time();
            if (cb(cb_args, now) == INT_RESCHEDULE)
                resched = true;
        }
    }
    if ((NRF_RTC1->EVENTS_COMPARE[1] == 1) && (NRF_RTC1->INTENSET & RTC_INTENSET_COMPARE1_Msk)) {
        NRF_RTC1->EVENTS_COMPARE[1] = 0;
        NRF_RTC1->INTENCLR  = RTC_INTENSET_COMPARE1_Msk;
        event_signal(cc1_event_p,false);
        resched = true;
    }
    arm_cm_irq_exit(resched);
}


void arm_cm_systick_init(uint32_t mhz)
{
    tick_rate = mhz;
    tick_rate_mhz = mhz / 1000000;
}

