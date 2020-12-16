#include "GoodWeCommunicator.h"


GoodWeCommunicator::GoodWeCommunicator(SettingsManager* settingsMan)
{
	settingsManager = settingsMan;
}

void GoodWeCommunicator::start()
{
	auto settings = settingsManager->GetSettings();
	//create the software serial on the custom pins so we can use the hardware serial for debug comms.
	goodweSerial = new SoftwareSerial52();
	//start the software serial with the params (buffersize is larger than default, that's why we cant ue the constructor)	
	goodweSerial->begin(9600, SWSERIAL_8N1, settings->RS485Rx, settings->RS485Tx, false, BufferSize); //inverter fixed baud rate
	//goodweSerial->enableIntTx(false);
	inverters.clear();
	//set the fixed part of our buffer
	headerBuffer[0] = 0xAA;
	headerBuffer[1] = 0x55;
	headerBuffer[2] = GOODWE_COMMS_ADDRES;

	//remove all registered inverters. This is usefull when restarting the ESP. The inverter still thinks it is registered
	//but this program does not know the address. The timeout is 10 minutes.
	for (char cnt = 1; cnt < 255; cnt++)
	{
		sendRemoveRegistration(cnt);
		delay(1);
	}
}

void GoodWeCommunicator::stop()
{
	//clear out our data, stop serial.
	inverters.clear();
}


int GoodWeCommunicator::sendData(char address, char controlCode, char functionCode, char dataLength, char* data)
{
	//need to send out the crc which is the addition of all previous values. Calculate the address first
	int16_t crc = 0;
	
	//send the header first
	headerBuffer[3] = address;
	headerBuffer[4] = controlCode;
	headerBuffer[5] = functionCode;
	headerBuffer[6] = dataLength;
	for (int cnt = 0; cnt < 7; cnt++)
		crc += headerBuffer[cnt];

	for (int cnt = 0; cnt < dataLength; cnt++)
		crc += data[cnt];

	//write out the high and low
	auto high = (crc >> 8) & 0xff;
	auto low = crc & 0xff;


	goodweSerial->write(headerBuffer, 7);
	//check if we need to write the data part and send it.
	if (dataLength)
		goodweSerial->write(data, dataLength);
	
	//now log the message (if needed)
	goodweSerial->write(high);
	goodweSerial->write(low);

	return 7 + dataLength + 2; //header, data, crc
}

void GoodWeCommunicator::sendDiscovery()
{
	sendData(0x7F, 0x00, 0x00, 0x00, nullptr);
}

void GoodWeCommunicator::checkOfflineInverters()
{
	//check inverter timeout
	for (char index = 0; index < inverters.size(); ++index)
	{
		auto newOnline = (millis() - inverters[index].lastSeen) < OFFLINE_TIMEOUT;
		if (inverters[index].isOnline && !newOnline)
		{
			//check if inverter timed out

			sendRemoveRegistration(inverters[index].address); //send in case the inverter thinks we are online
			inverters[index].isOnline = inverters[index].addressConfirmed = false;
		}
		else if (!inverters[index].isOnline && !newOnline) //still offline
		{
			//offline inverter. Reset eday at midnight
			if (inverters[index].eDay > 0 && hour() == 0 && minute() == 0)
				inverters[index].eDay = 0;

			//check for data reset
			if (inverters[index].vac1 > 0 && millis() - inverters[index].lastSeen - OFFLINE_TIMEOUT > settingsManager->GetSettings()->inverterOfflineDataResetTimeout)
			{
				//reset all but eTotal, hTotal and eDay
				inverters[index].fac1 = inverters[index].fac2 = inverters[index].fac3 = inverters[index].gcfiFault =
					inverters[index].iac1 = inverters[index].iac2 = inverters[index].iac3 = inverters[index].ipv1 = inverters[index].ipv2 =
					inverters[index].line1FFault = inverters[index].line1VFault = inverters[index].line2FFault = inverters[index].line2VFault = inverters[index].line3FFault =
					inverters[index].line3VFault = inverters[index].pac = inverters[index].pv1Fault = inverters[index].pv2Fault = inverters[index].vac1 = inverters[index].vac2 =
					inverters[index].vac3 = inverters[index].vpv1 = inverters[index].vpv2 = inverters[index].temp = 0;
			}
		}

		inverters[index].isOnline = newOnline;
	}
}

