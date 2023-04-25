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

#include "BriandSamsungHvac.hxx"

/** SamsungHvacState class **/

Briand::SamsungHvacState::SamsungHvacState() {
	this->Fan = Briand::SamsungHvacFanSpeed::UNKNOWN;
	this->Mode = Briand::SamsungHvacMode::UNKNOWN;
	this->Power = Briand::SamsungHvacPower::UNKNOWN;
	this->Temperature = 0x00;
}

/** SamsungHvac class **/

Briand::SamsungHvac::SamsungHvac() {
	this->_stateChangeQueue = make_unique<queue<Briand::SamsungHvacState>>();
	this->_currentState = make_unique<Briand::SamsungHvacState>();
	this->_uartPort = UART_NUM_2;
}

Briand::SamsungHvac::~SamsungHvac() {
	// Wait for a mutex
	unique_lock<mutex> locker(this->_mutexStatus);
	this->_currentState.reset();

	// Wait for a mutex
	unique_lock<mutex> locker2(this->_mutexQueue);
	this->_stateChangeQueue.reset();
	
	// Uninstall driver and release UART port
	if (uart_is_driver_installed(this->_uartPort)) {
		uart_flush(this->_uartPort);
		uart_flush_input(this->_uartPort);
		uart_driver_delete(this->_uartPort);
	}
}

void Briand::SamsungHvac::Initialize(
	const uart_port_t& port /* = UART_NUM_2 */
	, const int& initrAllocFlags /* = 0 */
	, const int& txPin /* = UART_PIN_NO_CHANGE */
	, const int& rxPin /* = UART_PIN_NO_CHANGE */
	, const int& rtsPin /* = UART_PIN_NO_CHANGE */
	, const int& ctsPin /* = UART_PIN_NO_CHANGE */
	, const string& spiffsLogFilePath /* = "" */
) {
	this->_logFileName = spiffsLogFilePath;

	// Prepare configuration for Samsung Hvac. 
	// Baud is 2400, 8 bit data, partity even, 1 bit stop
	const uart_config_t uartConfiguration = {
		.baud_rate = 2400,							// Samsung HVAC default RS485 Baud rate
		.data_bits = UART_DATA_8_BITS,				// 8 bit data
		.parity = UART_PARITY_EVEN,					// Parity even
		.stop_bits = UART_STOP_BITS_1,				// 1 stop bit
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,		// took from ESP IDF docs and samples
		.rx_flow_ctrl_thresh = 14,					// should not be necessary with no hw flow control...
		.source_clk = UART_SCLK_APB					// meaning default value
	};

	this->_uartPort = port;

	ESP_LOGI("Briand::SamsungHvac", "Installing UART driver on port %d", port);

	// According to documentation and example RX bufsize is 127*2 (?? with other values is not working) and TX has no buffer
	ESP_ERROR_CHECK(uart_driver_install(port, 127*2, 0, 0, NULL, initrAllocFlags));
	ESP_ERROR_CHECK(uart_param_config(port, &uartConfiguration));

	// PIN Setting
	ESP_ERROR_CHECK(uart_set_pin(port, txPin, rxPin, rtsPin, ctsPin));
	ESP_LOGI("Briand::SamsungHvac", "UART driver installed on port %d with pins (TX,RX,RTS,CTS) = (%d,%d,%d,%d)", port, txPin, rxPin, rtsPin, ctsPin);

	// Set RS485 half duplex mode
    ESP_ERROR_CHECK(uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX));
	ESP_LOGI("Briand::SamsungHvac", "UART%d in RS485 HalfDuplex mode", port);

    // Set read timeout of UART TOUT feature
    ESP_ERROR_CHECK(uart_set_rx_timeout(port, 3)); // 3.5T * 8 = 28 ticks, TOUT=3 -> ~24..33 ticks
	ESP_LOGI("Briand::SamsungHvac", "UART%d with rx timeout set to 3.", port);

	ESP_LOGI("Briand::SamsungHvac", "UART%d READY!", port);
}

