// VariLite.cpp : Defines the exported functions for the DLL application.
//
// ***********************************************************************
// ***********************************************************************

#include "VariLite.h"
#include "vusbdmx.h"

using namespace std;

std::atomic<bool> _lock = false;

void lock()
{
	while (true) {
		bool expected = false;
		if (_lock.compare_exchange_strong(expected, true))
			break;
	}
}

void unlock()
{
	_lock.store(false);
}

// *******************************************************************************
// Virtual functions 

int VariLiteUSB_DMX::Startup()
{
	return Find_Devices(false);
}

int VariLiteUSB_DMX::Shutdown()
{
	return Close_All_Devices();
}

int VariLiteUSB_DMX::DisableOutput()
{
	for (unsigned int i = 0; i < devices.size(); ++i) {
		if (devices[i].OutputEnabled) {
			devices[i].OutputEnabled = false;
			devices[i].PrevDirection[0] = !devices[i].is_input[0];
			devices[i].PrevDirection[1] = !devices[i].is_input[1];
			int c = 0;
			for (unsigned int i = 0; i < devices.size(); ++i) {
				for (unsigned int j = 0; j < devices[i].port_count; ++j) {
					SetPortDirection(c, DMX_DIRECTION_IN);
					c++;
				}
			}
		}
	}
	return 0;
}

int VariLiteUSB_DMX::EnableOutput()
{
	for (unsigned int i = 0; i < devices.size(); ++i) {
		if (!devices[i].OutputEnabled) {
			devices[i].OutputEnabled = true;
			int c = 0;
			for (unsigned int i = 0; i < devices.size(); ++i) {
				for (unsigned int j = 0; j < devices[i].port_count; ++j) {
					if (devices[i].PrevDirection[j])
						SetPortDirection(c, DMX_DIRECTION_OUT);
					c++;
				}
			}
		}
	}
	return 0;
}

int VariLiteUSB_DMX::GetPortCount()
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		c += devices[i].port_count;
	}
	return c;
}

const char * VariLiteUSB_DMX::GetInterfaceName()
{
	//return "Philips Entertainment USB-DMX";
	return "Strand/Vari*Lite USB-DMX";
}

const char * VariLiteUSB_DMX::GetPortName(int idx)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				devices[i].port_name[j] = "Device " + boost::lexical_cast<string>((int)i) + ":" + char(j + 65);
				return devices[i].port_name[j].c_str();
			}
			c++;
		}
	}
	return "No device found";
}

bool VariLiteUSB_DMX::IsDeviceNetwork()
{
	return false;
}

bool VariLiteUSB_DMX::SupportsVarFrameRate(int idx)
{
	return true;
}

bool VariLiteUSB_DMX::SupportsDMXReceive(int idx)
{
	return true;
}

const char * VariLiteUSB_DMX::GetPortInfo(int idx)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				std::stringstream stream;
				stream << std::hex << (int)devices[i].device_version;
				devices[i].port_ver[j] = "Version " + stream.str() + ", Serial: " + devices[i].Get_Serial_Number();
				//devices[i].port_ver[j] = "V" + boost::lexical_cast<string>((int)devices[i].device_version) +" SER:" + devices[i].Get_Serial_Number();
				return devices[i].port_ver[j].c_str();
			}
			c++;
		}
	}
	return "error!";
}

int VariLiteUSB_DMX::GetPortFrameRate(int idx)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				return devices[i].refresh_rate;
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::SetPortFrameRate(int idx, int fps)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				devices[i].refresh_rate = fps;
				return 0;
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::GetPortDirection(int idx)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (devices[i].is_input[j]) return DMX_DIRECTION_IN;
				else return DMX_DIRECTION_OUT;
				return 0;
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::SetPortDirection(int idx, int direction)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (!devices[i].OutputEnabled) {
					if (direction == DMX_DIRECTION_IN)
						devices[idx].PrevDirection[j] = false;
					else
						devices[idx].PrevDirection[j] = true;
				}
				else {
					if (direction == DMX_DIRECTION_IN) {
						devices[i].SetAsInput(j);
						if (!in_thread_running) {
							DWORD thrId;
							hInThread = CreateThread(NULL, 0, Input_Data_Thread, this, 0, &thrId);
						}
					}
					else {
						devices[i].SetAsOutput(j);
					}
				}
				return direction;
			}
			c++;
		}
	}
	return -1;
}

const char * VariLiteUSB_DMX::GetPortNetworkInterfaceIP(int idx)
{
	return "";
}

int VariLiteUSB_DMX::SetPortNetworkInterfaceIP(int idx, const char * net_interface)
{
	return 0;
}

const char * VariLiteUSB_DMX::GetPortNetworkInterfaceBroadcast(int idx)
{
	return "";
}

int VariLiteUSB_DMX::SetPortNetworkInterfaceBroadcast(int idx, const char * net_interface)
{
	return 0;
}

