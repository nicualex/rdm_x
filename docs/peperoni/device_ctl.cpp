#include "device_ctl.h"
#include "RDMComs.h"
#include <vector>

using namespace std;

Device_Def::Device_Def()
	:last_received_DMX1(), dmx_buf()
{
	refresh_rate = 44;
	device_version = 0;
	port_count = 1;
	last_send = GetTickCount() + 1000;
	OutputEnabled = true;
	ReceivedDataToSend = false;
	PrevDirection[0] = true;
	PrevDirection[1] = true;
}

Device_Def::~Device_Def()
{

}

void Device_Def::SetAsInput(int p)
{
	if(p<2) is_input[p] = true;
}

void Device_Def::SetAsOutput(int p)
{
	if(p<2) is_input[p] = false;
}

int Device_Def::Set_Params_User(unsigned char * data, int sz)
{
	if (!vusbdmx_eeprom_set(dev_handle, data, sz))
		return -1;
	return sz;
}

int Device_Def::Get_Params_User(unsigned char * data)
{
	if (!vusbdmx_eeprom_get(dev_handle, data, 200))
		return -1;
	return 0;
}

std::string Device_Def::Get_Serial_Number()
{
	if (serial_number == "0") 
		return "A78B238F";
	else if (serial_number == "")
		return "not supported";
	else
		return serial_number;
}

void Device_Def::ReOpenDevice()
{

}

void Device_Def::ReceiveDMX(int b, int p)
{
	
	vusbdmx_rx(dev_handle,	// handle to the interface 
					p,				// universe addressed 
									// on DMX Dongle: only 0 is supported 
					513,			// number of slots to receive, incl. startcode 
					last_received_DMX1[b],	// buffer for the received data 
					0.1f,			// timeout to receive the total frame [s] 
					1e-3f,			// timeout to receive the next slot [s] 
					&slots,			// number of slots received 
					&timestamp,		// timestamp of the frame 
					&status);		// status information 
}

int Device_Def::Send_DMX(int p, unsigned char * data)
{
	if (!vusbdmx_tx(dev_handle, 					// handle to the interface 
					 p,								// universe addressed
													// on DMX Dongle: only 0 is supported 
					 513,							// number of slots to be transmitted, incl. startcode 
					 data,						    // buffer with dmx data (bufnew[0] is the startcode 
					 0,								// configuration for this frame, see usbdmx.h for details 
					 0,								// parameter for configuration [s], see usbdmx.h for details, 
													// in this case: block 100ms with respect to previous frame 
					 200e-6f,						// length of break [s]. If 0, no break is generated 
					 20e-6f,						// length of mark-after-break [s], If 0, no MaB is generated 
				 	 &timestamp,					// timestamp of the frame [ms] 
					 &status))						// status information 
		return -1;

	if (!VUSBDMX_BULK_STATUS_IS_OK(status))
		return -(status);

	return 0;
}

// Send a special frame with 57600 baud and trailing break
// slots is the number of slots to send
// buffer contains the frame to send

int Device_Def::tx_special(int p, int slots, unsigned char * buffer)
{
	if (slots > 513)
		return -1;

	// 1. send frame without break and mab, send data with 57600 baud */
	if (!vusbdmx_tx(dev_handle, p, slots, buffer, 
					VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_SPEED | VUSBDMX_BULK_CONFIG_NORETX, 
					(float)100e-3, (float)0e-6, (float)0e-6, &timestamp, &status))
		return -(status);
	
	/****
	* 2. send 500us break */

	if (!vusbdmx_tx(dev_handle, p, 0, buffer, 
					VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_SPEED | VUSBDMX_BULK_CONFIG_NORETX, 
					(float)100e-3, (float)600e-6, (float)100e-6, &timestamp, &status))
		return -(status);

	return 0;
}

int Device_Def::TxTrigger(int Universe)
{
	UCHAR mTxConfig = VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_NORETX;
	const float mTxTimeout = 0.1f;		// transmission timeout
	const float mTxBreak = 500e-6f;	// break time
	const float mTxMab = 20e-6f;	// MaB time
	unsigned char buffer[2];

	if (!vusbdmx_tx(dev_handle, Universe, 1, buffer, mTxConfig, mTxTimeout, mTxBreak, mTxMab, &timestamp, &status))
		return -(status);

	return 0;
}