void GoodWeCommunicator::checkIncomingData()
{
	if (goodweSerial->available())
	{
		while (goodweSerial->available() > 0)
		{
			byte incomingData = goodweSerial->read();

			//wait for packet start. if found read until data length  + data. 
			//set the time we received the data so we can use some kind of timeout
			if (!startPacketReceived && (lastReceivedByte == 0xAA && incomingData == 0x55))
			{
				//packet start received
				startPacketReceived = true;
				lastReceived = millis();
				curReceivePtr = 0;
				numToRead = 0;
				lastReceivedByte = 0x00; //reset last received for next packet
			}
			else if (startPacketReceived)
			{
				if (numToRead > 0 || curReceivePtr < 5)
				{
					inputBuffer[curReceivePtr] = incomingData;
					curReceivePtr++;
					if (curReceivePtr == 5)
					{
						//we received the data langth. keep on reading until data length is read.
						//we need to add two for the crc calculation
						numToRead = inputBuffer[4] + 2;
					}
					else if (curReceivePtr > 5)
						numToRead--;


				}
				if (curReceivePtr >= 5 && numToRead == 0)
				{
					//got the complete packet
					//parse it
					startPacketReceived = false;
					parseIncomingData(curReceivePtr);
				}

			}
			else if (!startPacketReceived)
				lastReceivedByte = incomingData; //keep track of the last incoming byte so we detect the packet start
		}


	}
	if (startPacketReceived && millis() - lastReceived > PACKET_TIMEOUT) // 0.5 sec timoeut
	{
		//there is an open packet timeout. 
		startPacketReceived = false; //wait for start packet again
	}
}
void GoodWeCommunicator::parseIncomingData(char incomingDataLength) //
{
	//first check the crc
	//Data always start without the start bytes of 0xAA 0x55
	//incomingDataLength also has the crc data in it
	int16_t crc = 0xAA + 0x55;
	for (char cnt = 0; cnt < incomingDataLength - 2; cnt++)
		crc += inputBuffer[cnt];

	auto high = (crc >> 8) & 0xff;
	auto low = crc & 0xff;

	//match the crc
	if (!(high == inputBuffer[incomingDataLength - 2] && low == inputBuffer[incomingDataLength - 1]))
		return;

	//check the contorl code and function code to see what to do
	if (inputBuffer[2] == 0x00 && inputBuffer[3] == 0x80)
	{
		if(incomingDataLength > 21) //check if we have enough data
			handleRegistration(inputBuffer + 5, 16); //check length
		//if (incomingDataLength > 20) //check if we have enough byte to call handle registration
			
		//else
		//		debugPrintln("Not enough data for handle registration");
	}
		
	else if (inputBuffer[2] == 0x00 && inputBuffer[3] == 0x81)
		handleRegistrationConfirmation(inputBuffer[0]);
	else if (inputBuffer[2] == 0x01 && inputBuffer[3] == 0x81)
		handleIncomingInformation(inputBuffer[0], inputBuffer[4], inputBuffer + 5);
}

void GoodWeCommunicator::handleRegistration(char* serialNumber, char length)
{
	//check if the serialnumber isn't listed yet. If it is use that one
	//Add the serialnumber, generate an address and send it to the inverter
	if (length != 16)
		return;

	for (char index = 0; index < inverters.size(); ++index)
	{
		//check inverter 
		if (memcmp(inverters[index].serialNumber, serialNumber, 16) == 0)
		{
			//found it. Set to unconfirmed and send out the existing address to the inverter
			inverters[index].addressConfirmed = false;
			inverters[index].lastSeen = millis();
			sendAllocateRegisterAddress(serialNumber,(short) inverters[index].address);
			return;
		}
	}

	//still here. This a new inverter
	GoodWeCommunicator::GoodweInverterInformation newInverter;
	newInverter.addressConfirmed = false;
	newInverter.lastSeen = millis();
	newInverter.isDTSeries = false; //TODO. Determine if DT series inverter by getting info
	memset(newInverter.serialNumber, 0, 17);
	memcpy(newInverter.serialNumber, serialNumber, 16);
	//get the new address. Add one (overflows at 255) and check if not in use
	lastUsedAddress++;
	while (getInverterInfoByAddress(lastUsedAddress) != nullptr)
		lastUsedAddress++;
	newInverter.address = lastUsedAddress;
	inverters.push_back(newInverter);

	sendAllocateRegisterAddress(serialNumber, lastUsedAddress);
}

void GoodWeCommunicator::handleRegistrationConfirmation(char address)
{
	//lookup the inverter and set it to confirmed
	auto inverter = getInverterInfoByAddress(address);
	if (inverter)
	{
		inverter->addressConfirmed = true;
		inverter->isOnline = false; //inverter is online, but we first need to get its information
		inverter->lastSeen = millis();
	}
	//get the information straight away
	askInverterForInformation(address);
}

