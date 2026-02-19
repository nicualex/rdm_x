#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#ifndef t_device_ctlH
#define t_device_ctlH

#include <windows.h>
#include <boost/lexical_cast.hpp>
#include "vusbdmx.h"
#include <vector>

// rms - added to support 32+2 (Checksum) bytes back from the RDM message
#define E120_MAX_RDM_DATA_LEN  255
#define E120_MAX_DEVICE_LABEL_LEN 32
#define E120_MAX_DMX_ADDRESS_LEN 3

/* Protocol version. */
#define E120_PROTOCOL_VERSION  0x0100

/* RDM START CODE (Slot 0)                                                                                                     */
#define E120_SC_RDM  0xCC

/* RDM Protocol Data Structure ID's (Slot 1)                                                                                   */
#define E120_SC_SUB_MESSAGE  0x01

/* Broadcast Device UID's                                                                                                      */
#define E120_BROADCAST_ALL_DEVICES_ID  0xFFFFFFFFFFFF   /* (Broadcast all Manufacturers)                    */

#define E120_SUB_DEVICE_ALL_CALL 0xFFFF

#define RPKT_MAX_LEN 255
#define RPKT_DEST_ID 0
#define RPKT_SOURCE_ID 6
#define RPKT_TRANS_NUM 12
#define RPKT_PORT_ID 13
#define RPKT_MSG_COUNT 14
#define RPKT_SUBDEV1 15
#define RPKT_SUBDEV2 16
#define RPKT_CMDCLS 17
#define RPKT_PIDHI 18
#define RPKT_PIDLO 19
#define RPKT_PDL 20

#define ACK_TIMEOUT 0x0e
#define ACK_TIMEOUT_ZERO 0x1e
#define UID_BROADCAST 0xffffffffffff
#define UID_STRAND 0x736c00000003
#define UID_UPDATE_FLASH 0x010101010101UL

/********************************************************/
/* Table A-1: RDM Command Classes (Slot 20)             */
/********************************************************/
#define E120_DISCOVERY_COMMAND 0x10
#define E120_DISCOVERY_COMMAND_RESPONSE 0x11
#define E120_GET_COMMAND 0x20
#define E120_GET_COMMAND_RESPONSE 0x21
#define E120_SET_COMMAND 0x30
#define E120_SET_COMMAND_RESPONSE 0x31

/********************************************************/
/* Table A-2: Response Type Defines                     */
/********************************************************/
#define RESPONSE_TYPE_ACK          0x00
#define RESPONSE_TYPE_ACK_TIMER    0x01
#define RESPONSE_TYPE_NACK_REASON  0x02 // See Table A - 17
#define RESPONSE_TYPE_ACK_OVERFLOW 0x03 // Additional Response Data available beyond single response length.

/********************************************************/
/* Table A-3: RDM Parameter ID's (Slots 21-22)          */
/********************************************************/
/* Category - Network Management   */
#define E120_DISC_UNIQUE_BRANCH  0x0001
#define E120_DISC_MUTE  0x0002
#define E120_DISC_UN_MUTE  0x0003
#define E120_PROXIED_DEVICES  0x0010
#define E120_PROXIED_DEVICE_COUNT 0x0011
#define E120_COMMS_STATUS  0x0015

/* Category - Status Collection    */
#define E120_QUEUED_MESSAGE  0x0020 /* See Table A-4                                              */
#define E120_STATUS_MESSAGES  0x0030 /* See Table A-4                                              */
#define E120_STATUS_ID_DESCRIPTION  0x0031
#define E120_CLEAR_STATUS_ID  0x0032
#define E120_SUB_DEVICE_STATUS_REPORT_THRESHOLD  0x0033 /* See Table A-4   */

/********************************************************/
/* Table A-4: Status Type Defines                       */
/********************************************************/
#define STATUS_NONE               0x00    //Not allowed for use with GET : QUEUED_MESSAGE
#define STATUS_GET_LAST_MESSAGE   0x01
#define STATUS_ADVISORY           0x02
#define STATUS_WARNING            0x03
#define STATUS_ERROR              0x04
#define STATUS_ADVISORY_CLEARED   0x12
#define STATUS_WARNING_CLEARED    0x13
#define STATUS_ERROR_CLEARED      0x14


