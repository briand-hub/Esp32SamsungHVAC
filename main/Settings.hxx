/** Copyright (C) 2023 briand (https://github.com/briand-hub)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#ifndef BRIAND_SAMSUNGHVAC_32_SETTINGS
	#define BRIAND_SAMSUNGHVAC_32_SETTINGS

	#define WIFI_ESSID 			"<yout network>"				// Must be 2.4GHz
	#define WIFI_PASSW			"<your password>"
	#define WIFI_TIMEOUT		60 						// Connection timeout in seconds
	#define WIFI_HOST			"ESP32SAMSUNG" 			// Connection timeout in seconds
	#define WEB_SERVER_PORT 	80						// Web server port
	#define WEB_SERVER_STACK 	4096					// Web server task stack size (default 4096)
	#define SERIALTASK_STACK 	4096					// Serial worker task stack size (default 4096)
	#define LOG_TAG				"SAMSUNG"				// TAG for ESP_LOG

#endif /* BRIAND_SAMSUNGHVAC_32_SETTINGS */