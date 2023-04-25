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

#include "BriandLibEsp32IDF.hxx"

using namespace std;

// ESP SYSTEM

// Static vars definition

esp_chip_info_t Briand::Esp32System::chip_info;
uint32_t Briand::Esp32System::flash_size = 0;
uint32_t Briand::Esp32System::heap_size = 0;
uint32_t Briand::Esp32System::heap_free = 0;
uint32_t Briand::Esp32System::psram_size = 0;
uint32_t Briand::Esp32System::psram_free = 0;
bool Briand::Esp32System::has_psram = false;

// Methods definition

void Briand::Esp32System::CollectInfo() {
	esp_chip_info(&Briand::Esp32System::chip_info);

	if (esp_flash_get_size(NULL, &Briand::Esp32System::flash_size) != ESP_OK) Briand::Esp32System::flash_size = 0;

	#ifdef CONFIG_SPIRAM
		Briand::Esp32System::psram_size = esp_get_free_heap_size();
		Briand::Esp32System::psram_free = esp_get_free_heap_size();
		Briand::Esp32System::has_psram = psram_size > 0;
	#else
		Briand::Esp32System::has_psram = false;
		Briand::Esp32System::psram_free = 0;
		Briand::Esp32System::psram_size = 0;
	#endif

	Briand::Esp32System::heap_free = esp_get_free_heap_size();
	Briand::Esp32System::heap_size = heap_caps_get_total_size(MALLOC_CAP_8BIT);
}