const Briand::SamsungHvacState Briand::SamsungHvac::GetSamsungStatus() {
	SamsungHvacState state;

	// Wait for a mutex
	unique_lock<mutex> locker(this->_mutexStatus);

	state.Power = this->_currentState->Power;
	state.Fan = this->_currentState->Fan;
	state.Mode = this->_currentState->Mode;
	state.Temperature = this->_currentState->Temperature;

	return state;
}

void Briand::SamsungHvac::RequestSamsungChangeStatus(const SamsungHvacState& newState) {
	// Wait for a mutex
	unique_lock<mutex> locker(this->_mutexQueue);

	// Add to the queue
	this->_stateChangeQueue->push(newState);

	ESP_LOGI("Briand::SamsungHvac", "New status requested. Queue length is %d", this->_stateChangeQueue->size());
}

unsigned char Briand::SamsungHvac::GetMessageChecksum(const unsigned char* message) {
	unsigned char checksum = 0x00;

	// Checksum is the XOR from bytes 2 to 12.
	// Message is missing START so checksum will be calculated from index 0 to 10 (included)

	for (unsigned char i = 0; i < 11; i++) {
        checksum ^= message[i];
    }

	return checksum;
}

const char* Briand::SamsungHvac::FanSpeedToString(const Briand::SamsungHvacFanSpeed& speed) {
	switch (speed)
	{
		case Briand::SamsungHvacFanSpeed::FanAuto : return "auto";
		case Briand::SamsungHvacFanSpeed::FanMax : return "max";
		case Briand::SamsungHvacFanSpeed::FanMid : return "mid";
		case Briand::SamsungHvacFanSpeed::FanMin : return "min";
		default: return "???";
	}
}

const char* Briand::SamsungHvac::ModeToString(const Briand::SamsungHvacMode& mode) {
	switch (mode)
	{
		case Briand::SamsungHvacMode::Auto : return "auto";
		case Briand::SamsungHvacMode::Cool : return "cool";
		case Briand::SamsungHvacMode::Dry : return "dry";
		case Briand::SamsungHvacMode::Fan : return "fan";
		case Briand::SamsungHvacMode::Heat : return "heat";
		default:  return "???";
	}
}

const char* Briand::SamsungHvac::PowerToString(const Briand::SamsungHvacPower& power) {
	switch (power)
	{
		case Briand::SamsungHvacPower::On : return "on";
		case Briand::SamsungHvacPower::Off : return "off";
		default:  return "???";
	}
}

