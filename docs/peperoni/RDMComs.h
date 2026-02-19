#pragma once

#include "device_ctl.h"
#include <vector>

#define USHORT  unsigned short

// *********************************************************************************************
// RDM *****************************************************************************************
// *********************************************************************************************

enum VUSBDMX_ERROR
{
	VUSBDMX_RDM_INPUT = -1,	// input parameter wrong
	VUSBDMX_RDM_TX = -10,	// transmission failed
	VUSBDMX_RDM_UNIVERSE = -11,	// universe number wrong
	VUSBDMX_RDM_RX = -20,	// reception failed
	VUSBDMX_RDM_RX_TIMEOUT = -21,	// reception failed with timed out
	VUSBDMX_RDM_RX_FRAMEERROR = -22,	// reception failed with frame error
	VUSBDMX_RDM_RX_NOBREAK = -23,	// reception failed with no break
	VUSBDMX_RDM_RX_LENGTH = -24,	// reception failed with length error (received less data than expected)
	VUSBDMX_RDM_RX_STARTCODE = -25,	// wrong startcode received
	VUSBDMX_RDM_RX_SUBSTARTCODE = -26,	// wrong sub-startcode received
	VUSBDMX_RDM_RX_CHECKSUM = -27,	// reception failed with checksum error
	VUSBDMX_RDM_COLLISION = -30	// collision detected
};

// ----------------------------------------------------------------------------------
// RdmFrame
//
class RdmFrame
{
public:
	const static int MAX_RDM_FRAME_LEN = 255 + 2;
	USHORT slots;
	unsigned char data[257]; // new to MAX_RDM_FRAME_LEN
	RdmFrame()
	{
		int idx;
		slots = 0;
		for (idx = 0; idx < MAX_RDM_FRAME_LEN; idx++)
			data[0] = 0;
	}
};

RdmFrame fTx;
RdmFrame fRx;

// ---------------------------------------------------------------------------------------------
// calcualte RDM checksum
// ---------------------------------------------------------------------------------------------
USHORT CalcXSum(unsigned char * buf, USHORT slots)
{
	USHORT xsum = 0;
	unsigned char * p = buf;
	int idx;
	// sum up data in buf
	xsum = 0;
	idx = 0;
	while (slots-- > 0)
	{
		xsum += p[idx];
		idx++;
	}
	return xsum;
};

// ---------------------------------------------------------------------------------------------
// set checksum in rdm frame
// ---------------------------------------------------------------------------------------------
void SetChecksum(RdmFrame & f)
{
	USHORT xsum;
	USHORT slots = f.slots;
	int idx = slots;

	if (slots < 255)
	{
		// calculate RDM checksum
		xsum = CalcXSum(f.data, slots);
		f.data[idx] = (char)(xsum >> 8);
		f.data[idx + 1] = (char)(xsum & 0xff);
		f.slots = (USHORT)(slots + 2);
	}
};
// ---------------------------------------------------------------------------------------------
// verify checksum in rdm frame
// ---------------------------------------------------------------------------------------------
int VerifyChecksum(RdmFrame f)
{
	USHORT xsum;
	USHORT len = (USHORT)f.data[2];		// length field
	int idx = len;

	// did we received the checksum
	if (f.slots < len + 2)
		// length error not enough data received
		return -1;

	// calculate RDM checksum
	xsum = CalcXSum(f.data, len);
	if ((f.data[len + 0] != (xsum >> 8)) || (f.data[len + 1] != (xsum & 0xff)))
		// checksum wrong
		return -2;

	return 0;
}

//
// RdmFrame
// ----------------------------------------------------------------------------------

#define TxConfig  VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_RX | VUSBDMX_BULK_CONFIG_NORETX
#define TxTimeout  30e-3f   // 0.1f;		// transmission timeout 100e-3
#define TxBreak  300e-6f	// break time 200e-6
#define TxMab  50e-6f	// MaB time
#define RxTimeoutRx  2.5e-3f	// slot-to-slot receive timeout (allowed 0 .. 2.1ms)

// ---------------------------------------------------------------------------------------------
// pack buffer into RDM frame
// ---------------------------------------------------------------------------------------------
void PackRdmFrame(RdmFrame & f, USHORT len, unsigned char * buffer)
{
	int idx;
	// pack data into rdm frame
	f.slots = (USHORT)(3 + len);		// length is startcode, sub-startcode, length, data, checksum
	for (idx = 0; idx < len; idx++)
		f.data[idx + 3] = buffer[idx];

	f.data[0] = 0xcc;		// RDM startcode
	f.data[1] = 0x01;		// sub-startcode
	f.data[2] = (char)(3 + len);	// length (without CRC)
	SetChecksum(f);
};