void Briand::Esp32System::PrintInfo() {

	Briand::Esp32System::CollectInfo();

	printf("System info -----------");

	printf("CHIP INFO: %s chip with %d CPU core(s), WiFi%s%s%s\n",
           CONFIG_IDF_TARGET,
           Briand::Esp32System::chip_info.cores,
           (Briand::Esp32System::chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (Briand::Esp32System::chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           (Briand::Esp32System::chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : ""
	);

	printf("FLASH MEMORY: %lu MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (Briand::Esp32System::chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external"
	);

	printf("HEAP SIZE: %lu bytes\n", Briand::Esp32System::heap_size);
	printf("HEAP FREE: %lu bytes\n", Briand::Esp32System::heap_free);
	printf("HAS PSRAM: %s\n", Briand::Esp32System::has_psram ? "yes" : "no");
	printf("PSARM SIZE: %lu bytes\n", Briand::Esp32System::psram_size);
	printf("PSRAM FREE: %lu bytes\n", Briand::Esp32System::psram_free);

	printf("-----------------------");
}

// -------------------------------------------------------------------------------------
// ESP WIFI

// Static vars definition

bool Briand::WifiManager::EspNetIfEventsDone = false;
bool Briand::WifiManager::EspWifiInitialized = false;
bool Briand::WifiManager::EventHandlerRegistered = false;
bool Briand::WifiManager::StaStarted = false;
bool Briand::WifiManager::AutoReconnect = false;
esp_netif_t* Briand::WifiManager::InterfaceSTA = NULL;
esp_netif_t* Briand::WifiManager::InterfaceAP = NULL;
wifi_config_t Briand::WifiManager::WifiConfiguration;
int Briand::WifiManager::WifiStaRemainingTentatives = 0;
EventGroupHandle_t Briand::WifiManager::FreeRTOSEventGroup_STA;
EventGroupHandle_t Briand::WifiManager::FreeRTOSEventGroup_AP;
const char* Briand::WifiManager::TAG = "Briand::WifiManager";

// Methods definition

Briand::WifiManager::WifiManager() {
	ESP_LOGV(WifiManager::TAG, "Constructor called");
}

void Briand::WifiManager::ConnectStation(const string& ssid, const string& password, const int& maxRetry /*= 0x7FFFFFFF*/, const bool& autoReconnect /*= true*/) {
	ESP_LOGV(WifiManager::TAG, "ConnectStation called");

	// Set auto-reconnect parameter
	WifiManager::AutoReconnect = autoReconnect;

	esp_err_t err;

	// Initialize NVS
	err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// try to erase and reinit
    	ESP_ERROR_CHECK(nvs_flash_erase());
    	err = nvs_flash_init();
    }

	ESP_ERROR_CHECK(err);

	// Initialize event group for STA
	WifiManager::FreeRTOSEventGroup_STA = xEventGroupCreate();

	// If not done before, init networking interface
	if (!WifiManager::EspNetIfEventsDone) {
		// Init network interface, events loop
		ESP_LOGI(WifiManager::TAG, "NETIF initializing");

    	ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());

		WifiManager::EspNetIfEventsDone = true;
		ESP_LOGI(WifiManager::TAG, "NETIF initialized");
	}

	// If not done before, create STA interface
	if (WifiManager::InterfaceSTA == NULL) {
		ESP_LOGI(WifiManager::TAG, "NETIF Creating default Wifi STA");
		WifiManager::InterfaceSTA = esp_netif_create_default_wifi_sta();
		ESP_LOGI(WifiManager::TAG, "NETIF Created default Wifi STA");
	} 

	// If not done before, take default wifi configuration and use it
    if (!WifiManager::EspWifiInitialized) {
		ESP_LOGI(WifiManager::TAG, "Initializing ESP WIFI configuration");
		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
		ESP_LOGI(WifiManager::TAG, "Initialized ESP WIFI configuration");

		WifiManager::EspWifiInitialized = true;
	}

	// Attach event manager for events to be caught (if not done before, method will check)
	this->RegisterFreeRTOSEvents();

	// Set station essid/password and authentication mode

	ESP_LOGI(WifiManager::TAG, "Configuring ESP WIFI ssid/password/authmode");

	strcpy(reinterpret_cast<char*>(WifiManager::WifiConfiguration.sta.ssid), ssid.c_str());
	strcpy(reinterpret_cast<char*>(WifiManager::WifiConfiguration.sta.password), password.c_str());
	
	/* 
		Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
		If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
		to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
		WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
	*/

	WifiManager::WifiConfiguration.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	WifiManager::WifiConfiguration.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	ESP_LOGI(WifiManager::TAG, "Configured ESP WIFI ssid/password/authmode");

	// Get current mode, if not STA *add* STA mode
	wifi_mode_t currentMode;
	ESP_ERROR_CHECK(esp_wifi_get_mode(&currentMode));
	if (currentMode == WIFI_MODE_AP) {
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
		ESP_LOGI(WifiManager::TAG, "AP+STA-Only mode set");
	}
	else if (currentMode != WIFI_MODE_STA && currentMode != WIFI_MODE_APSTA) {
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_LOGI(WifiManager::TAG, "STA-Only mode set");
	}
	else {
		ESP_LOGI(WifiManager::TAG, "No mode to be set (current is %d)", currentMode);
	}

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &WifiManager::WifiConfiguration));

	// If this is the first initialization, sta must be started. But if the
	// event queue has been done once, a connect is enough!
	if (WifiManager::StaStarted) {
		// Just connect.
		ESP_LOGI(TAG, "Wifi connecting");
		ESP_ERROR_CHECK(esp_wifi_connect());
	}
	else {
		// Must start wifi, then in event handler the connect will be called when done
		ESP_ERROR_CHECK(esp_wifi_start());
		ESP_LOGI(TAG, "Wifi started");
	}

	// Reset tentatives
	WifiManager::WifiStaRemainingTentatives = maxRetry;

    /* 
		Waiting until either the connection is established (BIT1) or connection failed for the maximum
    	number of re-tries (BIT0). The bits are set by event_handler() (see above) 
	*/

    EventBits_t bits = xEventGroupWaitBits(WifiManager::FreeRTOSEventGroup_STA,
            BIT1 | BIT0,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /*
		xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     	happened. 
	*/

    if (bits & BIT1) ESP_LOGI(WifiManager::TAG, "Wifi connected to ap SSID:%s password:%s", ssid.c_str(), password.c_str());
    else if (bits & BIT0) ESP_LOGI(WifiManager::TAG, "Wifi FAILED connected to ap SSID:%s password:%s", ssid.c_str(), password.c_str());
    else ESP_LOGE(TAG, "UNEXPECTED EVENT: %lu", bits);
}

