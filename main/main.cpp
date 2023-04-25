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

#include "Common.hxx"
#include "BriandLibEsp32IDF.hxx"
#include "BriandSamsungHvac.hxx"
#include "Settings.hxx"

/* Early declarations */
void SerialReadWriteTask(void* taskArg);
esp_err_t ServeStatus(httpd_req_t* req);
esp_err_t ServeSetStatus(httpd_req_t* req);
esp_err_t ServeRestart(httpd_req_t* req);
void Reboot();

/* Declarations */

const httpd_uri uriStatus {
	.uri = "/",
	.method = HTTP_GET,
	.handler = ServeStatus
};

const httpd_uri uriSetStatus {
	.uri = "/SetStatus",
	.method = HTTP_GET,
	.handler = ServeSetStatus
};

const httpd_uri uriRestart {
	.uri = "/RestartEsp",
	.method = HTTP_GET,
	.handler = ServeRestart
};

httpd_handle_t serverHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;
TaskHandle_t wifiTaskHandle = NULL;
unique_ptr<Briand::SamsungHvac> SamsungHvac = nullptr;
unique_ptr<Briand::WifiManager> wifi = nullptr;

const esp_vfs_spiffs_conf_t spiffsConfiguration = {
	.base_path = "/spiffs",
	.partition_label = NULL,
	.max_files = 5,
	.format_if_mount_failed = true
};

/********************************************/
/*					PROGRAM					*/
/********************************************/

// Required extern "C" for C++ use WITH IDF!
extern "C" void app_main() {
	try {
		// Set BuiltIn LED 
		gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);

		// UNbuffer stdout
		setvbuf(stdout, NULL, _IONBF, 0);

		// Common for error testing
		esp_err_t ret;

		// Initialize the NVS
		ESP_LOGI(LOG_TAG, "Initializing NVS");

		ret = nvs_flash_init();
		if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			ESP_ERROR_CHECK(nvs_flash_erase());
			ret = nvs_flash_init();
		}
		ESP_ERROR_CHECK(ret);
		ESP_LOGI(LOG_TAG, "NVS Initialized.");

		// Initialize SPIFFS
		ESP_LOGI(LOG_TAG, "Initializing SPIFFS");

		// Use settings defined above to initialize and mount SPIFFS filesystem.
		// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
		ret = esp_vfs_spiffs_register(&spiffsConfiguration);
		if (ret == ESP_OK) {
			ESP_LOGI(LOG_TAG, "SPIFFS Initialized.");
		}
		else {
			ESP_LOGE(LOG_TAG, "**** SPIFFS initialization failed! ERROR is: %s", esp_err_to_name(ret));
		}

		// Initialize WiFi handler
		wifi = make_unique<Briand::WifiManager>();
		wifi->ConnectStation(WIFI_ESSID, WIFI_PASSW);

		// Start web server
		ESP_LOGI(LOG_TAG, "Starting WebServer on port %d", WEB_SERVER_PORT);
		httpd_config serverConfig = HTTPD_DEFAULT_CONFIG();
		serverConfig.server_port = WEB_SERVER_PORT;
		serverConfig.stack_size = WEB_SERVER_STACK;
		serverConfig.lru_purge_enable = true;
		ret = httpd_start(&serverHandle, &serverConfig);
		if (ret == ESP_OK) {
			// Set URI handlers
			ESP_LOGI(LOG_TAG, "Registering URI handlers");
			httpd_register_uri_handler(serverHandle, &uriStatus);
			httpd_register_uri_handler(serverHandle, &uriSetStatus);
			httpd_register_uri_handler(serverHandle, &uriRestart);
		}
		else {
			ESP_LOGE(LOG_TAG, "**** Failed to start WebServer! ERROR IS: %s", esp_err_to_name(ret));
		}

		ESP_LOGI(LOG_TAG, "Starting Serial");
		
		SamsungHvac = make_unique<Briand::SamsungHvac>();
		SamsungHvac->Initialize(UART_NUM_2, 0, GPIO_NUM_17, GPIO_NUM_16, GPIO_NUM_18, UART_PIN_NO_CHANGE);
		ESP_LOGI(LOG_TAG, "Serial ready.");

		// Start task for serial read/write with max priority in specific core
		xTaskCreatePinnedToCore(SerialReadWriteTask, "SerialTask", 4096, NULL, 1, &serialTaskHandle, 1);
	} 
	catch(const exception& e) {
		ESP_LOGE(LOG_TAG, "**** Exception caught: %s\n", e.what());
	}

	fflush(stdout);
}