// ---------------------------------------------------------------------------------------------
// send rdm data
// ---------------------------------------------------------------------------------------------
int tx(HANDLE h, UCHAR u, USHORT len, PUCHAR buffer)
{
	unsigned char status = 1;
	USHORT rxamt;
	USHORT slots;
	USHORT ptime;
	USHORT rxsize;
	float timeout;
	float rxtimeout;

	// test input parameter
	if (h == INVALID_HANDLE_VALUE || len == 0 || buffer == NULL || len > 255 - 3)	// length needs to fit into a single byte
																					// input parameter error
		return ((int)VUSBDMX_RDM_INPUT);

	// pack data into rdm frame
	PackRdmFrame(fTx, len, buffer);

	unsigned char txbuf[267];
	std::copy(fTx.data, fTx.data + 257, txbuf);
	slots = fTx.slots;

	// send the request
	for (auto i = 3; i > 0; --i)
	{
		if (!vusbdmx_tx(h, u, slots, txbuf, TxConfig, TxTimeout, TxBreak, TxMab, &rxamt, &status))
			return (int)VUSBDMX_RDM_TX;
		// if transmission was ok, leave the loop
		if (status == VUSBDMX_BULK_STATUS_OK)
			break;

		// analyse status further
		if (status == VUSBDMX_BULK_STATUS_UNIVERSE_WRONG)
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_UNIVERSE;

		// set no timeout but a small slot-to-slot timeout to terminate the active reception
		rxsize = 257;
		unsigned char rxbuffer[257];
		timeout = 0e-3f;
		rxtimeout = 100e-6f;
		if (!vusbdmx_rx(h, u, rxsize, rxbuffer, timeout, rxtimeout, &slots, &ptime, &status))
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX;
		fRx.slots = slots;
		std::copy(rxbuffer, rxbuffer + rxsize, fRx.data);
	}

	// transmission successful
	return fTx.slots;
};
// ---------------------------------------------------------------------------------------------
// receive rdm data
// ---------------------------------------------------------------------------------------------
static int rx(HANDLE h, UCHAR u, float timeout, bool NeedBreak, std::vector<unsigned char> &rxbuffer)
{
	USHORT slots = 0;
	unsigned char status = 0;
	USHORT ptime;

	// receive the answer
	if (!vusbdmx_rx(h, u, (unsigned short)rxbuffer.size(), (PUCHAR)&rxbuffer[0], timeout, RxTimeoutRx, &slots, &ptime, &status))
	{	// general - unrecoverable - reception error
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX;
	}
	// if reception failed indicate an error
	if (!(status == VUSBDMX_BULK_STATUS_OK))
	{	// something is not ideal - analyse the reason
		if (status == VUSBDMX_BULK_STATUS_TIMEOUT)
			// timeout -> error
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_TIMEOUT;
		if ((status & VUSBDMX_BULK_STATUS_RX_FRAMEERROR) != 0)
			// frame error -> error
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_FRAMEERROR;
		if ((status & VUSBDMX_BULK_STATUS_RX_NO_BREAK) != 0 && NeedBreak)
			// no break received -> error
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_NOBREAK;

		//if ((status & VUSBDMX_BULK_STATUS_RX_OLD_FRAME) != 0)
		// old frame lost -> information only
		//	SetGlobalBuf("WARNING: old frame not read\n");
		//if ((status & VUSBDMX_BULK_STATUS_RX_LENGTH_DECODER) != 0)
		//{	// rdm length information decoded
		//	if (slots != (rxbuffer[2]) + 2)
		//		SetGlobalBuf("WARNING: RDM length incorrectly decoded.\n");
		//}
	}
	return slots;
}

