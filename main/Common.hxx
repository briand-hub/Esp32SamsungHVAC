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

#ifndef BRIAND_SAMSUNGHVAC_32_COMMON
	#define BRIAND_SAMSUNGHVAC_32_COMMON

	#include <cstdio>
	#include <iostream>
	#include <thread>
	#include <mutex>
	#include <queue>
	#include <fstream>

	#include <freertos/FreeRTOS.h>
	#include <freertos/task.h>
	#include <esp_system.h>
	#include <esp_task.h>
	#include <esp_log.h>
	#include <nvs_flash.h>
	#include <esp_pthread.h>
	#include <driver/uart.h>
	#include <driver/gpio.h>
	#include <esp_http_server.h>
	#include <esp_spiffs.h>
	#include <sys/stat.h>
	#include <unistd.h>
	#include <esp_netif.h>
	#include <esp_timer.h>

	using namespace std;

#endif /* BRIAND_SAMSUNGHVAC_32_COMMON */