// ---------------------------------------------------------------------------------
// RdmGet - GET command
// ---------------------------------------------------------------------------------
int Device_Def::RdmGet(UINT64 uid, USHORT pid, unsigned char * rxbuf, unsigned int * len)
{
	int ret;

	pTx.len = 21;
	pTx.data[RPKT_PIDHI] = pTx.GetHighByte(pid);
	pTx.data[RPKT_PIDLO] = pTx.GetLowByte(pid);
	pTx.data[RPKT_CMDCLS] = E120_GET_COMMAND;
	pTx.data[RPKT_PDL] = (unsigned char)*len;
	pTx.data[RPKT_PORT_ID] = 0x01;
	// Load the transmit buffer
	for (unsigned int idx = 0; idx < *len; idx++)
		pTx.data[RPKT_PDL + idx + 1] = rxbuf[idx];
	// Set the total length
	pTx.len = (ULONG)(21 + *len);

	// set requested uid
	pTx.SetDestinationUID(uid);

	// receive buffer
	std::vector<unsigned char> rxdata(RPKT_MAX_LEN);
	USHORT rxLen = RPKT_MAX_LEN;

	// rms - insert the rxbuf data here
	ret = vusbdmx_rdm(dev_handle, 0, (USHORT)pTx.len, pTx.data, &rxLen, rxdata);

	if (ret < 0)
		return ret;

	if (rxdata[13] == RESPONSE_TYPE_ACK_OVERFLOW) {
		int new_len = 0;
		while (rxdata[13] == RESPONSE_TYPE_ACK_OVERFLOW) {
			std::copy(rxdata.begin() + 21, rxdata.begin() + ret, &rxbuf[new_len]);
			new_len = new_len + ret - 21;
			rxdata.resize(RPKT_MAX_LEN);
			ret = vusbdmx_rdm(dev_handle, 0, (USHORT)pTx.len, pTx.data, &rxLen, rxdata);
		}
		new_len = new_len + ret - 21;
		*len = new_len;
	}
	else {
		*len = ret - 21;
		std::copy(rxdata.begin() + 21, rxdata.begin() + ret, rxbuf);
	}

	return rxdata[13]; // Status result
}

// ---------------------------------------------------------------------------------
// RdmSet - SET command
// ---------------------------------------------------------------------------------
int Device_Def::RdmSet(UINT64 uid, int pid, unsigned char * tx_buf, USHORT tx_len, 
	                                        unsigned char * rx_buf, PUSHORT rx_len)
{
	int ret;
	int idx;
	int size;

	pTx.data[RPKT_PIDHI] = pTx.GetHighByte(pid);
	pTx.data[RPKT_PIDLO] = pTx.GetLowByte(pid);
	pTx.data[RPKT_CMDCLS] = (unsigned char)E120_SET_COMMAND;
	pTx.data[RPKT_PDL] = (unsigned char)tx_len;
	pTx.data[RPKT_PORT_ID] = 0x01;
	if (tx_len > E120_MAX_RDM_DATA_LEN)
		tx_len = E120_MAX_RDM_DATA_LEN;
	// Load the transmit buffer
	for (idx = 0; idx < tx_len; idx++)
		pTx.data[RPKT_PDL + idx + 1] = tx_buf[idx];
	// Set the total length
	pTx.len = (ULONG)(21 + tx_len);

	// set requested uid
	pTx.SetDestinationUID(uid);
	pTx.SetSourceUID(UID_UPDATE_FLASH);

	// Clear the receieve buffer here
	//rx_buf = [];

	if (uid == UID_BROADCAST)
		size = 0;
	else
		size = (int)*rx_len;

	// receive buffer
	std::vector<unsigned char> rxdata(RPKT_MAX_LEN);

	// send frame
	ret = vusbdmx_rdm(dev_handle, 0, (USHORT)pTx.len, pTx.data, rx_len, rxdata);
	// Set the length of the return here
	if (uid == UID_BROADCAST)
	{
		Sleep(25);
		return (0);
	}
	else
	{
		if (ret < 0)
			return(ACK_TIMEOUT);
		*rx_len = (USHORT)ret;
		//if (ret > *rx_len) ret = *rx_len - 1;
		if (rxdata[20] > 0 && rxdata[20] < *rx_len) {
			std::copy(rxdata.begin() + 21, rxdata.begin() + 21 + rxdata[20], rx_buf);
			*rx_len = rxdata[20];
		}
	}

	// Successful return
	return rxdata[13];
}

