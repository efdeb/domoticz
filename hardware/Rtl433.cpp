#include "stdafx.h"
#include "hardwaretypes.h"
#include "../main/Logger.h"
#include "../main/RFXtrx.h"
#include "../main/Helper.h"
#include "../main/mainworker.h"
#include "../main/SQLHelper.h"
#include "../main/json_helper.h"

#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include "Rtl433.h"

void removeCharsFromString(std::string& str, const char* charsToRemove) {
	for (unsigned int i = 0; i < strlen(charsToRemove); ++i) {
		str.erase(remove(str.begin(), str.end(), charsToRemove[i]), str.end());
	}
}

CRtl433::CRtl433(const int ID, const std::string& cmdline) :
	m_cmdline(cmdline)
{
	// Basic protection from malicious command line
	removeCharsFromString(m_cmdline, ";/$()`<>|&");
	m_HwdID = ID;
	/*
			#ifdef _DEBUG
				std::string line = "{\"time\" : \"2023-05-17 15:26:52\", \"model\" : \"Flowis\", \"id\" : 240259236, \"type\" : 1, \"volume_m3\" : 759.420, \"device_time\" : \"2055-05-17T15:19:32\", \"alarm\" : 0, \"backflow\" : 64, \"mic\" : \"CRC\", \"mod\" : \"FSK\", \"freq1\" : 867.960, \"freq2\" : 868.050, \"rssi\" : -0.129, \"snr\" : 26.218, \"noise\" : -26.346}";
				if (!ParseJsonLine(line))
				{
					// this is also logged when parsed data is invalid
					Log(LOG_STATUS, "Unhandled sensor reading, please report: (%s)", line.c_str());
				}
			#endif
	*/
}

bool CRtl433::StartHardware()
{
	RequestStart();

	m_thread = std::make_shared<std::thread>([this] { Do_Work(); });
	SetThreadNameInt(m_thread->native_handle());
	m_bIsStarted = true;
	sOnConnected(this);
	StartHeartbeatThread();
	return true;
}

bool CRtl433::StopHardware()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}

	m_bIsStarted = false;
	StopHeartbeatThread();
	return true;
}

bool CRtl433::ParseJsonLine(const std::string& sLine)
{
	std::map<std::string, std::string> _Fields;
	Json::Value root;

	std::string errstr;
	if (ParseJSon(sLine, root, &errstr))
	{
		size_t totFields = root.size();
		for (size_t ii = 0; ii < totFields; ii++)
		{
			if (
				(!root[root.getMemberNames()[ii]].isObject())
				&& (!root[root.getMemberNames()[ii]].isArray())
				)
			{
				std::string vname = root.getMemberNames()[ii];
				std::string vvalue = root[root.getMemberNames()[ii]].asString();
				_Fields[vname] = vvalue;
			}
		}
		return ParseData(_Fields);
	}
	return false;
}

bool CRtl433::FindField(const std::map<std::string, std::string>& data, const std::string& field)
{
	return (data.find(field) != data.end());
}

