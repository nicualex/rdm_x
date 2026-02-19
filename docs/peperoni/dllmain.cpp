// dllmain.cpp : Defines the entry point for the DLL application.
// Interface dll for vari*Lite USB to DMX device
// *************************************************
// 

#include "VariLite.h"

VariLiteUSB_DMX * varilite_usb_dmx = NULL;

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
	)
{
	return TRUE;
}

extern "C" __declspec(dllexport) bool Supported(int m_number)
{
	return (m_number == MAGIC_NO);
}

extern "C" __declspec(dllexport) UDMX_Common_Intf * AddReference()
{
	if (varilite_usb_dmx == NULL)
		varilite_usb_dmx = new VariLiteUSB_DMX();
	return varilite_usb_dmx;
}

extern "C" __declspec(dllexport) int ReleaseReference()
{
	delete varilite_usb_dmx;
	varilite_usb_dmx = NULL;
	return 0;
}