// ---------------------------------------------------------------------------------
// Unmute send unmute command
// ---------------------------------------------------------------------------------
int Device_Def::Unmute(UINT64 uid)
{
	int ret;

	pTx.len = 21;
	pTx.data[RPKT_PIDHI] = pTx.GetHighByte(E120_DISC_UN_MUTE);
	pTx.data[RPKT_PIDLO] = pTx.GetLowByte(E120_DISC_UN_MUTE);
	pTx.data[RPKT_CMDCLS] = E120_DISCOVERY_COMMAND;
	pTx.data[RPKT_PORT_ID] = 1;
	pTx.data[RPKT_PDL] = 0;

	// set requested uid
	pTx.SetDestinationUID(uid);

	// call rdm (tx and rx routines)
	std::vector<unsigned char> rxdata(RPKT_MAX_LEN);
	USHORT rxLen = RPKT_MAX_LEN;
	ret = vusbdmx_rdm(dev_handle, 0, (USHORT)pTx.len, pTx.data, &rxLen, rxdata);
	if (ret < 0)
		return ret;

	return 0;
}

// ---------------------------------------------------------------------------------
// Mute send mute command
// ---------------------------------------------------------------------------------
int Device_Def::Mute(UINT64 uid)
{
	int ret;

	pTx.len = 21;
	pTx.data[RPKT_PIDHI] = pTx.GetHighByte(E120_DISC_MUTE);
	pTx.data[RPKT_PIDLO] = pTx.GetLowByte(E120_DISC_MUTE);
	pTx.data[RPKT_CMDCLS] = E120_DISCOVERY_COMMAND;
	pTx.data[RPKT_PORT_ID] = 1;
	pTx.data[RPKT_PDL] = 0;

	// set requested uid
	pTx.SetDestinationUID(uid);

	// call rdm (tx and rx routines)
	std::vector<unsigned char> rxdata(RPKT_MAX_LEN);
	USHORT rxLen = RPKT_MAX_LEN;
	ret = vusbdmx_rdm(dev_handle, 0, (char)pTx.len, pTx.data, &rxLen, rxdata);
	if (ret < 0)
		return ret;

	return 0;
}

int Device_Def::RDMGetDeviceCount()
{
	return (int)RDM_Device_List.size();
}

int Device_Def::Clear_RDM_Device_List()
{
	RDM_Device_List.clear();
	return 0;
}

UINT64 Device_Def::Get_RDM_UID_by_Index(int idx)
{
	if (idx < (int)RDM_Device_List.size())
		return RDM_Device_List[idx];
	else
		return 0;
}

// ---------------------------------------------------------------------------------
// DiscoveryRecursive - full binary discovery seach
// ---------------------------------------------------------------------------------
// receive buffer
int Device_Def::DiscoveryRecursive(UINT64 UidMin, UINT64 UidMax)
{
	const int MAX_DEV_SIZE = 18;
	std::vector<unsigned char> rxdevices(MAX_DEV_SIZE);
	int idx;
	int ret;
	int NewDevices;
	UINT64 uid;			// received Uid
	std::string str;

	do
	{
		// send unique branch with given Uid range
		ret = SendUniqueBranch(UidMin, UidMax, rxdevices);
		if (ret == (int)VUSBDMX_RDM_COLLISION)
			goto Collision;

		// other errors?
		if (ret < 0) // transmission failed
			return ret;

		// no errors -> device('s) found
		idx = 0;
		NewDevices = ret / 6;
		while (ret > 0)
		{
			uid = 0;
			uid |= ((UINT64)rxdevices[idx + 0] << 40);
			uid |= ((UINT64)rxdevices[idx + 1] << 32);
			uid |= ((UINT64)rxdevices[idx + 2] << 24);
			uid |= ((UINT64)rxdevices[idx + 3] << 16);
			uid |= ((UINT64)rxdevices[idx + 4] << 8);
			uid |= ((UINT64)rxdevices[idx + 5] << 0);
			idx += 6;
			ret -= 6;

			// test if the device with uid exists
			if (Mute(uid) == 0)
			{
				bool flag = true;

				// Check for duplicates
				for (unsigned int dev = 0; dev < RDM_Device_List.size(); dev++)
				{
					if (uid == RDM_Device_List[dev])
					{
						flag = false;
						break;
					}
				}
				if (flag)
					// Adding to the list 
					RDM_Device_List.push_back(uid);
			}
		}
	} while (NewDevices > 0);
	return (int)RDM_Device_List.size();

Collision:
	{
		UINT64 UidMid = (UidMax + UidMin) / 2;

		// split branch
		if (UidMin <= UidMid)
		{
			ret = DiscoveryRecursive(UidMin, UidMid);
			if (ret < 0)
				return ret;
		}
		if (UidMid < UidMax)
		{
			ret = DiscoveryRecursive(UidMid + 1, UidMax);
			if (ret < 0)
				return ret;
		}

		return (int)RDM_Device_List.size();
	}
}

