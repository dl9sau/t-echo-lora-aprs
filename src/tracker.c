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

#include <math.h>

#include <app_timer.h>
#include <nrf_log.h>

#include "lora.h"
#include "time_base.h"
#include "utils.h"

#include "tracker.h"

static float m_last_tx_heading = 0.0f;

static float m_last_tx_lat = 0.0f;
static float m_last_tx_lon = 0.0f;

static uint64_t m_last_tx_time = 0;
static uint64_t m_last_wx_time = 0;

static uint32_t m_tx_counter = 0;

static tracker_callback m_callback;

ret_code_t tracker_init(tracker_callback callback)
{
	m_callback = callback;

	return NRF_SUCCESS;
}


ret_code_t tracker_run(const nmea_data_t *data, aprs_args_t *args)
{
	bool do_tx = false;

	uint64_t now = time_base_get();

        if ((now - m_last_tx_time) > 20000) {
          if ((now - m_last_wx_time) > WX_INTERVAL_MS) {
            if (m_last_wx_time) {
	      uint8_t message[APRS_MAX_FRAME_LEN];
	      size_t frame_len;
	      frame_len = aprs_build_frame(message, args, PACKET_TYPE_WX);
              if (frame_len > 0) {
	        //args->frame_id = ++m_tx_counter;
	        NRF_LOG_INFO("tracker: sending WX report");
	        NRF_LOG_INFO("Generated frame:");
	        NRF_LOG_HEXDUMP_INFO(message, frame_len);
	        lora_send_packet(message, frame_len);
		m_callback(TRACKER_EVT_TRANSMISSION_STARTED);
              }
            }
            m_last_wx_time = now;
	    return NRF_SUCCESS;
          }
        }
        if (m_last_tx_time && m_last_wx_time && (now - m_last_wx_time) < 20000) {
	  // do not transmit too often
	  return NRF_ERROR_BUSY;
        }

	if(!data->pos_valid) {
		// do not transmit invalid positions
		return NRF_ERROR_INVALID_DATA;
	}

	if((now - m_last_tx_time) < MIN_TX_INTERVAL_MS) {
		// do not transmit too often
		return NRF_ERROR_BUSY;
	}


	if((now - m_last_tx_time) > MAX_TX_INTERVAL_MS) {
		// transmit if the previous one was too long ago
		NRF_LOG_INFO("tracker: forced tx after %d ms idle", now - m_last_tx_time);
		do_tx = true;
	}

	if(data->speed_heading_valid && (data->speed >= HEADING_CHECK_MIN_SPEED)) {
		float delta_heading = data->heading - m_last_tx_heading;

		if(delta_heading < -180.0f) {
			delta_heading += 360.0f;
		} else if(delta_heading > 180.0f) {
			delta_heading -= 360.0f;
		}

		if(delta_heading < 0.0f) {
			delta_heading = -delta_heading;
		}

		if(delta_heading >= MAX_HEADING_DELTA_DEG) {
			NRF_LOG_INFO("tracker: heading changed too much: was: %d, is: %d, delta: %d", (int)(m_last_tx_heading + 0.5f), (int)(data->heading + 0.5f), (int)(delta_heading + 0.5f));
			do_tx = true;
		}
	}

	float distance = great_circle_distance_m(
			data->lat, data->lon,
			m_last_tx_lat, m_last_tx_lon);

	if(distance >= MAX_DISTANCE_M) {
		NRF_LOG_INFO("tracker: distance since last TX too high: %d m", (int)(distance + 0.5f));
		do_tx = true;
	}

	if(do_tx) {
		if(data->speed_heading_valid) {
			m_last_tx_heading = data->heading;
		}

		m_last_tx_lat = data->lat;
		m_last_tx_lon = data->lon;
		m_last_tx_time = now;

		// generate a new APRS packet
		uint8_t message[APRS_MAX_FRAME_LEN];
		size_t  frame_len;

		aprs_update_pos_time(data->lat, data->lon, data->altitude, now / 1000);

		args->frame_id = ++m_tx_counter;
		//frame_len = aprs_build_frame(message, args);
		frame_len = aprs_build_frame(message, args, PACKET_TYPE_POSITION);

		if (frame_len > 0) {
		  NRF_LOG_INFO("Generated frame:");
		  NRF_LOG_HEXDUMP_INFO(message, frame_len);

		  lora_send_packet(message, frame_len);
                }

		m_callback(TRACKER_EVT_TRANSMISSION_STARTED);
	}

	return NRF_SUCCESS;
}


void tracker_force_tx(void)
{
	// force transmission by resetting the last transmission time.
	m_last_tx_time = 0;
	m_last_wx_time = 0;
}


uint32_t tracker_get_tx_counter(void)
{
	return m_tx_counter;
}


void tracker_reset_tx_counter(void)
{
	m_tx_counter = 0;
}
