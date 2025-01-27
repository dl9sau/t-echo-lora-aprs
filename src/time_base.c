/*
 * vim: noexpandtab
 *
 * Copyright (c) 2021-2022 Thomas Kolb <cfr34k-git@tkolb.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <app_timer.h>
#include <sdk_macros.h>
#include <nrf_log.h>

#include "time_base.h"

#define TRACKING_TIMER_INTERVAL_MS 180000

#define APP_TIMER_TICKS_PER_SEC (APP_TIMER_CLOCK_FREQ / (APP_TIMER_CONFIG_RTC_FREQUENCY + 1))

static uint64_t m_cur_time;
static uint32_t m_cnt_last;
static uint32_t m_lost_subticks_accumulator;

APP_TIMER_DEF(m_tracking_timer);


static void update_time(void)
{
	uint32_t cnt_now  = app_timer_cnt_get();
	uint32_t delta    = app_timer_cnt_diff_compute(cnt_now, m_cnt_last);

	uint32_t delta_subticks = delta * 1000;

	uint32_t delta_ms = delta_subticks / APP_TIMER_TICKS_PER_SEC;
	uint32_t delta_ms_with_lost = (delta_subticks + m_lost_subticks_accumulator) / APP_TIMER_TICKS_PER_SEC;

	if(delta_ms != delta_ms_with_lost)
	{ // at least one millisecond has been taken over from the lost subticks accumulator
		m_lost_subticks_accumulator = (delta_subticks + m_lost_subticks_accumulator) % APP_TIMER_TICKS_PER_SEC;
	}
	else
	{
		// remainder of the division = time not represented in delta_ms
		uint32_t lost_subticks = delta_subticks % APP_TIMER_TICKS_PER_SEC;

		// accumulate remainder across updates
		m_lost_subticks_accumulator += lost_subticks;
	}

	m_cnt_last = cnt_now;

	m_cur_time += delta_ms_with_lost;

	//NRF_LOG_INFO("delta: %8d => ms: %7d; accu: %8d", delta, delta_ms_with_lost, m_lost_subticks_accumulator);
}


static void cb_tracking_timer(void *context)
{
	update_time();
}


ret_code_t time_base_init(void)
{
	m_cur_time = 0;
	m_cnt_last = app_timer_cnt_get();

	VERIFY_SUCCESS(app_timer_create(&m_tracking_timer, APP_TIMER_MODE_REPEATED, cb_tracking_timer));
	VERIFY_SUCCESS(app_timer_start(m_tracking_timer, APP_TIMER_TICKS(TRACKING_TIMER_INTERVAL_MS), NULL));

	return NRF_SUCCESS;
}


uint64_t time_base_get(void)
{
	update_time();
	return m_cur_time;
}