int VariLiteUSB_DMX::SendDMXData(int idx, unsigned char * data, int size)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (devices[i].is_input[j]) return -2;
				lock();
				copy(data, data + size, devices[i].dmx_buf[j] + 1);
				devices[i].ReceivedDataToSend = true;
				unlock();
				return 0;
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::GetDMXData(int idx, unsigned char * data)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (!devices[i].is_input[j]) return 0;
				memcpy(data, devices[i].last_received_DMX1[j] + 1, 512);
				return 0;
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::SendROMData(int idx, unsigned char * data, int size)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				return devices[i].Set_Params_User(data, size);
			}
			c++;
		}
	}
	return -1;
}

int VariLiteUSB_DMX::GetROMData(int idx, unsigned char * data)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (j == 0)
					return devices[i].Get_Params_User(data);
			}
			c++;
		}
	}
	return -1;
}

Device_Def & VariLiteUSB_DMX::IndexToDevice(int idx)
{
	int c = 0;
	for (unsigned int i = 0; i < devices.size(); ++i) {
		for (unsigned int j = 0; j < devices[i].port_count; ++j) {
			if (c == idx) {
				if (j == 0)
					return devices[i];
			}
			c++;
		}
	}
	return devices[0];
}

bool VariLiteUSB_DMX::IsRDM()
{
	return true;
}

int VariLiteUSB_DMX::RDMDiscover(int port_idx, bool full_discovery)
{
	Device_Def& dev = IndexToDevice(port_idx);

	dev.OutputEnabled = false;

	dev.TxTrigger(0);

	// send unmute to all 
	if (full_discovery) {
		dev.Clear_RDM_Device_List();
		dev.Unmute(E120_BROADCAST_ALL_DEVICES_ID);
	}

	// start discovery
	int res = dev.DiscoveryRecursive(0UL, 0xffffffffffffUL);

	dev.OutputEnabled = true;

	if (res < 0)
		return res;
	else
		return dev.RDMGetDeviceCount();
}

int VariLiteUSB_DMX::RDMClearDeviceList(int port_idx)
{
	Device_Def& dev = IndexToDevice(port_idx);
	dev.Clear_RDM_Device_List();
	return 0;
}

int VariLiteUSB_DMX::RDMGetDeviceCount(int port_idx)
{
	Device_Def& dev = IndexToDevice(port_idx);
	return dev.RDMGetDeviceCount();
}

int VariLiteUSB_DMX::RDMGetDeviceUID(int port_idx, int idx, BYTE * responce_data)
{
	Device_Def& dev = IndexToDevice(port_idx);
	int count = dev.RDMGetDeviceCount();
	if (idx < count) {
		for (int i = 0; i < 6; i++)
			responce_data[5 - i] = (BYTE)(dev.Get_RDM_UID_by_Index(idx) >> (i * 8));
		return 1;
	}
	else
		return 0;
}

int VariLiteUSB_DMX::RDMGetParameter(int port_idx, int idx, int sub_idx, int pid, unsigned int * param_length, BYTE * responce_data)
{
	Device_Def& dev = IndexToDevice(port_idx);
	//*param_length = 0;
	int ret = dev.RdmGet(dev.Get_RDM_UID_by_Index(idx), pid, responce_data, param_length);
	while (ret == RESPONSE_TYPE_ACK_TIMER) {
		int wait_time = ((responce_data[0] << 8) + responce_data[1]) * 10;
		if (wait_time < 100) wait_time = 100;
		Sleep(wait_time);
		responce_data[0] = STATUS_ERROR;
		*param_length = 1;
		ret = dev.RdmGet(dev.Get_RDM_UID_by_Index(idx), E120_QUEUED_MESSAGE, responce_data, param_length);
	}
	return ret;
}

int VariLiteUSB_DMX::RDMSetParameter(int port_idx, int idx, int sub_idx, int pid, unsigned int * param_length, BYTE * responce_data)
{
	Device_Def& dev = IndexToDevice(port_idx);
	int ret = dev.RdmSet(dev.Get_RDM_UID_by_Index(idx), pid, responce_data, *param_length, responce_data, (PUSHORT)param_length);
	while (ret == RESPONSE_TYPE_ACK_TIMER) {
		int wait_time = ((responce_data[0] << 8) + responce_data[1]) * 10;
		if (wait_time < 100) wait_time = 100;
		Sleep(wait_time);
		responce_data[0] = STATUS_ERROR;
		*param_length = 1;
		ret = dev.RdmGet(dev.Get_RDM_UID_by_Index(idx), E120_QUEUED_MESSAGE, responce_data, param_length);
	}

	return ret;
}

int VariLiteUSB_DMX::ShowAdditionalProperties(int idx)
{
	return 0;
}

// Private RDM routines

// *******************************************************************************

// Constructor
VariLiteUSB_DMX::VariLiteUSB_DMX()
{
	kill_in_thread = false;
	in_thread_running = false;
	kill_out_thread = false;
	out_thread_running = false;
	in_universe = -1;
}

VariLiteUSB_DMX::~VariLiteUSB_DMX()
{
	stop_send_thread(2000);
	stop_input_thread(2000);
}

