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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <nrf_log.h>
#include <sdk_macros.h>

#include "nmea.h"

static uint8_t hexchar2num(char hex)
{
	if((hex >= '0') && (hex <= '9')) {
		return hex - '0';
	} else if((hex >= 'A') && (hex <= 'F')) {
		return hex - 'A' + 10;
	} else if((hex >= 'a') && (hex <= 'f')) {
		return hex - 'a' + 10;
	} else {
		NRF_LOG_WARNING("'%c' is not a valid hexadecimal digit.", hex);
		return 0;
	}
}

#define INVALID_COORD 1024.0f

static float nmea_coord_to_float(const char *token)
{
	char *dot = strchr(token, '.');
	if(!dot) {
		NRF_LOG_ERROR("nmea: could not find float in coordinate: '%s'", token);
		return INVALID_COORD;
	}

	size_t dotpos = dot - token;

	if((dotpos != 4) && (dotpos != 5)) {
		NRF_LOG_ERROR("nmea: wrong dot position %d in coordinate: '%s'", dotpos, token);
		return INVALID_COORD;
	}

	size_t degrees_len = dotpos - 2;

	float minutes;
	char *endptr;

	minutes = strtof(token + degrees_len, &endptr);

	if(!endptr) {
		NRF_LOG_ERROR("nmea: could not convert minute value to float: '%s'", token + degrees_len);
		return INVALID_COORD;
	}

	char degstr[4];
	strncpy(degstr, token, degrees_len);
	degstr[degrees_len] = '\0';

	long degrees = strtol(degstr, &endptr, 10);

	if(!endptr) {
		NRF_LOG_ERROR("nmea: could not convert degrees string to integer: '%s'", degstr);
		return INVALID_COORD;
	}

	return (float)degrees + minutes / 60.0f;
}

static float nmea_sign_from_char(char *polarity)
{
	char c = polarity[0];

	if((c == 'N') || (c == 'E')) {
		return 1.0f;
	} else if((c == 'S') || (c == 'W')) {
		return -1.0f;
	} else {
		NRF_LOG_ERROR("nmea: polarity char is not one of NSEW: '%s'", polarity);
		return INVALID_COORD;
	}
}

/**@brief Tokenize the given string into parts separated by given character.
 * @details
 * This works like the standard C function strtok(), but can recognize empty fields.
 */
static char* nmea_tokenize(char *input_str, char sep)
{
	static char *next_token_ptr = NULL;

	if(input_str) {
		next_token_ptr = input_str;
	}

	if(!next_token_ptr) {
		return NULL;
	}

	char *cur_token = next_token_ptr;

	char *next_sep = strchr(next_token_ptr, sep);
	if(!next_sep) {
		next_token_ptr = NULL;
	} else {
		*next_sep = '\0';
		next_token_ptr = next_sep + 1;
	}

	return cur_token;
}

static void fix_info_to_data_struct(nmea_data_t *data,
		bool auto_mode, int fix_type, float pdop, float hdop, float vdop,
		uint8_t used_sats, uint8_t sys_id)
{
	size_t empty_idx = NMEA_NUM_FIX_INFO;
	size_t found_idx = NMEA_NUM_FIX_INFO;

	// scan the existing data
	for(size_t i = 0; i < NMEA_NUM_FIX_INFO; i++) {
		uint8_t scan_sys_id = data->fix_info[i].sys_id;

		if(scan_sys_id == sys_id) {
			found_idx = i;
			break;
		}

		if(empty_idx == NMEA_NUM_FIX_INFO
				&& scan_sys_id == NMEA_SYS_ID_INVALID) {
			empty_idx = i;
		}
	}

	size_t use_idx = found_idx;

	if(use_idx == NMEA_NUM_FIX_INFO) {
		// existing entry not found, try to allocate a new one
		if(empty_idx == NMEA_NUM_FIX_INFO) {
			// no free space exists in the struct, abort
			return;
		}

		use_idx = empty_idx;

		// mark as used by this system
		data->fix_info[use_idx].sys_id = sys_id;
	}

	data->fix_info[use_idx].auto_mode = auto_mode;
	data->fix_info[use_idx].sats_used = used_sats;
	data->fix_info[use_idx].fix_type  = fix_type;

	// update generic info from this fix
	data->pdop = pdop;
	data->hdop = hdop;
	data->vdop = vdop;
}