class RdmPacket
{
public:
	UINT64 len;
	unsigned char data[RPKT_MAX_LEN];
	RdmPacket()
	{
		int idx;
		len = 21; // By default just a command packet without much data.
		for (idx = 0; idx < RPKT_MAX_LEN; idx++)
			data[0] = 0;

		// destination Uid (set to broadcast 0xffffffffffff)
		SetDestinationUID(UID_BROADCAST);
		// s l 0 0 0 3 - Strand Lighting Source Uid
		SetSourceUID(UID_STRAND);

		data[RPKT_PORT_ID] = 0x01; // port ID
	}

	// Start Methods here
	char GetHighByte(int val)
	{
		return ((char)((val & 0xff00) >> 8));
	}
	char GetLowByte(int val)
	{
		return ((char)(val & 0x00ff));
	}
	void SetSourceUID(UINT64 uid)
	{
		data[RPKT_SOURCE_ID] = (char)((uid >> 40) & 0xff);
		data[RPKT_SOURCE_ID + 1] = (char)((uid >> 32) & 0xff);
		data[RPKT_SOURCE_ID + 2] = (char)((uid >> 24) & 0xff);
		data[RPKT_SOURCE_ID + 3] = (char)((uid >> 16) & 0xff);
		data[RPKT_SOURCE_ID + 4] = (char)((uid >> 8) & 0xff);
		data[RPKT_SOURCE_ID + 5] = (char)(uid & 0xff);
	}
	void SetDestinationUID(UINT64 uid)
	{
		data[RPKT_DEST_ID] = (char)((uid >> 40) & 0xff);
		data[RPKT_DEST_ID + 1] = (char)((uid >> 32) & 0xff);
		data[RPKT_DEST_ID + 2] = (char)((uid >> 24) & 0xff);
		data[RPKT_DEST_ID + 3] = (char)((uid >> 16) & 0xff);
		data[RPKT_DEST_ID + 4] = (char)((uid >> 8) & 0xff);
		data[RPKT_DEST_ID + 5] = (char)(uid & 0xff);
	}
};

class Device_Def
{
private:
	USHORT slots;			/* number of received slots */
	USHORT timestamp;		/* timestamp [ms] returned from ccusbdmx_tx and rx */
	UCHAR  status;			/* status information from ccusbdmx_tx and rx */
	std::vector<UINT64> RDM_Device_List;

	RdmPacket pTx;
	int SendUniqueBranch(UINT64 uid_min, UINT64 uid_max, std::vector<unsigned char> &rxbuffer);
public:
	DWORD last_send;
	bool OutputEnabled;
	bool ReceivedDataToSend;
	bool PrevDirection[2]; // true = output

	unsigned char dmx_buf[2][513];

	unsigned char last_received_DMX1[2][513];

	int device_version;
	bool is_input[2];
	std::string port_name[2];
	std::string port_ver[2];

	HANDLE dev_handle;
	unsigned int port_count;
	std::string serial_number;

	int refresh_rate;

	void SetAsInput(int p);
	void SetAsOutput(int p);
	
	int Set_Params_User(unsigned char * data, int sz);
	int Get_Params_User(unsigned char * data);
	std::string Get_Serial_Number();

	void ReOpenDevice();
	void ReceiveDMX(int d, int p);
	int Send_DMX(int p, unsigned char * data);
	int tx_special(int p, int slots, unsigned char * buffer);

	int TxTrigger(int Universe);

	int RdmGet(UINT64 uid, USHORT pid, unsigned char * rxbuf, unsigned int * len);
	int RdmSet(UINT64 uid, int pid, unsigned char * tx_buf, USHORT tx_len, unsigned char * rx_buf, PUSHORT rx_len);
	int Unmute(UINT64 uid);
	int Mute(UINT64 uid);
	int DiscoveryRecursive(UINT64 UidMin, UINT64 UidMax);
	int RDMGetDeviceCount();
	int Clear_RDM_Device_List();
	UINT64 Get_RDM_UID_by_Index(int idx);

	static void SetUid(RdmPacket & pTx, UINT64 Uid, unsigned int pos);
	static UINT64 GetUid(RdmPacket pTx, unsigned int pos);

	Device_Def();
	~Device_Def();
};

#endif