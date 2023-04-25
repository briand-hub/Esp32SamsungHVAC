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

#ifndef BRIANDLIBESP32IDF_H
#define BRIANDLIBESP32IDF_H

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <cstring>

#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"


namespace Briand {

	using namespace std;

	/** @brief Static class with system info/utilities */
	class Esp32System {
		public:

		static esp_chip_info_t chip_info;
    	static uint32_t flash_size;
		static uint32_t heap_size;
		static uint32_t heap_free;
		static uint32_t psram_size;
		static uint32_t psram_free;
		static bool has_psram;

		/** @brief Collects info about ESP system */
		static void CollectInfo();

		/** @brief Print informations */
		static void PrintInfo();
	};

	/** @brief Simplified Wifi objects for station/ap */
	class WifiManager {

		/*
			Most of the code has been reviewed from ESP IDF example: 
			https://github.com/espressif/esp-idf/blob/release/v5.0/examples/wifi/getting_started/station/main/station_example_main.c
		*/

		private:
		
		/** @brief ESP LOG Tag */
		static const char* TAG;

		/** @brief FreeRTOS event group to signal when we are connected */
		static EventGroupHandle_t FreeRTOSEventGroup_STA;
		static EventGroupHandle_t FreeRTOSEventGroup_AP;

		/* Support variables in order to not re-do some tasks */
		static bool EspNetIfEventsDone;
		static bool EspWifiInitialized;
		static bool EventHandlerRegistered;
		static bool StaStarted;
		static bool AutoReconnect;
		static esp_netif_t* InterfaceSTA; 
		static esp_netif_t* InterfaceAP; 
		static wifi_config_t WifiConfiguration; 
		static int WifiStaRemainingTentatives; 

		/** @brief FreeRTOS event handler (for wifi, both STA and AP) */
		static void EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

		/** @brief Attach events to be managed with event handler*/
		void RegisterFreeRTOSEvents();

		public:

		/** @brief Default constructor */
		WifiManager();

		/** @brief Connect WIFI as a station */
		void ConnectStation(const string& ssid, const string& password, const int& maxRetry = 0x7FFFFFFF, const bool& autoReconnect = true);

		/** @brief Disconnect WIFI station */
		void DisonnectStation();
	};

}

#endif