bool CRtl433::ParseData(std::map<std::string, std::string>& data)
{
	int id = 0;

	bool haveUnit = false;
	int unit = 0;

	bool haveChannel = false;
	int channel = 0;

	bool haveBattery = false;
	int batterylevel = 255;

	bool haveTemp = false;
	float tempC = 0;

	bool haveHumidity = false;
	int humidity = 0;

	bool havePressure = false;
	float pressure = 0;

	bool havePressure_PSI = false;
	float pressure_PSI = 0;

	bool haveRain = false;
	float rain = 0;

	bool haveDepth = false;
	float depth = 0;

	bool haveWind_Speed = false;
	float wind_speed = 0;

	bool haveWind_Gust = false;
	float wind_gust = 0;

	bool haveWind_Dir = false;
	int wind_dir = 0;

	bool haveMoisture = false;
	int moisture = 0;

	bool havePower = false;
	float power = 0;

	bool haveEnergy = false;
	float energy = 0;

	bool haveSequence = false;
	int sequence = 0;

	bool haveUV = false;
	float uvi = 0;

	bool haveLux = false;
	float lux = 0;

	bool haveMeter = false;
	float meter = 0;

	int snr = 12;  // Set to show "-" if no snr is received. rtl_433 uses automatic gain, better to use SNR instead of RSSI to report received RF Signal quality

	int code = 0;

	if (FindField(data, "center_frequency"))
	{
		// Frequency hoping
		return true;
	}

	if (FindField(data, "id"))
	{
		id = stoi(data["id"]);
	}
	if (FindField(data, "unit"))
	{
		unit = stoi(data["unit"]);
		haveUnit = true;
	}
	if (FindField(data, "channel"))
	{
		channel = stoi(data["channel"]);
		haveChannel = true;
	}
	if (FindField(data, "battery_ok"))
	{
		if (data["battery_ok"] == "0") {
			batterylevel = 10;
			haveBattery = true;
		}
		else if (data["battery_ok"] == "1") {
			batterylevel = 100;
			haveBattery = true;
		}
	}
	if (FindField(data, "temperature_C"))
	{
		tempC = stof(data["temperature_C"]);
		haveTemp = true;
	}
	if (FindField(data, "humidity"))
	{
		if (data["humidity"] == "HH") // "HH" and "LL" are specific to WT_GT-02 and WT-GT-03 see issue 1996
		{
			humidity = 90;
			haveHumidity = true;
		}
		else if (data["humidity"] == "LL")
		{
			humidity = 10;
			haveHumidity = true;
		}
		else
		{
			humidity = stoi(data["humidity"]);
			haveHumidity = true;
		}
	}
	if (FindField(data, "pressure_hPa"))
	{
		pressure = stof(data["pressure_hPa"]);
		havePressure = true;
	}
	if (FindField(data, "pressure_PSI"))
	{
		pressure_PSI = stof(data["pressure_PSI"]);
		havePressure_PSI = true;
	}
	if (FindField(data, "pressure_kPa"))
	{
		pressure = stof(data["pressure_kPa"]) * 10.0F; // convert to hPA
		havePressure = true;
	}
	if (FindField(data, "rain_mm"))
	{
		rain = stof(data["rain_mm"]);
		haveRain = true;
	}
	if (FindField(data, "depth_cm"))
	{
		depth = stof(data["depth_cm"]);
		haveDepth = true;
	}
	if (FindField(data, "wind_avg_km_h")) // wind speed average (converting into m/s note that internal storage if 10.0f*m/s) 
	{
		wind_speed = (stof(data["wind_avg_km_h"])) / 3.6F;
		haveWind_Speed = true;
	}
	if (FindField(data, "wind_avg_m_s")) // wind speed average
	{
		wind_speed = stof(data["wind_avg_m_s"]);
		haveWind_Speed = true;
	}
	if (FindField(data, "wind_dir_deg"))
	{
		wind_dir = stoi(data["wind_dir_deg"]); // does domoticz assume it is degree ? (and not rad or something else)
		haveWind_Dir = true;
	}
	if (FindField(data, "wind_max_km_h")) // idem, converting to m/s
	{
		wind_gust = (stof(data["wind_max_km_h"])) / 3.6F;
		haveWind_Gust = true;
	}
	if (FindField(data, "wind_max_m_s"))
	{
		wind_gust = stof(data["wind_max_m_s"]);
		haveWind_Gust = true;
	}
	if (FindField(data, "moisture"))
	{
		moisture = stoi(data["moisture"]);
		haveMoisture = true;
	}
	if (FindField(data, "power_W")) // -- power_W,energy_kWh,radio_clock,sequence,
	{
		power = stof(data["power_W"]);
		havePower = true;
	}
	if (FindField(data, "energy_kWh")) // sensor type general subtype electric counter
	{
		energy = stof(data["energy_kWh"]);
		haveEnergy = true;
	}
	if (FindField(data, "sequence")) // please do not remove : to be added in future PR for data in sensor (for fiability reporting)
	{
		sequence = stoi(data["sequence"]);
		haveSequence = true;
	}
	if (FindField(data, "uv"))
	{
		uvi = stof(data["uv"]);
		haveUV = true;
	}
	if (FindField(data, "light_klx"))
	{
		lux = (stof(data["light_klx"])) * 1000.0F;
		haveLux = true;
	}
	if (FindField(data, "light_lux"))
	{
		lux = stof(data["light_lux"]);
		haveLux = true;
	}
	if (FindField(data, "volume_m3"))
	{
		meter = stof(data["volume_m3"]);
		haveMeter = true;
	}
	if (FindField(data, "snr"))
	{
		/* Map the received Signal to Noise Ratio to the domoticz RSSI 4-bit field that has range of 0-11 (12-15 display '-' in device tab).
		   rtl_433 will not be able to decode a signal with less snr than 4dB or so, why we map snr<5dB to rssi=0 .
		   We use better resolution at low snr. snr=5-10dB map to rssi=1-6, snr=11-20dB map to rssi=6-11, snr>20dB map to rssi=11
		*/
		snr = stoi(data["snr"]) - 4;

		if (snr > 5) snr -= (int)(snr - 5) / 2;
		if (snr > 11) snr = 11; // Domoticz RSSI field can only be 0-11, 12 is used for non-RF received devices
		if (snr < 0) snr = 0; // In case snr actually was below 4 dB
	}
	if (FindField(data, "code"))
	{
		code = stoul(data["code"], nullptr, 16);
	}

	std::string model = data["model"]; // new model format normalized from the 201 different devices presently supported by rtl_433

	bool bDone = false;

	if (FindField(data, "state") || FindField(data, "command"))
	{
		bool bOn = false;
		if (FindField(data, "state"))
			bOn = data["state"] == "ON";
		else if (FindField(data, "command"))
			bOn = data["command"] == "On";
		unsigned int switchidx = (id & 0xfffffff) | ((channel & 0xf) << 28);
		SendSwitch(switchidx,
			(const uint8_t)unit,
			batterylevel,
			bOn,
			0, model, m_Name, snr);
		bDone = true;
	}
	if (FindField(data, "switch1") && FindField(data, "id"))
	{
		std::stringstream sstr;
		sstr << std::hex << data["id"];
		uint32_t idx;
		sstr >> idx;
		for (int iSwitch = 0; iSwitch < 5; iSwitch++)
		{
			char szSwitch[20];
			sprintf(szSwitch, "switch%d", iSwitch + 1);
			if (FindField(data, szSwitch))
			{
				bool bOn = (data[szSwitch] == "CLOSED");
				unsigned int switchidx = ((idx & 0xffffff) << 8) | (iSwitch + 1);
				SendSwitch(switchidx,
					(const uint8_t)unit,
					batterylevel,
					bOn,
					0, model, m_Name, snr);
			}
			bDone = true;
		}
	}

	if (bDone)
		return true;


	unsigned int sensoridx = (id & 0xff) | ((channel & 0xff) << 8);
	if (model == "TFA-Drop") {
		sensoridx = id;
	}

	if (haveTemp && haveHumidity)
	{
		//Check if it is valid
		bool bValidTempHum = !((tempC == 0) && (humidity == 0));
		if (!bValidTempHum)
			return false; //invalid temp+hum
	}

	bool bHandled = false;
	if (haveTemp && haveHumidity && havePressure)
	{
		int iForecast = 0;
		SendTempHumBaroSensor(sensoridx, batterylevel, tempC, humidity, pressure, iForecast, model, snr);
		bHandled = true;
	}
	else if (haveTemp && haveHumidity)
	{
		SendTempHumSensor(sensoridx, batterylevel, tempC, humidity, model, snr);
		bHandled = true;
	}
	else
	{
		if (haveTemp)
		{
			SendTempSensor(sensoridx, batterylevel, tempC, model, snr);
			bHandled = true;
		}
		if (haveHumidity)
		{
			SendHumiditySensor(sensoridx, batterylevel, humidity, model, snr);
			bHandled = true;
		}
	}
	if (haveWind_Speed || haveWind_Gust || haveWind_Dir)
	{
		SendWind(sensoridx, batterylevel, wind_dir, wind_speed, wind_gust, tempC, 0, haveTemp, false, model, snr);
		bHandled = true;
	}
	if (haveRain)
	{
		SendRainSensor(sensoridx, batterylevel, rain, model, snr);
		bHandled = true;
	}
	if (haveDepth)
	{
		SendDistanceSensor(sensoridx, unit, batterylevel, depth, model, snr);
		bHandled = true;
	}
	if (haveMoisture)
	{
		//moisture is in percentage
		if (haveChannel)	// Channel is used to identify each sensor
		{
			SendCustomSensor((uint8_t)sensoridx, (uint8_t)channel, batterylevel, static_cast<float>(moisture), model, "%", snr);
		}
		else
		{
			SendCustomSensor((uint8_t)sensoridx, (uint8_t)unit, batterylevel, static_cast<float>(moisture), model, "%", snr);
		}
		//SendMoistureSensor(sensoridx, batterylevel, moisture, model, snr);
		bHandled = true;
	}
	if (havePower)
	{
		SendWattMeter((uint8_t)sensoridx, (uint8_t)unit, batterylevel, power, model, snr);
		bHandled = true;
	}
	if (havePressure_PSI)
	{
		SendCustomSensor((uint8_t)sensoridx, (uint8_t)unit, batterylevel, pressure_PSI, model, "PSI", snr);
		bHandled = true;
	}
	if (haveEnergy && havePower)
	{
		//can remove this comment : Log(LOG_STATUS, ": CM180 haveSequence(%d) sensoridx(%d) havePower(%d) haveEnergy(%d))", haveSequence, sensoridx, havePower, haveEnergy);
		sensoridx = sensoridx + 1;
		//can rmeove this comment : Log(LOG_STATUS, ": CM180 sensoridx(%d) unit(%d) batterylevel(%d) power(%f) energy(%f) model(%s)", sensoridx, unit, batterylevel, power, energy, model.c_str());
		SendKwhMeter(sensoridx, unit, batterylevel, power, energy, model, snr);
		bHandled = true;
	}
	if (haveUV)
	{
		SendUVSensor((uint8_t)sensoridx, (uint8_t)unit, batterylevel, uvi, model, snr);
		bHandled = true;
	}
	if (haveLux)
	{
		SendLuxSensor((uint8_t)sensoridx, (uint8_t)unit, batterylevel, lux, model);
		bHandled = true;
	}
	if (haveMeter)
	{
		SendMeterSensor((uint8_t)sensoridx, (uint8_t)unit, batterylevel, meter, model);
		bHandled = true;
	}

	if (!strcmp(model.c_str(), "X10-Security"))
	{
		// More X10 sensors can be added if their codes are known
		uint8_t x10_device = 0;
		uint8_t x10_status = 0;

		bHandled = true;

		switch (code & 0xfe) // The last bit is indicating low battery and is already handled
		{
		case 0x00:
			x10_status = sStatusAlarmDelayed; // Door open, Delay switch set to MAX on DS18
			x10_device = sTypeSecX10;
			break;
		case 0x04:
			x10_status = sStatusAlarm;  // Door open, Delay switch set to MIN on DS18
			x10_device = sTypeSecX10;
			break;
		case 0x40:
			x10_status = sStatusAlarmDelayedTamper;
			x10_device = sTypeSecX10;
			break;
		case 0x44:
			x10_status = sStatusAlarmTamper;
			x10_device = sTypeSecX10;
			break;
		case 0x80:
			x10_status = sStatusNormalDelayed;
			x10_device = sTypeSecX10;
			break;
		case 0x84:
			x10_status = sStatusNormal;
			x10_device = sTypeSecX10;
			break;
		case 0xc0:
			x10_status = sStatusNormalDelayedTamper;
			x10_device = sTypeSecX10;
			break;
		case 0xc4:
			x10_status = sStatusNormalTamper;
			x10_device = sTypeSecX10;
			break;
		case 0x8c:
			x10_status = sStatusNoMotion;
			x10_device = sTypeSecX10M;
			break;
		case 0xcc:
			x10_status = sStatusNoMotionTamper;
			x10_device = sTypeSecX10M;
			break;
		case 0x0c:
			x10_status = sStatusMotion;
			x10_device = sTypeSecX10M;
			break;
		case 0x4c:
			x10_status = sStatusMotionTamper;
			x10_device = sTypeSecX10M;
			break;
		case 0x26:
		case 0x88:
		case 0x98:
			x10_status = sStatusPanic;
			x10_device = sTypeSecX10R;
			break;
		case 0x42:
			x10_status = sStatusLightOn;
			x10_device = sTypeSecX10R;
			break;
		case 0x46:
			x10_status = sStatusLight2On;
			x10_device = sTypeSecX10R;
			break;
		case 0xc2:
			x10_status = sStatusLightOff;
			x10_device = sTypeSecX10R;
			break;
		case 0xc6:
			x10_status = sStatusLight2Off;
			x10_device = sTypeSecX10R;
			break;
		case 0x06:
			x10_status = sStatusArmAway;
			x10_device = sTypeSecX10R;
			break;
		case 0x82:
		case 0x86:
			x10_status = sStatusDisarm;
			x10_device = sTypeSecX10R;
			break;
		default:
			bHandled = false;
			break;
		}
		if (bHandled)
			SendSecurity1Sensor(stoul(data["id"], nullptr, 16), x10_device, batterylevel, x10_status, model, m_Name, snr);
	} // End of X10-Security section

	return bHandled; //not handled (Yet!)
}