esp_err_t ServeStatus(httpd_req_t* req) {
	httpd_resp_set_type(req, "application/json");
	
	auto currentStatus = SamsungHvac->GetSamsungStatus();
	char conversionBuffer[3];

	unique_ptr<string> json = make_unique<string>();
	json->append("{ ");
	json->append("\"power\": \"");
	json->append(Briand::SamsungHvac::PowerToString(currentStatus.Power));
	json->append("\", \"mode\": \"");
	json->append(Briand::SamsungHvac::ModeToString(currentStatus.Mode));
	json->append("\", \"temp\": ");
	json->append(itoa(currentStatus.Temperature, conversionBuffer, 10));
	json->append(", \"fan\": \"");
	json->append(Briand::SamsungHvac::FanSpeedToString(currentStatus.Fan));

	// some vars that helps in HomeAssistant
	json->append("\", \"ha_status\": \"OK\", \"ha_active\": true");

	json->append(" }");

	return httpd_resp_send(req, json->c_str(), json->length());
}

void SerialReadWriteTask(void* taskArg) {
	while (1) {
		SamsungHvac->DoSerialWork();
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

esp_err_t ServeSetStatus(httpd_req_t* req) {
	httpd_resp_set_hdr(req, "Content-Type", "text/plain");
	httpd_resp_set_status(req, "200 OK");

	// Get current status
	auto newStatus = SamsungHvac->GetSamsungStatus();
	
	// Get query params and change the status
	int qsBufLen = httpd_req_get_url_query_len(req) + 1;
	char qsBuffer[qsBufLen];

	if (httpd_req_get_url_query_str(req, qsBuffer, qsBufLen) != ESP_OK) {
		httpd_resp_set_type(req, "text/plain");
		return httpd_resp_send(req, "No argument received!", strlen("No argument received!"));
	}

	char param[32] = { 0 };

	if (httpd_query_key_value(qsBuffer, "power", param, sizeof(param)) == ESP_OK) {
		if (strcmp(param, "on") == 0) {
			newStatus.Power = Briand::SamsungHvacPower::On; 
			httpd_resp_sendstr_chunk(req, "Power changed to ON\r\n");
		} 
		else if (strcmp(param, "off") == 0) {
			newStatus.Power = Briand::SamsungHvacPower::Off;
			httpd_resp_sendstr_chunk(req, "Power changed to OFF\r\n");
		}
		else {
			httpd_resp_sendstr_chunk(req, "Power INVALID! valid values = on|off but received = ");
			httpd_resp_sendstr_chunk(req, param);
			httpd_resp_sendstr_chunk(req, "\r\n");
		}
	}

	bzero(static_cast<void*>(param), 32);

	if (httpd_query_key_value(qsBuffer, "fan", param, sizeof(param)) == ESP_OK) {
		if (strcmp(param, "auto") == 0) {
			newStatus.Fan = Briand::SamsungHvacFanSpeed::FanAuto;
			httpd_resp_sendstr_chunk(req, "Fan changed to AUTO\r\n");
		} 
		else if (strcmp(param, "max") == 0) {
			newStatus.Fan = Briand::SamsungHvacFanSpeed::FanMax;
			httpd_resp_sendstr_chunk(req, "Fan changed to MAX\r\n");
		} 
		else if (strcmp(param, "mid") == 0) {
			newStatus.Fan = Briand::SamsungHvacFanSpeed::FanMid;
			httpd_resp_sendstr_chunk(req, "Fan changed to MID\r\n");
		} 
		else if (strcmp(param, "min") == 0) {
			newStatus.Fan = Briand::SamsungHvacFanSpeed::FanMin;
			httpd_resp_sendstr_chunk(req, "Fan changed to MIN\r\n");
		} 
		else {
			httpd_resp_sendstr_chunk(req, "Fan INVALID! valid values = auto|max|mid|min but received = ");
			httpd_resp_sendstr_chunk(req, param);
			httpd_resp_sendstr_chunk(req, "\r\n");
		}
	}

	bzero(static_cast<void*>(param), 32);

	if (httpd_query_key_value(qsBuffer, "mode", param, sizeof(param)) == ESP_OK) {
		if (strcmp(param, "auto") == 0) {
			newStatus.Mode = Briand::SamsungHvacMode::Auto;
			httpd_resp_sendstr_chunk(req, "Mode changed to AUTO\r\n");
		} 
		else if (strcmp(param, "cool") == 0) {
			newStatus.Mode = Briand::SamsungHvacMode::Cool;
			httpd_resp_sendstr_chunk(req, "Mode changed to COOL\r\n");
		} 
		else if (strcmp(param, "dry") == 0) {
			newStatus.Mode = Briand::SamsungHvacMode::Dry;
			httpd_resp_sendstr_chunk(req, "Mode changed to DRY\r\n");
		} 
		else if (strcmp(param, "fan") == 0) {
			newStatus.Mode = Briand::SamsungHvacMode::Fan;
			httpd_resp_sendstr_chunk(req, "Mode changed to FAN\r\n");
		} 
		else if (strcmp(param, "heat") == 0) {
			newStatus.Mode = Briand::SamsungHvacMode::Heat;
			httpd_resp_sendstr_chunk(req, "Mode changed to HEAT\r\n");
		} 
		else {
			httpd_resp_sendstr_chunk(req, "Mode INVALID! valid values = auto|cool|dry|heat|fan but received = ");
			httpd_resp_sendstr_chunk(req, param);
			httpd_resp_sendstr_chunk(req, "\r\n");
		}
	}

	bzero(static_cast<void*>(param), 32);

	if (httpd_query_key_value(qsBuffer, "temp", param, sizeof(param)) == ESP_OK) {
		int temp = atoi(param);
		if (temp >= 0 && temp <= 40) {
			newStatus.Temperature = temp;
			httpd_resp_sendstr_chunk(req, "Temperature changed to: ");
			char ntBuf[16] = { 0 };
			itoa(temp, ntBuf, 10);
			httpd_resp_sendstr_chunk(req, ntBuf);
			httpd_resp_sendstr_chunk(req, "\r\n");
		}
		else {
			httpd_resp_sendstr_chunk(req, "Temperature INVALID! valid values = <int> but received = ");
			httpd_resp_sendstr_chunk(req, param);
			httpd_resp_sendstr_chunk(req, "\r\n");
		}
	}
	
	SamsungHvac->RequestSamsungChangeStatus(newStatus);

	//return httpd_resp_send(req, NULL, 0);
	return httpd_resp_sendstr_chunk(req, NULL);
}

esp_err_t ServeRestart(httpd_req_t* req) {
	httpd_resp_set_type(req, "text/plain");
	httpd_resp_send(req, "REBOOTING!", strlen("REBOOTING!"));
	httpd_resp_send(req, NULL, 0);
	Reboot();

	return ESP_OK;
}

void Reboot() {
	ESP_LOGI(LOG_TAG, "\n\n### Restarting ESP.\n\n");
	try {
		ESP_LOGI(LOG_TAG, "### Stopping serial...");
		if (serialTaskHandle != NULL) vTaskDelete(serialTaskHandle);
		SamsungHvac.reset();
		ESP_LOGI(LOG_TAG, "### Stopping wifi...");
		if (wifiTaskHandle != NULL) vTaskDelete(wifiTaskHandle);
		if (wifi != nullptr) wifi->DisonnectStation();
		ESP_LOGI(LOG_TAG, "### Stopping webserver...");
		if (serverHandle != NULL && httpd_stop(serverHandle) != ESP_OK) ESP_LOGW(LOG_TAG, "**** Stopping webserver failed!");
	}
	catch(const exception& e) {
		ESP_LOGE(LOG_TAG, "**** Exception caught while restarting: %s\n", e.what());
	}

	ESP_LOGI(LOG_TAG, "### Restarting in 1 second.");
	fflush(stderr);
	fflush(stdout);
	vTaskDelay(1000 / portTICK_PERIOD_MS);

	esp_restart();
}