// ---------------------------------------------------------------------------------------------
// analyse standard rdm response
// ---------------------------------------------------------------------------------------------
static int AnalyseStandardResponse(RdmFrame & f, USHORT resultsize, std::vector<unsigned char> &result)
{
	int RxDataLen;	// length of received data

					// test received data
	if (f.slots < 5)
		// length too short -> error
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_LENGTH;
	// test startcodes
	if (f.data[0] != 0xcc)
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_STARTCODE;
	if (f.data[1] != 0x01)
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_SUBSTARTCODE;
	// test if received lenght matches length field
	if (f.data[2] + 2 != f.slots)
		// received length differs from expected
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_LENGTH;

	// verify checksum
	if (VerifyChecksum(f) < 0)
		// checksum error
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_CHECKSUM;

	// extract data
	RxDataLen = (f.data[2] - 3);	// data length = frame length - (startcode + sub-startcode + length field)

									// limit data copied back to buffer size
	int tmp = RxDataLen;
	if (tmp > (int)resultsize)
		tmp = (int)resultsize;

	result.assign(f.data + 3, f.data + 3 + tmp);
	//std::copy(f.data + 3, f.data + 3 + tmp, result);

	return (int)RxDataLen;
}
// ---------------------------------------------------------------------------------------------
// analyse buffer for discovery response signatures
// ---------------------------------------------------------------------------------------------
static int AnalyseDiscoveryResponse(RdmFrame f, USHORT resultsize, std::vector<unsigned char> &result)
{
	USHORT xsum, xsum2;
	unsigned int len = f.slots;			// received length
	int idx, ridx = 0;
	unsigned int rlen = 0;					// result length
	unsigned char calc[256];
	int len_data;

	result[0] = 0;

	idx = 0;
	// loop until all data has been handled
	while (len > 0)
	{
		// a) strip preable
		while (len > 0 && f.data[idx] == 0xfe)
		{
			len--;
			idx++;
		}
		// test length
		if (len <= 0)
			// length to short
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_COLLISION;

		// b) test header delimiator
		if (f.data[idx] != 0xAA)
		{
			// preamble wrong -> nothing received
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_COLLISION;
		}
		// skip deliminator
		idx++;
		len--;

		// c) validate checksum
		if (len < 16)
		{
			// length to short
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_COLLISION;
		}

		len_data = f.slots - idx;
		if (len_data < 12)
			len_data = 12;

		std::copy(f.data + idx, f.data + idx + len_data, calc);
		xsum = (USHORT)CalcXSum(calc, 12);
		xsum2 = (USHORT)(((calc[12] & calc[13]) << 8) + (calc[14] & calc[15]));
		if (xsum != xsum2)
		{	// checksum wrong -> collision
			return (int)VUSBDMX_ERROR::VUSBDMX_RDM_COLLISION;
		}

		// device found
		rlen += 6;
		// does the UID fit into the result buffer?
		if (resultsize >= rlen)
		{
			result[ridx++] = (UCHAR)(f.data[idx + 0] & f.data[idx + 1]);
			result[ridx++] = (UCHAR)(f.data[idx + 2] & f.data[idx + 3]);
			result[ridx++] = (UCHAR)(f.data[idx + 4] & f.data[idx + 5]);
			result[ridx++] = (UCHAR)(f.data[idx + 6] & f.data[idx + 7]);
			result[ridx++] = (UCHAR)(f.data[idx + 8] & f.data[idx + 9]);
			result[ridx++] = (UCHAR)(f.data[idx + 10] & f.data[idx + 11]);
		}
		len -= 16;
	}

	// return size of result
	return (int)rlen;
}

// ---------------------------------------------------------------------------------------------
// ReadAckStatus
// ---------------------------------------------------------------------------------------------
unsigned char ReadAckStatus(UINT64 uid, unsigned char * buf, USHORT len)
{
	// Ignore all cases if broadcasting
	if (uid == UID_BROADCAST)
		return(0);

	if (len < 0) // Nothing returned Timeout
		return(ACK_TIMEOUT);
	else if (buf[20] >= 1) // Is the packet length at least one character returned
		return(buf[21]);
	else
		return(ACK_TIMEOUT_ZERO); // Timeout error with zero packet length
}