ret_code_t nmea_parse(char *sentence, bool *position_updated, nmea_data_t *data)
{
	if(position_updated != NULL) {
		*position_updated = false;
	}

	if(sentence[0] != '$') {
		NRF_LOG_ERROR("nmea: sentence does not start with '$'");
		return NRF_ERROR_INVALID_DATA;
	}

	size_t len = strlen(sentence);

	// strip newlines and carriage-returns from the end
	char *endptr = sentence + len - 1;
	while((endptr > sentence) &&
			((*endptr == '\n') || (*endptr == '\r'))) {
		endptr--;
		len--;
	}

	sentence[len] = '\0';

	// try to find and extract the checksum, which starts at a '*'
	endptr = sentence + len - 1;
	while((endptr > sentence) && (*endptr != '*')) {
		endptr--;
	}

	if(endptr == sentence) {
		NRF_LOG_ERROR("nmea: checksum not found. Sentence incomplete? %s", NRF_LOG_PUSH(sentence));
		return NRF_ERROR_INVALID_DATA;
	} else {
		char *checksum_str = endptr + 1;

		// string ends at the asterisk before the checksum
		*endptr = '\0';

		uint8_t checksum =
			(hexchar2num(checksum_str[0]) << 4)
			+ hexchar2num(checksum_str[1]);

		uint8_t checksum_calc = 0;
		uint8_t *ptr = (uint8_t*)(sentence + 1); // skip the '$'

		while(*ptr) {
			checksum_calc ^= *ptr++;
		}

		if(checksum_calc != checksum) {
			NRF_LOG_ERROR("nmea: checksum invalid! Expected: %02x, calculated: %02x", checksum, checksum_calc);
			return NRF_ERROR_INVALID_DATA;
		}
	}

	char *token = nmea_tokenize(sentence + 1, ','); // skip the '$' in the beginning

	if(strcmp(token, "GNGGA") == 0 || strcmp(token, "GPGGA") == 0) {
		// parse Detailed GNSS position information
		size_t info_token_idx = 0;

		float lat = INVALID_COORD, lon = INVALID_COORD, altitude = 0.0f;
		bool data_valid = false;

		while((token = nmea_tokenize(NULL, ','))) {
			switch(info_token_idx) {
				// case 0: time

				case 1:
					lat = nmea_coord_to_float(token);
					break;

				case 2:
					lat *= nmea_sign_from_char(token);
					break;

				case 3:
					lon = nmea_coord_to_float(token);
					break;

				case 4:
					lon *= nmea_sign_from_char(token);
					break;

				case 5: // quality indicator
					switch(token[0]) {
						case '0': // no position
							data_valid = false;
							break;

						case '1': // no differential corrections (autonomous)
						case '2': // differentially corrected position (SBAS, DGPS,Atlas DGPSservice, L- Dif and e-Dif)
						case '3': // ???
						case '4': // RTK fixed or Atlas high precision services converged
						case '5': // RTK float,Atlas high precision services converging
							data_valid = true;
							break;

						default:
							data_valid = false;
							break;
					}
					break;

				// case 6: number of satellites in solution
				// case 7: HDOP

				case 8: // altitude
					altitude = strtof(token, NULL);
					break;

				// case 9: unit of altitude
				// case 10: geoidal separation
				// case 11: unit of geoidal separation
				// case 12: age of differential corrections in seconds
				// case 13: DGPS station ID
			}

			info_token_idx++;
		}

		if(data_valid) {
			//NRF_LOG_INFO("Got valid position: Lat: " NRF_LOG_FLOAT_MARKER ", Lon: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(lat), NRF_LOG_FLOAT(lon));

			data->lat = lat;
			data->lon = lon;
			data->altitude = altitude;
			data->pos_valid = true;
		} else {
			data->pos_valid = false;
		}

		if(position_updated != NULL) {
			*position_updated = true;
		}
	} else if(strcmp(token, "GNRMC") == 0) {
		// parse date, time, ground speed and heading
		size_t info_token_idx = 0;

		float speed_knots = 0.0f, heading = 0.0f;
		bool data_valid = false;

		int8_t time_h = -1, time_m = -1, time_s = -1;
		int8_t date_d = -1, date_m = -1, date_y = -1;
		char timeparser_tmp[3];
		timeparser_tmp[2] = '\0';

		while((token = nmea_tokenize(NULL, ','))) {
			switch(info_token_idx) {
				case 0: // time
					if(strlen(token) < 6) {
						continue;
					}

					timeparser_tmp[0] = token[0];
					timeparser_tmp[1] = token[1];
					time_h = strtod(timeparser_tmp, NULL);

					timeparser_tmp[0] = token[2];
					timeparser_tmp[1] = token[3];
					time_m = strtod(timeparser_tmp, NULL);

					timeparser_tmp[0] = token[4];
					timeparser_tmp[1] = token[5];
					time_s = strtod(timeparser_tmp, NULL);

					break;

				case 6: // speed
					speed_knots = strtof(token, NULL);
					break;

				case 7: // heading
					heading = strtof(token, NULL);
					break;

				case 8: // date
					if(strlen(token) < 6) {
						continue;
					}

					timeparser_tmp[0] = token[0];
					timeparser_tmp[1] = token[1];
					date_d = strtod(timeparser_tmp, NULL);

					timeparser_tmp[0] = token[2];
					timeparser_tmp[1] = token[3];
					date_m = strtod(timeparser_tmp, NULL);

					timeparser_tmp[0] = token[4];
					timeparser_tmp[1] = token[5];
					date_y = strtod(timeparser_tmp, NULL);
					break;

				case 11: // quality indicator
					if(token[0] == 'E' || token[0] == 'A' || token[0] == 'D') {
						data_valid = true;
					}
			}

			info_token_idx++;
		}

		if(data_valid) {
			//NRF_LOG_INFO("Got valid speed: " NRF_LOG_FLOAT_MARKER ", heading: " NRF_LOG_FLOAT_MARKER, NRF_LOG_FLOAT(speed), NRF_LOG_FLOAT(heading));

			data->speed = speed_knots * 0.5144444f;
			data->heading = heading;
			data->speed_heading_valid = true;

			if(time_h >= 0 && time_h <= 23
						&& time_m >= 0 && time_m <= 59
						&& time_s >= 0 && time_s <= 59
						&& date_d >= 1 && date_d <= 31
						&& date_m >= 1 && date_m <= 12
						&& date_y >= 0 && date_y <= 99) {
				// WARNING: this assignment will only work properly until 2099.
				// Alternatively the GNZDA sentence, which contains the full
				// year, could be parsed for date and time, but I'm not sure if
				// that’s available on all devices.
				data->datetime.time_h = time_h;
				data->datetime.time_m = time_m;
				data->datetime.time_s = time_s;
				data->datetime.date_d = date_d;
				data->datetime.date_m = date_m;
				data->datetime.date_y = 2000 + (uint16_t)date_y;

				data->datetime_valid = true;
			} else {
				data->datetime_valid = true;
			}
		} else {
			data->speed_heading_valid = false;
			data->datetime_valid = false;
		}

		if(position_updated != NULL) {
			*position_updated = true;
		}

	} else if(strcmp(token, "GNGSA") == 0) {
		// parse DOP and Active Satellites sentence.
		size_t info_token_idx = 0;

		bool auto_mode = false;
		int fix_type = -1;
		float pdop = 0.0f, hdop = 0.0f, vdop = 0.0f;
		uint8_t used_sats = 0;
		uint8_t sys_id = 0;

		while((token = nmea_tokenize(NULL, ','))) {
			switch(info_token_idx) {
				case 0:
					if(token[0] == 'A') {
						auto_mode = true;
					}
					break;

				case 1:
					if(token[0] >= '1' && token[0] <= '3') {
						fix_type = token[0] - '1';
					}
					break;

				case 14:
					pdop = strtof(token, NULL);
					break;

				case 15:
					hdop = strtof(token, NULL);
					break;

				case 16:
					vdop = strtof(token, NULL);
					break;

				case 17:
					sys_id = hexchar2num(token[0]);
					break;
			}

			if((info_token_idx >= 2) && (info_token_idx <= 13)) {
				if(token[0] != '\0') {
					used_sats++;
				}
			}

			info_token_idx++;
		}

		if(fix_type >= 0) {
			fix_info_to_data_struct(data, auto_mode, fix_type, pdop, hdop, vdop, used_sats, sys_id);
		}
	} else if(strcmp(token, "GPGSV") == 0 || strcmp(token, "GLGSV") == 0) {
		// parse Satellites in View sentence for GPS and GLONASS
		size_t info_token_idx = 0;

		bool is_gps = (token[1] == 'P');

		nmea_sat_info_t *sat_list  = is_gps ? data->sat_info_gps          : data->sat_info_glonass;
		uint8_t         *sat_count = is_gps ? &(data->sat_info_count_gps) : &(data->sat_info_count_glonass);

		uint8_t current_sentence = 0;
		uint8_t sat_id = 0;
		while((token = nmea_tokenize(NULL, ','))) {
			switch(info_token_idx) {
				case 1:
					current_sentence = strtod(token, NULL);
					if(current_sentence == 1) {
						// reset the satellite list
						*sat_count = 0;
					}
					break;
			}

			if(info_token_idx >= 3 && ((info_token_idx - 3) % 4) == 0) {
				sat_id = strtod(token, NULL);
			}

			if((*sat_count < NMEA_NUM_SAT_INFO)
					&& info_token_idx >= 6
					&& ((info_token_idx - 6) % 4) == 0) {
				sat_list[*sat_count].sat_id = sat_id;
				if(token[0] != '\0') {
					sat_list[*sat_count].snr = strtod(token, NULL);
				} else {
					sat_list[*sat_count].snr = -1; // not tracked
				}

				(*sat_count)++;
			}

			info_token_idx++;
		}
	}

	return NRF_SUCCESS;
}


const char* nmea_fix_type_to_string(uint8_t fix_type)
{
	switch(fix_type)
	{
		case NMEA_FIX_TYPE_NONE: return "none";
		case NMEA_FIX_TYPE_2D:   return "2D";
		case NMEA_FIX_TYPE_3D:   return "3D";
		default:                 return NULL; // unknown
	}
}


const char* nmea_sys_id_to_short_name(uint8_t sys_id)
{
	switch(sys_id)
	{
		case NMEA_SYS_ID_INVALID: return "unk";
		case NMEA_SYS_ID_GPS:     return "GPS";
		case NMEA_SYS_ID_GLONASS: return "GLO";
		case NMEA_SYS_ID_GALILEO: return "GAL";
		case NMEA_SYS_ID_BEIDOU:  return "BD";
		case NMEA_SYS_ID_QZSS:    return "QZ";
		case NMEA_SYS_ID_NAVIC:   return "NAV";
		default:                  return NULL; // unknown
	}
}