void Briand::SamsungHvac::DoSerialWork() {
	// annoying log! ESP_LOGI("Briand::SamsungHvac", "Check reading");

	unsigned char message[13] = { 0x00 };
	uart_read_bytes(this->_uartPort, message, 1, 10/portTICK_PERIOD_MS);

	// Read from Serial one byte, if matches a message START (0x32) mark then read other bytes.
	if (message[0] == this->MSG_START) {
		int readedBytes = uart_read_bytes(this->_uartPort, message, 13, 20/portTICK_PERIOD_MS);

		string rawMsg = "(0x) 32 ";
		for (unsigned char i=0; i<readedBytes; i++) {
			char buf[3] = {0};
			rawMsg.append(itoa(message[i], buf, 16));
			rawMsg.append(" ");
		} 

		ESP_LOGI("Briand::SamsungHvac", "[IN MESSAGE]: %s ", rawMsg.c_str());

		if (readedBytes < 13) {
			ESP_LOGI("Briand::SamsungHvac", "[IN MESSAGE] Invalid length of %d bytes", readedBytes);
			return;
		}
		
		/*
			<start> <src> <dst> <cmd> <data: 8 bytes> <chksum> <end>

			Index in message	Byte   Identifier   Comments
			------------------------------------------------------------------------------------------------
			-					1      Start  : start of message (0x32)
			0					2      Src    : Source address
			1					3      Dst    : Destination address
			2					4      Cmd    : Command byte
			3-10				5-12   Data   : Data is always 8 bytes in length, unused bytes will be zero
			11					13     Chksum : Checksum of message which is the XOR of bytes 2-12
			12					14     End    : end of message (0x34)
		*/

		// Check if message is ended correctly
		if (message[12] != this->MSG_END) {
			ESP_LOGW("Briand::SamsungHvac", "[IN MESSAGE] Invalid END");
			return;
		}

		// Check if checksum is valid
		unsigned char inMsgChecksum = this->GetMessageChecksum(message);
		if (message[11] != inMsgChecksum) {
			ESP_LOGW("Briand::SamsungHvac", "[IN MESSAGE] Invalid checksum, expected %02X found %02X", inMsgChecksum, message[11]);
			return;
		} 

		// First, check if is a good moment to send a message. This is when destination is 0xAD
		// (there will be 300ms pause).
		if (message[1] == this->DESTINATION_PAUSE_MARK && !this->_stateChangeQueue->empty()) {
			SamsungHvacState sendState;
			
			// Get current time in ms
			int64_t timeStartMs = (esp_timer_get_time())/1000L;

			{
				// Wait for a mutex
				unique_lock<mutex> locker(this->_mutexQueue);
				sendState = this->_stateChangeQueue->front();
				this->_stateChangeQueue->pop();
			}

			unsigned char message[14];

			message[0] = this->MSG_START;	// Start
			message[1] = this->WALL_REMOTE; // Source
			message[2] = this->MACHINE;		// Destination
			message[3] = 0xA0;				// SET command

			// Data bytes
			message[4] = 0x1F; 				// Swing up/down, 0x1F swing off
			message[5] = 0x18;				// Costant (???) 
			// Fan speed + temperature (converted??)
			message[6] = static_cast<unsigned char>(sendState.Fan) | sendState.Temperature;
			message[7] = static_cast<unsigned char>(sendState.Mode);
			message[8] = static_cast<unsigned char>(sendState.Power);
			message[9] = 0x00;
			message[10] = 0x00;
			message[11] = 0x00;

			message[12] = this->GetMessageChecksum(message + 1); // +1 to exclude START
			message[13] = this->MSG_END; 	// Finish

			// Check if taking mutex requested a long time! We have just 300ms of "space" to send message!
			int64_t timeStopMs = (esp_timer_get_time())/1000L;

			if (timeStopMs - timeStartMs > 180) {
				// Too short, better do nothing
				ESP_LOGI("Briand::SamsungHvac", "Too much time (>150ms) elapsed for OUT MESSAGE mutex. Waiting next cycle!");
				return;
			}
			
			// A little delay helps previous remote command to be sent before "space" command. 
			// This sould overcome to E607 on wall remote.
			vTaskDelay(10/portTICK_PERIOD_MS);

			uart_write_bytes(this->_uartPort, static_cast<const void*>(message), 14);
			// This flush is not for output but for RX buffer! Never do it!
			//uart_flush(this->_uartPort);
			uart_wait_tx_done(this->_uartPort, 300/portTICK_PERIOD_MS);

			int64_t timeSentMessage = (esp_timer_get_time())/1000L;

			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] Waited for tx: %d ms, sent in %d ms", (int)(timeStopMs-timeStartMs), (int)(timeSentMessage-timeStopMs));
			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] POWER: %s", Briand::SamsungHvac::PowerToString(sendState.Power));
			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] TEMPERATURE: %d", sendState.Temperature);
			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] MODE: %s", Briand::SamsungHvac::ModeToString(sendState.Mode));
			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] FAN: %s", Briand::SamsungHvac::FanSpeedToString(sendState.Fan));

			rawMsg = "(0x) ";
			for (unsigned char i=0; i<14; i++) {
				char buf[3] = {0};
				rawMsg.append(itoa(message[i], buf, 16));
				rawMsg.append(" ");
			} 

			ESP_LOGI("Briand::SamsungHvac", "[OUT MESSAGE] %s", rawMsg.c_str());
		}

		// Check if the source is the unit and destination is the remote to gather informations
		if (message[0] == this->MACHINE && message[1] == this->WALL_REMOTE) {

			// Command 0x53 contains mode in last data byte
			if (message[2] == 0x53 && message[10] <= 4 && message[10] != static_cast<unsigned char>(this->_currentState->Mode)) {
				// Wait for a mutex
				unique_lock<mutex> locker(this->_mutexStatus);
				
				this->_currentState->Mode = static_cast<Briand::SamsungHvacMode>(message[10]);
				ESP_LOGI("Briand::SamsungHvac", "[STATUS UPDATE] Mode updated to %s", Briand::SamsungHvac::ModeToString(this->_currentState->Mode));
			}

			// Command 0x52 contains the other interesting infos
			if (message[2] == 0x52) {
				// First data byte contains encoded temperature setting
				// bit 5-0 = setup temperature - 9
				unsigned char temperature = (message[3] & 0x1F) + 9;

				// The 7th bit of data byte 5 contains power on (1) / off (0)
				Briand::SamsungHvacPower powerstate = ((message[7] & 0x80) > 0) ? Briand::SamsungHvacPower::On : Briand::SamsungHvacPower::Off;

				// The 4th byte of data contains fan speed in the first 3 bits (0 to 2)
				Briand::SamsungHvacFanSpeed fans;

				switch(message[6] & 0x0F) {
					case 0x0A:
						fans = Briand::SamsungHvacFanSpeed::FanMin;
						break;
					case 0x0C:
						fans = Briand::SamsungHvacFanSpeed::FanMid;
						break;
					case 0x0D:
						fans = Briand::SamsungHvacFanSpeed::FanMax;
						break;
					default:
						fans = Briand::SamsungHvacFanSpeed::FanAuto;
						break;
				}

				// Wait for a mutex 
				unique_lock<mutex> locker(this->_mutexStatus);
								
				if (this->_currentState->Temperature != temperature) {
					this->_currentState->Temperature = temperature;
					ESP_LOGI("Briand::SamsungHvac", "[STATUS UPDATE] Temperature: %u", (unsigned int)temperature);			
				}

				if (this->_currentState->Power != powerstate) {
					this->_currentState->Power = powerstate;
					ESP_LOGI("Briand::SamsungHvac", "[STATUS UPDATE] Power: %s", Briand::SamsungHvac::PowerToString(this->_currentState->Power));	
				}

				if (this->_currentState->Fan != fans) {
					this->_currentState->Fan = fans;
					ESP_LOGI("Briand::SamsungHvac", "[STATUS UPDATE] Fan speed %s", Briand::SamsungHvac::FanSpeedToString(this->_currentState->Fan));						
				}
			}
		}
	}
}

const mutex& Briand::SamsungHvac::GetLogMutex() {
	return this->_mutexLog;
}

const string& Briand::SamsungHvac::GetLogFileName() {
	return this->_logFileName;
}

void Briand::SamsungHvac::AddLogLine(const string& line) {
	if (this->_logFileName.length() > 0) {
		unique_lock<mutex> lock(this->_mutexLog);
		ofstream logFile;
		logFile.open(this->_logFileName, ios::out | ios::ate);
		time_t now;
		time(&now);
		logFile << "[" << now << "] ";
		logFile << line;
		logFile << "\r\n";
		logFile.flush();
		logFile.close();
	}
}

void Briand::SamsungHvac::AddLog(const string& line, const bool& prependTimeStamp /* = false*/) {
	if (this->_logFileName.length() > 0) {
		unique_lock<mutex> lock(this->_mutexLog);
		ofstream logFile;
		logFile.open(this->_logFileName, ios::out | ios::ate);
		if (prependTimeStamp) {
			time_t now;
			time(&now);
			logFile << "[" << now << "] ";
		} 
		logFile << line;
		logFile.flush();
		logFile.close();
	}
}