void GoodWeCommunicator::handleIncomingInformation(char address, char dataLength, char* data)
{
	//need to parse the information and update our struct
	//parse all pairs of two bytes and output them
	auto inverter = getInverterInfoByAddress(address);
	if (inverter == nullptr) return;

	if (dataLength < 44) //minimum for non dt series
		return;

	//data from iniverter, means online
	inverter->lastSeen = millis();
	char dtPtr = 0;
	inverter->vpv1 = bytesToFloat(data, 10);					dtPtr += 2;
	inverter->vpv2 = bytesToFloat(data + dtPtr, 10);				dtPtr += 2;
	inverter->ipv1 = bytesToFloat(data + dtPtr, 10);			dtPtr += 2;
	inverter->ipv2 = bytesToFloat(data + dtPtr, 10);			dtPtr += 2;
	inverter->vac1 = bytesToFloat(data + dtPtr, 10);			dtPtr += 2;
	if (inverter->isDTSeries)
	{
		inverter->vac2 = bytesToFloat(data + dtPtr, 10);		dtPtr += 2;
		inverter->vac3 = bytesToFloat(data + dtPtr, 10);		dtPtr += 2;
	}
	inverter->iac1 = bytesToFloat(data + dtPtr, 10);			dtPtr += 2;
	if (inverter->isDTSeries)
	{
		inverter->iac2 = bytesToFloat(data + dtPtr, 10);		dtPtr += 2;
		inverter->iac3 = bytesToFloat(data + dtPtr, 10);		dtPtr += 2;
	}
	inverter->fac1 = bytesToFloat(data + dtPtr, 100);			dtPtr += 2;
	if (inverter->isDTSeries)
	{
		inverter->fac2 = bytesToFloat(data + dtPtr, 100);		dtPtr += 2;
		inverter->fac3 = bytesToFloat(data + dtPtr, 100);		dtPtr += 2;
	}
	inverter->pac = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr + 1]);			dtPtr += 2;
	inverter->workMode = ((unsigned short)(data[dtPtr]) << 8) | (data[dtPtr + 1]);	dtPtr += 2;
	//TODO: Get the other values too
	inverter->temp = bytesToFloat(data + dtPtr, 10);		dtPtr += inverter->isDTSeries ? 34 : 26;
	inverter->eDay = bytesToFloat(data + dtPtr, 10);
	//isonline is set after first batch of data is set so readers get actual data 
	//inverter->isOnline = true;
}

float GoodWeCommunicator::bytesToFloat(char* bt, char factor)
{
	//convert two byte to float by converting to short and then dividing it by factor
	return float(((unsigned short)bt[0] << 8) | bt[1]) / factor;
}

void GoodWeCommunicator::askAllInvertersForInformation()
{
	for (char index = 0; index < inverters.size(); ++index)
	{
		if (inverters[index].addressConfirmed && inverters[index].isOnline)
			askInverterForInformation(inverters[index].address);

		yield();
	}
}

void GoodWeCommunicator::askInverterForInformation(char address)
{
	sendData(address, 0x01, 0x01, 0, nullptr);
}

GoodWeCommunicator::GoodweInverterInformation* GoodWeCommunicator::getInverterInfoByAddress(char address)
{
	for (char index = 0; index < inverters.size(); ++index)
	{
		//check inverter 
		if (inverters[index].address == address)
			return &inverters[index];
	}
	return nullptr;
}

void GoodWeCommunicator::sendAllocateRegisterAddress(char* serialNumber, char address)
{
	//create our registrationpacket with serialnumber and address and send it over
	char RegisterData[17];
	memcpy(RegisterData, serialNumber, 16);
	RegisterData[16] = (short)address;
	//need to send alloc msg
	sendData(0x7F, 0x00, 0x01, 17, RegisterData);
}

void GoodWeCommunicator::sendRemoveRegistration(char address)
{
	//send out the remove address to the inverter. If the inverter is still connected it will reconnect after discovery
	sendData(address, 0x00, 0x02, 0, nullptr);
}

void GoodWeCommunicator::handle()
{
	//always check for incoming data
	checkIncomingData();

	//check for offline inverters
	checkOfflineInverters();

	//discovery every 10 secs.
	if (millis() - lastDiscoverySent >= (inverters.size() ? DISCOVERY_WITH_ACTIVE_INVERTERS_INTERVAL : DISCOVERY_NO_INVERTERS_INTERVAL))
	{
		sendDiscovery();
		lastDiscoverySent = millis();
	}
	else if (millis() - lastInfoUpdateSent >= INFO_INTERVAL)
	{
		askAllInvertersForInformation();
		lastInfoUpdateSent = millis();
	}
	checkIncomingData(); //check again
}


std::vector<GoodWeCommunicator::GoodweInverterInformation> GoodWeCommunicator::getInvertersInfo()
{
	return inverters;
}

GoodWeCommunicator::~GoodWeCommunicator()
{
}