// ---------------------------------------------------------------------------------
// Send a unique branch message with UIDs set to min and max
// ---------------------------------------------------------------------------------
int Device_Def::SendUniqueBranch(UINT64 uid_min, UINT64 uid_max, std::vector<unsigned char> &rxbuffer)
{
	int ret;
	unsigned int idx;

	pTx.len = 33;
	pTx.data[RPKT_PIDHI] = pTx.GetHighByte(E120_DISC_UNIQUE_BRANCH);
	pTx.data[RPKT_PIDLO] = pTx.GetLowByte(E120_DISC_UNIQUE_BRANCH);
	pTx.data[RPKT_CMDCLS] = E120_DISCOVERY_COMMAND;
	pTx.data[RPKT_PDL] = 12; // 6 bytes for lower UID + 6 bytes for upper UID
	pTx.data[RPKT_PORT_ID] = 1;

	// update Destination to broadcasting ID
	idx = 0;
	SetUid(pTx, E120_BROADCAST_ALL_DEVICES_ID, idx);

	// pack lower and upper UID
	// 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// lower UID
	idx = 21;
	SetUid(pTx, uid_min, idx);
	// 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,	// upper UID
	idx = 27;
	SetUid(pTx, uid_max, idx);

	// send frame
	ret = vusbdmx_rdmdiscovery(dev_handle, 0, (USHORT)pTx.len, pTx.data, rxbuffer);
	if (ret == (int)VUSBDMX_RDM_TX)
		TxTrigger(0);

	return ret;
}

// ---------------------------------------------------------------------------------
// SetUid
// ---------------------------------------------------------------------------------
void Device_Def::SetUid(RdmPacket & pTx, UINT64 Uid, unsigned int pos)
{
	if (pos < pTx.len && pos < RPKT_MAX_LEN - 6)
	{
		pTx.data[pos] = (UCHAR)((Uid >> 40) & 0xff);
		pTx.data[pos + 1] = (UCHAR)((Uid >> 32) & 0xff);
		pTx.data[pos + 2] = (UCHAR)((Uid >> 24) & 0xff);
		pTx.data[pos + 3] = (UCHAR)((Uid >> 16) & 0xff);
		pTx.data[pos + 4] = (UCHAR)((Uid >> 8) & 0xff);
		pTx.data[pos + 5] = (UCHAR)((Uid >> 0) & 0xff);
	}
}

// ---------------------------------------------------------------------------------
// GetUid
// ---------------------------------------------------------------------------------
UINT64 Device_Def::GetUid(RdmPacket pTx, unsigned int pos)
{
	UINT64 Uid;

	Uid = 0UL;
	if (pos < pTx.len && pos < RPKT_MAX_LEN - 6)
	{
		Uid = ((UINT64)(pTx.data[pos]) << 40) & 0xff;
		Uid |= ((UINT64)(pTx.data[pos + 1]) << 32) & 0xff;
		Uid |= ((UINT64)(pTx.data[pos + 2]) << 24) & 0xff;
		Uid |= ((UINT64)(pTx.data[pos + 3]) << 16) & 0xff;
		Uid |= ((UINT64)(pTx.data[pos + 4]) << 8) & 0xff;
		Uid |= ((UINT64)(pTx.data[pos + 5]) << 0) & 0xff;
	}

	return Uid;
}


