#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#ifndef t_dmx_senderH
#define t_dmx_senderH

#include <windows.h>
#include <string>
#include <vector>
#include <boost/lexical_cast.hpp>
#include <Mmsystem.h>
#include "device_ctl.h"
#include "C:\data\Pas_Work\LightFactoryV2_10\universalDMX\universalDMX\udmx_common_intf.h"

class VariLiteUSB_DMX : public UDMX_Common_Intf
{
private:
	HANDLE hInThread;
	HANDLE hOutThread;
	bool kill_in_thread;
	bool kill_out_thread;
	bool in_thread_running;		
	bool out_thread_running;
	static DWORD WINAPI Input_Data_Thread(LPVOID lParam);
	static DWORD WINAPI Send_Data_Thread(LPVOID lParam);
	bool stop_input_thread(int timeout);
	bool stop_send_thread(int timeout);
	int in_universe;
	std::string tmp_dev_ver;

	std::vector<Device_Def> devices;

	int Find_Devices(bool find_only);
	int Close_All_Devices();
	Device_Def & IndexToDevice(int idx);
public:
	VariLiteUSB_DMX();
	~VariLiteUSB_DMX();

	int Startup();
	int Shutdown();

	int DisableOutput();
	int EnableOutput();

	const char * GetInterfaceName();
	int GetPortCount();
	const char * GetPortName(int idx);

	bool IsDeviceNetwork();
	bool SupportsVarFrameRate(int idx);
	bool SupportsDMXReceive(int idx);

	const char * GetPortInfo(int idx);

	int GetPortFrameRate(int idx);
	int SetPortFrameRate(int idx, int fps);

	int GetPortDirection(int idx);
	int SetPortDirection(int idx, int direction);

	const char * GetPortNetworkInterfaceIP(int idx);
	int SetPortNetworkInterfaceIP(int idx, const char * net_interface);
	const char * GetPortNetworkInterfaceBroadcast(int idx);
	int SetPortNetworkInterfaceBroadcast(int idx, const char * net_interface);

	int SendDMXData(int idx, unsigned char * data, int size);
	int GetDMXData(int idx, unsigned char * data);

	int SendROMData(int idx, unsigned char * data, int size);
	int GetROMData(int idx, unsigned char * data);

	bool IsRDM();
	int RDMDiscover(int port_idx, bool full_discovery);
	int RDMClearDeviceList(int port_idx);
	int RDMGetDeviceCount(int port_idx);
	int RDMGetDeviceUID(int port_idx, int idx, BYTE * responce_data);
	int RDMGetParameter(int port_idx, int idx, int sub_idx, int pid, unsigned int * param_length, BYTE * responce_data);
	int RDMSetParameter(int port_idx, int idx, int sub_idx, int pid, unsigned int * param_length, BYTE * responce_data);

	int ShowAdditionalProperties(int idx);
};

#endif