// ---------------------------------------------------------------------------------------------
// VUSBDMX_RDM - exchange rdm data
//
// INPUTs:	h				- handle to the device, returned by vusbdmx_open()
//			universe		- addressed universe
//			txlen			- number of bytes to transmitt
//			txbuffer		- data to be transmitted,  !!! sizeof(buffer) >= txlen !!!
//							  START code, Sub START Code and Message Length are inserted befor,
//							  Checksum is appended behind the data.
//          rxsize          - size of receive buffer (set to 0 if not data is to be received)
//          rxbuffer        - receive buffer
//                            Checksum is validated and stipped
//                            START code, Sub START code and length are validated and stipped
// RETURN:	> 0				- length of received data without START codes, length and Checksum, or length of UIDs in rxbuffer
//			< 0				- handling/reception error
// ---------------------------------------------------------------------------------------------
int vusbdmx_rdm(HANDLE h, UCHAR universe, USHORT txlen, PUCHAR txbuffer, PUSHORT rxsize, std::vector<unsigned char> &rxbuffer)
{
	USHORT rx_expectedslots;	    // number of slots expected
	float rx_timeout;			// entire reception timeout
	int slots;
	USHORT rxlen;
	std::string msg;

	std::fill(rxbuffer.begin(), rxbuffer.end(), 0);

	// test input parameter
	if (h == INVALID_HANDLE_VALUE || txlen == 0 || txbuffer == NULL || txlen > 255 - 3)	// length needs to fit into a single byte
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_INPUT;

	// calculate the number of expected slots
	rx_expectedslots = 255;	// maximum for now
							//unsigned char rxbuffer[255];

							// calculate recption timeout - time for transmission + reception time with 100us inter slot time
	rx_timeout = TxBreak + TxMab + (txlen + 3) * 48e-6f + 2e-3f + rx_expectedslots * (44e-6f + 100e-6f);
	// add extra timeout to compensate for larger delay between command and response for some requests
	//            rx_timeout += 100e-3f;

	// send data
	slots = (int)tx(h, universe, txlen, txbuffer);
	if (slots < 0)
		return slots;

	// test if an answer is requested
	if (rxsize <= 0)
		return 0;

	rxlen = rx_expectedslots;
	// receive the answer
	slots = rx(h, universe, rx_timeout, true, rxbuffer);
	if (slots < 0)
		return slots;

	// rmstodo - copy the rxbuffer into a new RdmFrame
	fRx.slots = (USHORT)slots;
	if (255 < slots)
	{
		slots = 255;
		//msg = "vusbdmx_rdm ERROR: Slots= " + slots.ToString() + " != " + rxbuffer.Length.ToString() + " = rxbuffer.Length";
		//Debug.WriteLine(msg);
	}
	std::copy(rxbuffer.begin(), rxbuffer.begin() + slots, fRx.data);
	//std::copy(rxbuffer, rxbuffer + slots, fRx.data);

	// analyse standard response
	return AnalyseStandardResponse(fRx, rxlen, rxbuffer);
};

// ---------------------------------------------------------------------------------------------
// VUSBDMX_RDMDISCOVERY - exchange rdm discovery data (handle special response without break)
//
// INPUTs:	h				- handle to the device, returned by vusbdmx_open()
//			universe		- addressed universe
//			txlen			- number of bytes to transmitt
//			txbuffer		- data to be transmitted,  !!! sizeof(buffer) >= txlen !!!
//							  START code, Sub START Code and Message Length are inserted befor,
//							  Checksum is appended behind the data.
//          rxsize          - size of receive buffer
//          rxbuffer        - receive buffer, contains received UIDs
//          rxlen           - length of received UIDs
// RETURN:	> 0				- length of UIDs in rxbuffer
//			< 0				- handling/reception error
// ---------------------------------------------------------------------------------------------
static int vusbdmx_rdmdiscovery(HANDLE h, UCHAR universe, USHORT txlen, PUCHAR txbuffer, std::vector<unsigned char> &rxbuffer)
{
	int slots;		    // return value from tx() and rx()
	float rxTimeout;	// entire reception timeout
						// test input parameter
	if (h == INVALID_HANDLE_VALUE || txlen == 0 || txbuffer == NULL || txlen > 255 - 3)	// length needs to fit into a single byte-  input parameter error
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_INPUT;

	// calculate recption timeout - standard says min. 5.8ms between discovery and any othere packet
	// wait long enough for transmission to finish and device to responde
	rxTimeout = 10e-3f;

	// send data
	slots = tx(h, universe, txlen, txbuffer);
	if (slots < 0) return slots;

	// resize the buffer
	rxbuffer.resize(RdmFrame::MAX_RDM_FRAME_LEN);

	// receive the answer
	slots = rx(h, universe, rxTimeout, false, rxbuffer);
	// timeout? -> just no device present
	if (slots == (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_TIMEOUT)
		return 0;
	// frameerror? -> just a collision
	if (slots == (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_FRAMEERROR)
	{
		// receive remaining data and leave with collision
		while (rx(h, universe, rxTimeout, false, rxbuffer) == (int)VUSBDMX_ERROR::VUSBDMX_RDM_RX_FRAMEERROR)
			;
		return (int)VUSBDMX_ERROR::VUSBDMX_RDM_COLLISION;
	}
	if (slots < 0)
		return slots;
	
	//            RdmFrame fRx;			// receive and transmit frame
	fRx.slots = (USHORT)slots;
	std::copy(rxbuffer.begin(), rxbuffer.begin() + slots, fRx.data);

	// analyse received data
	return AnalyseDiscoveryResponse(fRx, (USHORT)slots, rxbuffer);
};