int VariLiteUSB_DMX::Find_Devices(bool find_only)
{
	Device_Def tmp_device;
	int u = 0;
	devices.clear();

	HANDLE h;				// handle to one interface 
	USHORT version;			// version number in BCD 
	WCHAR  serial[128] = {};     // serial number string read from device 

	// verify USBDMX dll version 
	if (!VUSBDMX_DLL_VERSION_CHECK())
		return 1; // dll version mismatch

	// Look for devices 0 - 9
	for (int i = 0; i<10; ++i) {
		if (vusbdmx_open(i, &h))
		{

			// read product and serial string and device version from device 
			//if (!vusbdmx_product_get(h, product, sizeof(product)))
			//	return 2;
			vusbdmx_serial_number_get(h, serial, sizeof(serial));

			if (!vusbdmx_device_version(h, &version))
				return 4;

			tmp_device.dev_handle = h;
			tmp_device.device_version = version;
			tmp_device.serial_number = "";
			WCHAR * p = serial;
			while (*p) {
				tmp_device.serial_number.push_back((char)*p);
				p++;
			}

			if (vusbdmx_is_rodin1(h))
				tmp_device.port_count = 1;
			//else if (vusbdmx_is_rodin2(h))
			//	tmp_device.port_count = 2;

			devices.push_back(tmp_device);
			++u;

		}
	}

	if (!find_only) {
		for (unsigned int i = 0; i<devices.size(); ++i)
			devices[i].ReOpenDevice();

		for (unsigned int i = 0; i<devices.size(); ++i) {
			devices[i].SetAsOutput(0);
			devices[i].SetAsOutput(1);
		}

		DWORD thrId;
		if (devices.size() > 0) {
			hOutThread = CreateThread(NULL, 0, Send_Data_Thread, this, 0, &thrId);
		}
	}

	return (int)devices.size();
}

int VariLiteUSB_DMX::Close_All_Devices()
{
	stop_send_thread(2000);
	stop_input_thread(2000);
	devices.clear();
	return 0;
}

bool VariLiteUSB_DMX::stop_input_thread(int timeout)
{
	if (in_universe>-1) {
		unsigned int tc = GetTickCount() + timeout;
		kill_in_thread = true;
		while ((in_thread_running) && (tc>GetTickCount()))
			Sleep(20);
	}

	if (in_thread_running)
		TerminateThread(hInThread, 0);

	return in_thread_running;
}

bool VariLiteUSB_DMX::stop_send_thread(int timeout)
{
	unsigned int tc = GetTickCount() + timeout;
	kill_out_thread = true;
	while ((out_thread_running) && (tc>GetTickCount()))
		Sleep(20);

	if (out_thread_running)
		TerminateThread(hOutThread, 0);

	return out_thread_running;
}

DWORD WINAPI VariLiteUSB_DMX::Input_Data_Thread(LPVOID lParam)
{ // Thread to receive dmx data continiously
	VariLiteUSB_DMX *oud = (VariLiteUSB_DMX *)lParam;
	oud->in_thread_running = true;

	oud->kill_in_thread = false; // flag used to stop the thread
	while (!oud->kill_in_thread) { // run until "kill" flag is set
		for (unsigned int i = 0; i<oud->devices.size(); ++i) {
			if (oud->devices[i].port_count == 1) {
				if (oud->devices[i].is_input[0])
					oud->devices[i].ReceiveDMX(0, 0);
			}
			else {
				for (unsigned int j = 0; j<oud->devices[i].port_count; ++j) {
					if (oud->devices[i].is_input[j])
						oud->devices[i].ReceiveDMX(j, 1 - j);
				}
			}
		}
		Sleep(10);
	}

	oud->in_thread_running = false;
	return 0;
}

DWORD WINAPI VariLiteUSB_DMX::Send_Data_Thread(LPVOID lParam)
{ // Thread to send dmx data 
	VariLiteUSB_DMX *oud = (VariLiteUSB_DMX *)lParam;
	oud->out_thread_running = true;
	Sleep(1000);

	oud->kill_out_thread = false; // flag used to stop the thread
	while (!oud->kill_out_thread) { // run until "kill" flag is set

		for (unsigned int i = 0; i<oud->devices.size(); ++i) {
			if (oud->devices[i].last_send < timeGetTime() && oud->devices[i].ReceivedDataToSend) {
				if (oud->devices[i].port_count == 1) {
					if (!oud->devices[i].is_input[0]) {
						lock();
						oud->devices[i].Send_DMX(0, oud->devices[i].dmx_buf[0]);
						unlock();
					}
					oud->devices[i].last_send = timeGetTime() + (1000 / oud->devices[i].refresh_rate);
				}
				else {
					for (unsigned int j = 0; j<oud->devices[i].port_count; ++j) {
						if (!oud->devices[i].is_input[j]) {
							lock();
							oud->devices[i].Send_DMX(1 - j, oud->devices[i].dmx_buf[j]);
							unlock();
						}
						oud->devices[i].last_send = timeGetTime() + (1000 / oud->devices[i].refresh_rate);
					}
				}
			}
		}

		Sleep(1);
	}

	oud->out_thread_running = false;
	return 0;
}
