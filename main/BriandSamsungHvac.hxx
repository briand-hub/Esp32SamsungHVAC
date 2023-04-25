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

#include "Common.hxx"

namespace Briand {

	enum class SamsungHvacPower {
		UNKNOWN = 	0x00, /* default when not known/set */
		On = 	0xF4,
		Off = 	0xC4
	};

	enum class SamsungHvacMode {
		UNKNOWN = -1, /* default when not known/set */
		Auto = 	0,
		Cool = 	1,
		Dry = 	2,
		Fan = 	3,
		Heat = 	4
	};

	enum class SamsungHvacFanSpeed {
		UNKNOWN = 	0b00000000, /* default when not known/set */
		FanAuto =   0b00100000,
		FanMin =    0b01000000,
		FanMid = 	0b10000000,
		FanMax =    0b10100000
	};

	/** Represent the state (desidered or current)  */
	class SamsungHvacState {
		public:
		Briand::SamsungHvacPower Power;
		Briand::SamsungHvacMode Mode;
		Briand::SamsungHvacFanSpeed Fan;

		/** Temperature in CELSIUS */
		unsigned char Temperature;

		SamsungHvacState();
	};

	/** Manage SamsungHvac with RS485 and message queue */
	class SamsungHvac {		
		/** Wall remote wired address */
		const unsigned char WALL_REMOTE = 0x84;
		
		/** Unit (1) address */
		const unsigned char MACHINE = 0x20;

		/** Message START mark */
		const unsigned char MSG_START = 0x32;

		/** Message END mark */
		const unsigned char MSG_END = 0x34;

		/** Pause mark, after a message with this destination there are 300ms free to send commands */
		const unsigned char DESTINATION_PAUSE_MARK = 0xAD;

		/** Current status (never to be set, will always be refreshed!) */
		unique_ptr<SamsungHvacState> _currentState;

		/** Queue of change status requests */
		unique_ptr<queue<SamsungHvacState>> _stateChangeQueue;

		/** LOG file name */
		string _logFileName;

		/** Mutex for write/read status */
		std::mutex _mutexStatus;

		/** Mutex for write/read queue */
		std::mutex _mutexQueue;

		/** Mutex for log file */
		std::mutex _mutexLog;

		/** UART port */
		uart_port_t _uartPort;

		/** Calculate message checksum. Parameter must be of 13 bytes */
		unsigned char GetMessageChecksum(const unsigned char* message);

		/** Write a log line in log file with \r\n, if any specified. Optional timestamp mark could be added */
		void AddLog(const string& line, const bool& prependTimeStamp = false);

		/** Write a log string in log file, if any specified. */
		void AddLogLine(const string& line);

		public:

		SamsungHvac();
		~SamsungHvac();

		/** 
		 * Initialize serial and data structures. 
		 * @param port UART port number, 0-2 on ESP32. Port 2 by default
		 * @param initrAllocFlags 0 by default
		 * @param txPin 
		 * @param rxPin 
		 * @param rtsPin RequestToSend, connected to ~RE and DE on an RS485 driver  
		 * @param ctsPin CTS is not used in RS485 Half-Duplex Mode
		*/
		void Initialize(
						const uart_port_t& port = UART_NUM_2
						, const int& initrAllocFlags = 0
						, const int& txPin = UART_PIN_NO_CHANGE
						, const int& rxPin = UART_PIN_NO_CHANGE 
						, const int& rtsPin = UART_PIN_NO_CHANGE 
						, const int& ctsPin = UART_PIN_NO_CHANGE 
						, const string& spiffsLogFilePath = ""
					);

		/** Request a status change, appends to the queue */
		void RequestSamsungChangeStatus(const SamsungHvacState& newState);

		/** Returns the current status */
		const SamsungHvacState GetSamsungStatus();

		/** 
		 * Reads from serial and if status message is found then updates the current status.
		 * If possible, sends a change status message from the queue.
		 */
		void DoSerialWork();

		/** String for enum */
		static const char* FanSpeedToString(const Briand::SamsungHvacFanSpeed& speed);

		/** String for enum */
		static const char* ModeToString(const Briand::SamsungHvacMode& mode);

		/** String for enum */
		static const char* PowerToString(const Briand::SamsungHvacPower& power);

		/** 
		 * Returns the SPIFFS log mutex for any operation
		 * @return std::mutex
		*/
		const mutex& GetLogMutex();

		/** 
		 * Returns the SPIFFS log file name for read/write operation. MUST BE USED WITH MUTEX!
		 * @return string with file name (full path)
		*/
		const string& GetLogFileName();
	};

}