char* fgetline(FILE* stream, char* line, size_t bufsize)
{
	size_t idx = 0;
	int c;
	while ((c = fgetc(stream)) != EOF && c != '\n')
	{
		if (idx + 2 > bufsize)
		{
			// no more room
			line[0] = 0;
			return nullptr;
		}
		line[idx++] = c;
	}
	if (c == EOF && idx == 0)
	{
		// EOF with no data on last line
		return nullptr;
	}
	line[idx] = '\0';
	return line;
}

void CRtl433::Do_Work()
{
	sleep_milliseconds(1000);
	if (!m_cmdline.empty())
		Log(LOG_STATUS, "Worker started... (Extra Arguments: %s)", m_cmdline.c_str());
	else
		Log(LOG_STATUS, "Worker started...");

	std::string szLastLine;
	FILE* _hPipe = nullptr;

	while (!IsStopRequested(0))
	{
		std::string szFlags = "-F json -M newmodel -C si -M level " + m_cmdline; // newmodel used (-M newmodel) and international system used (-C si) -f 433.92e6 -f 868.24e6 -H 60 -d 0
#ifdef WIN32
		std::string szCommand = "C:\\rtl_433.exe " + szFlags;
		_hPipe = _popen(szCommand.c_str(), "r");
#else
		std::string szCommand = "rtl_433 " + szFlags + " 2>/dev/null";
		_hPipe = popen(szCommand.c_str(), "r");
#endif
		if (_hPipe == nullptr)
		{
			if (!IsStopRequested(0))
			{
				// sleep 30 seconds before retrying
#ifdef WIN32
				Log(LOG_STATUS, "rtl_433 startup failed. Make sure it's properly installed. (%s)  https://cognito.me.uk/computers/rtl_433-windows-binary-32-bit)", szCommand.c_str());
#else
				Log(LOG_STATUS, "rtl_433 startup failed. Make sure it's properly installed (%s). https://github.com/merbanan/rtl_433", szCommand.c_str());
#endif
				for (int i = 0; i < 30; i++)
				{
					if (IsStopRequested(1000))
						break;
				}
			}
			continue;
		}
		if (_hPipe == nullptr)
			return;
#ifndef WIN32
		//Set to non-blocking mode
		int fd = fileno(_hPipe);
		int flags;
		flags = fcntl(fd, F_GETFL, 0);
		flags |= O_NONBLOCK;
		fcntl(fd, F_SETFL, flags);
#endif
		char line[2048];
#define RTL433_USE_fgets
#ifdef RTL433_USE_fgets
		size_t line_offset = 0;
#endif
		while (!IsStopRequested(100))
		{
#ifndef RTL433_USE_fgets
			if (fgetline(_hPipe, (char*)&line, sizeof(line)) != nullptr)
			{
				std::string sLine(line);
				stdreplace(sLine, "\n", "");
				if (sLine != szLastLine)
				{
					szLastLine = sLine;
					if (!ParseJsonLine(sLine))
					{
						// this is also logged when parsed data is invalid
						Log(LOG_STATUS, "Unhandled sensor reading, please report: (%s)", sLine.c_str());
					}
				}
			}
#else
			line[line_offset] = 0;
			if (fgets(line + line_offset, static_cast<int>(sizeof(line) - 1 - line_offset), _hPipe) != nullptr)
			{
				if ((line[strlen(line) - 1] != '\n') && (line[strlen(line) - 1] != '\r'))
				{
					if (line[0] != '{')
					{
						//we do not have a valid line at all
						line_offset = 0;
						continue;
					}
					line_offset += (strlen(line) - 1);
					if (line_offset >= sizeof(line) - 3)
					{
						//buffer out of sync, restart
						line_offset = 0;
					}
					continue;
				}
				std::string sLine(line);
				stdreplace(sLine, "\n", "");
				if (sLine != szLastLine)
				{
					szLastLine = sLine;
					if (!ParseJsonLine(sLine))
					{
						// this is also logged when parsed data is invalid
						Log(LOG_STATUS, "Unhandled sensor reading, please report: (%s)", sLine.c_str());
					}
				}
				line_offset = 0;
			}
			else { //fgets
				if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
					continue;
				}
				break; // bail out, subprocess has failed
			}
#endif
		} // while !IsStopRequested()
		if (_hPipe)
		{
#ifdef WIN32
			_pclose(_hPipe);
#else
			pclose(_hPipe);
#endif
		}
		for (int ii = 0; ii < 10; ii++)
		{
			if (IsStopRequested(1000))
				break;
		}
	} // while !IsStopRequested()
	Log(LOG_STATUS, "Worker stopped...");
}

bool CRtl433::WriteToHardware(const char* /*pdata*/, const unsigned char /*length*/)
{
	//const tRBUF *pSen = reinterpret_cast<const tRBUF*>(pdata);
	return false;
}
