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
#include <stdio.h>

#include "utils.h"


#define F_PI   3.141592653589793f

#define EARTH_RADIUS_M   6371000
float great_circle_distance_m(float lat1, float lon1, float lat2, float lon2)
{
	// convert to radians
	lat1 *= F_PI / 180.0f;
	lon1 *= F_PI / 180.0f;
	lat2 *= F_PI / 180.0f;
	lon2 *= F_PI / 180.0f;

	// calculation using the haversine formula from
	// https://en.wikipedia.org/wiki/Great-circle_distance
	float sin_dlat_over_2 = sinf((lat2 - lat1) * 0.5f);
	float sin_dlon_over_2 = sinf((lon2 - lon1) * 0.5f);
	float sin_sumlat_over_2 = sinf((lat2 + lat1) * 0.5f);

	float sin_sq_dlat_over_2 = sin_dlat_over_2 * sin_dlat_over_2;
	float sin_sq_dlon_over_2 = sin_dlon_over_2 * sin_dlon_over_2;
	float sin_sq_sumlat_over_2 = sin_sumlat_over_2 * sin_sumlat_over_2;

	float arg = sqrtf(sin_sq_dlat_over_2 + (1 - sin_sq_dlat_over_2 - sin_sq_sumlat_over_2) * sin_sq_dlon_over_2);
	float angle = 2.0f * asinf(arg);
	return angle * EARTH_RADIUS_M;
}


float direction_angle(float lat1, float lon1, float lat2, float lon2)
{
	// convert to radians
	lat1 *= F_PI / 180.0f;
	lon1 *= F_PI / 180.0f;
	lat2 *= F_PI / 180.0f;
	lon2 *= F_PI / 180.0f;

	float lon12 = lon2 - lon1;

	float numer = cosf(lat2) * sinf(lon12);
	float denum = cosf(lat1) * sinf(lat2) - sinf(lat1) * cosf(lat2) * cosf(lon12);

	float angle = atan2f(numer, denum) * (180.0f / F_PI);

	if(angle < 0) {
		angle = 360.0f + angle;
	}

	return angle;
}


void format_float(char *s, size_t s_len, float f, uint8_t decimals)
{
	char fmt[32];

	int32_t factor = 1;

	// mitigate rounding errors
        if (f >= 0.0f) {
        	f =  f + (5.0f/ pow(10, decimals +1));
	} else {
        	f =  f - (5.0f/ pow(10, decimals +1));
	}

	for(uint8_t i = 0; i < decimals; i++) {
		factor *= 10;
	}

	snprintf(fmt, sizeof(fmt), "%%s%%d.%%0%dd", decimals);

	snprintf(s, s_len, fmt,
	         (f < 0 && f > -1.0) ? "-" : "",
	         (int32_t)f,
	         (int32_t)(((f > 0) ? (f - (int32_t)f) : ((int32_t)f - f)) * factor));
}


void format_position_nautical(char *s, size_t s_len, float f, uint8_t decimals, int is_latitude)
{
	char fmt[32];
        int i_deg;
        char c_nswe = is_latitude ? 'N' : 'E';

        if (f < 0) {
          f *= -1;
          c_nswe = is_latitude ? 'S' : 'W';
        }
        i_deg = (int ) f;
        f = (f - i_deg) * 60.0; // f is now minutes.decimals

	// mitigate rounding errors
        f = f + (5.0f/ pow(10, decimals +1));

	if (f >= 60.0f) {
        	f = 60 - (1.0/ pow(10, decimals));
	}

	int32_t factor = 1;

	for(uint8_t i = 0; i < decimals; i++) {
		factor *= 10;
	}

	snprintf(fmt, sizeof(fmt), "%%0%dd-%%0%dd,%%0%dd%%c", (is_latitude ? 2 : 3), 2, decimals);

	snprintf(s, s_len, fmt,
                 i_deg,
	         (int32_t)f,
	         (int32_t)((f - (int32_t)f) * factor),
	         c_nswe);

        // something buged here. We get 5 decimals.
        //s[is_latitude ? 9 : 10] = c_nswe;
        //s[is_latitude ? 10 : 11] = 0;
}