void Briand::WifiManager::DisonnectStation() {
	ESP_LOGV(WifiManager::TAG, "DisonnectStation called");

	ESP_LOGI(WifiManager::TAG, "Disconnecting station");

	// This will grant no other connect will be fired again in event handler.
	WifiManager::WifiStaRemainingTentatives = 0;

	ESP_ERROR_CHECK(esp_wifi_disconnect());
	ESP_LOGI(WifiManager::TAG, "Station disconnected!");
}

void Briand::WifiManager::RegisterFreeRTOSEvents() {
	
	// If not registered yet...
	
	if (!WifiManager::EventHandlerRegistered) {
		ESP_LOGI(WifiManager::TAG, "Registering FreeRTOS events");

		esp_event_handler_instance_t instance_any_id;
		esp_event_handler_instance_t instance_got_ip;

		// Any ID related to WIFI will be handled by method
		ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
															ESP_EVENT_ANY_ID,
															&WifiManager::EventHandler,
															NULL,
															&instance_any_id));

		// IP Event matching STATION got IP will be handled
		ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
															IP_EVENT_STA_GOT_IP,
															&WifiManager::EventHandler,
															NULL,
															&instance_got_ip));

		// TODO: sta lost ip, etc.

		// TODO: ap started, station connected ....

		ESP_LOGI(WifiManager::TAG, "Registered FreeRTOS events");

		WifiManager::EventHandlerRegistered = true;
	}
}

void Briand::WifiManager::EventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	ESP_LOGV(WifiManager::TAG, "EventHandler called");

	// All registered events will arrive here!

	if (!WifiManager::StaStarted && event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		ESP_LOGI(WifiManager::TAG, "Caught event WIFI_EVENT / WIFI_EVENT_STA_START");

		ESP_LOGI(TAG, "Wifi started");

		// If STA has been started after initialization, start connect to essid
		ESP_LOGI(TAG, "Wifi connecting");
		esp_wifi_connect();

		// This should not be done there after the first initialization!
		WifiManager::StaStarted = true;
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		ESP_LOGI(WifiManager::TAG, "Caught event WIFI_EVENT / WIFI_EVENT_STA_DISCONNECTED");

		// If there are no remaining tentatives available, return with failure.
		// But this point could also be reached when a Disconnect is called or when
		// connection is lost.
		// The autoreconnect parameter will do the trick: if true, when connected, remaining tentatives will
		// be high number, so if wifi has problems or ip is lost will auto-reconnect.
		// If autoreconnect parameter is false this will not happen because tenetatives will be zero. 

		if (WifiManager::WifiStaRemainingTentatives > 0) {
			// retry connection
			esp_wifi_connect();
			WifiManager::WifiStaRemainingTentatives--;
			ESP_LOGI(WifiManager::TAG, "Retry to connect to the AP");
		} 
		else {
			// Mark a fail bit in order to stop tentatives
			xEventGroupSetBits(WifiManager::FreeRTOSEventGroup_STA, BIT0);
			ESP_LOGI(WifiManager::TAG, "Connection failed.");
		}
	} 
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ESP_LOGI(WifiManager::TAG, "Caught event IP_EVENT / IP_EVENT_STA_GOT_IP");

		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(WifiManager::TAG, "Wifi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));

		// If auto-reconnect is true, set tentatives to high number in order to
		// handle reconnection in the right way (see comment above on disconnect event)

		WifiManager::WifiStaRemainingTentatives = WifiManager::AutoReconnect ? 0x7FFFFFFF : 0x00;
		
		// Mark success bit
		xEventGroupSetBits(WifiManager::FreeRTOSEventGroup_STA, BIT1);
	}
}




