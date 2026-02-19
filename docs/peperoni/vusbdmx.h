/*
 * VUSBDMX.H -- include file for Peperoni-Light's VUSBDMX.DLL
 *   to communicate with the Rodin1, Rodin2, VUSBDMX X-Switch 
 *   and VUSBDMX21 usb to dmx512 interfaces.
 *
 * Copyright (C) 2004-2006 by Jan Menzel
 * All rights reserved 
 */

#ifndef VUSBDMX_H
#define VUSBDMX_H

// define the dll space this file is used in
#ifdef VUSBDMX_DLL_EXPORT
#define VUSBDMX_TYPE __declspec(dllexport) __stdcall
#else
#define VUSBDMX_TYPE __declspec(dllimport) __stdcall
#ifdef _WIN64
#pragma comment(lib, "vusbdmx64")
#else
#pragma comment(lib, "vusbdmx")
#endif
#endif

/*
 * DLL version
 */
static USHORT VUSBDMX_DLL_VERSION = 0x0404;

/*
 * MACRO to verify dll version
 */
#define VUSBDMX_DLL_VERSION_CHECK() (vusbdmx_version() >= VUSBDMX_DLL_VERSION)

/* *************************************
 * functions defined in the vusbdmx.dll *
 ***************************************/

/*
 *   vusbdmx_version(): returns the version number (16bit, 4 digits BCD)
 * Current version is VUSBDMX_DLL_VERSION. Use the Macro 
 * VUSBDMX_DLL_VERSION_CHECK() compare dll's and header files version.
 */
USHORT	VUSBDMX_TYPE	vusbdmx_version();

/*
 *   vusbdmx_open(): open device number <device>, where 0 is the first
 * and unlimit devices are supported. The function returnes 
 * STATUS_INVALID_HANDLE_VALUE if <device> is not supported. Use the
 * returned handle to access the device later on. One device can be
 * opened an unlimited number of times.
 */
BOOL	VUSBDMX_TYPE	vusbdmx_open(USHORT device, PHANDLE h);

/*
 *   vusbdmx_close(): close the device identified by the given handle.
 */
BOOL	VUSBDMX_TYPE vusbdmx_close(HANDLE h);


/*
 *   vusbdmx_device_id(): read the device id of the device
 */
BOOL	VUSBDMX_TYPE	vusbdmx_device_id(HANDLE h, PUSHORT pid);

/*
 *   vusbdmx_is_XXX(): identify the device identified by the given handle.
 * Each function returns TRUE if the device matches.
 */
BOOL	VUSBDMX_TYPE	vusbdmx_is_rodin1(HANDLE h);

/*
 *   vusbdmx_product_get(): read the product string from the device.
 * size specifies the maximum size of the buffer pointed to by <string> 
 * (unit bytes).
 */
BOOL	VUSBDMX_TYPE	vusbdmx_product_get(HANDLE h, PWCHAR string, USHORT size);

/*
 *   vusbdmx_serial_number_get(): read the serial number for the device.
 * size specifies the maximum size of the buffer pointed to by <string> 
 * (unit bytes).
 */
BOOL	VUSBDMX_TYPE	vusbdmx_serial_number_get(HANDLE h, PWCHAR string, USHORT size);

/*
 *   vusbdmx_eeprom_XXX(): get/set data in eeprom of interface
 */
BOOL	VUSBDMX_TYPE	vusbdmx_eeprom_set(HANDLE h, const void *data, USHORT size);
BOOL	VUSBDMX_TYPE	vusbdmx_eeprom_get(HANDLE h, void *data, USHORT size);

/*
 *   vusbdmx_device_version(): Read the the device version of a device.
 * the device version is one of the values within the USBs configuration
 * descriptor (BcdDevice). pversion is only valid if the function returns
 * TRUE.
 */
BOOL	VUSBDMX_TYPE	vusbdmx_device_version(HANDLE h, PUSHORT pversion);

/*
 *   VUSBDMX_TX(): transmitt a frame using the new protocol on bulk endpoints
 *
 * INPUTs:	h			- handle to the device, returned by vusbdmx_open()
 *			universe	- addressed universe
 *			slots		- number of bytes to be transmitted, as well as sizeof(buffer)
 *						  for DMX512: buffer[0] == startcode, slots <= 513
 *			buffer		- data to be transmitted,  !!! sizeof(buffer) >= slots !!!
 *			config		- configuration of the transmitter, see below for possible values
 *			time		- time value in s, depending on config, either timeout or delay
 *			time_break	- break time in s (can be zero, to not transmitt a break)
 *			time_mab	- Mark-after-Break time (can be zero)
 * OUTPUTs:	ptimestamp	- timestamp of this frame in ms, does overrun
 *			pstatus		- status of this transmission, see below for possible values
 */
BOOL	VUSBDMX_TYPE vusbdmx_tx(IN HANDLE h, IN UCHAR universe, IN USHORT slots, 
							  IN PUCHAR buffer, IN UCHAR config, IN FLOAT time, 
							  IN FLOAT time_break, IN FLOAT time_mab, 
							  OUT PUSHORT ptimestamp, OUT PUCHAR pstatus);
/*
 * values of config (to be ored together)
 */
#define VUSBDMX_BULK_CONFIG_DELAY	(0x01)	// delay frame by time
#define VUSBDMX_BULK_CONFIG_BLOCK	(0x02)	// block while frame is not transmitting (timeout given by time)
#define VUSBDMX_BULK_CONFIG_RX		(0x04)	// switch to RX after having transmitted this frame
#define VUSBDMX_BULK_CONFIG_NORETX	(0x08)	// do not retransmit this frame
#define VUSBDMX_BULK_CONFIG_SPEED	(0x80)	// send data with 57600 baud

/*
 *   VUSBDMX_RX(): receive a frame using the new protocol on bulk endpoints
 *
 * INPUTs:	h			- handle to the device, returned by vusbdmx_open()
 *			universe	- addressed universe
 *			slots_set	- number of bytes to receive, as well as sizeof(buffer)
 *						  for DMX512: buffer[0] == startcode, slots_set <= 513
 *			buffer		- data to be transmitted,  !!! sizeof(buffer) >= slots !!!
 *			timeout		- timeout for receiving the total frame in s,
 *			timeout_rx	- timeout between two slots used to detect premature end of frames
 * OUTPUTs:	pslots_get	- number of slots actually received, *pslots_get <= slots_set
 *          ptimestamp	- timestamp of this frame in ms, does overrun
 *			pstatus		- status of the reception, see below for possible values
 */
BOOL	VUSBDMX_TYPE vusbdmx_rx(IN HANDLE h, IN UCHAR universe, IN USHORT slots_set, 
							  IN PUCHAR buffer, IN FLOAT timeout, IN FLOAT timeout_rx,
							  OUT PUSHORT pslots_get, OUT PUSHORT ptimestamp, OUT PUCHAR pstatus);

/*
 * values of *pstatus
 */
#define VUSBDMX_BULK_STATUS_OK				(0x00)
#define VUSBDMX_BULK_STATUS_TIMEOUT			(0x01)	// request timed out	
#define VUSBDMX_BULK_STATUS_TX_START_FAILED	(0x02)	// delayed start failed
#define VUSBDMX_BULK_STATUS_UNIVERSE_WRONG	(0x03)	// wrong universe addressed\tabularnewline
#define VUSBDMX_BULK_STATUS_RX_OLD_FRAME	(0x10)	// old frame not read
#define VUSBDMX_BULK_STATUS_RX_TIMEOUT		(0x20)	// receiver finished with timeout (ored with others)
#define VUSBDMX_BULK_STATUS_RX_NO_BREAK		(0x40)	// frame without break received (ored with others)
#define VUSBDMX_BULK_STATUS_RX_FRAMEERROR	(0x80)	// frame finished with frame error (ored with others)

/*
 * macro to check, it the return status is ok
 */
#define VUSBDMX_BULK_STATUS_IS_OK(s) (s == VUSBDMX_BULK_STATUS_OK)

/*
 *   vusbdmx_id_led_XXX(): get/set the "id-led", the way the TX-led is handled:
 * special value: see below
 * other:         the blue led blinks the given number of times and then pauses
 */
BOOL	VUSBDMX_TYPE	vusbdmx_id_led_set(HANDLE h, UCHAR id);
BOOL	VUSBDMX_TYPE	vusbdmx_id_led_get(HANDLE h, PUCHAR id);

/*
 * special values of id
 */
#define VUSBDMX_ID_LED_USB		(0xff)	// display the USB status: blink with 2Hz on USB transactions
#define VUSBDMX_ID_LED_USB_RX	(0xfe)	// display USB and receiver status. the LED blinks red if not valid dmx signal in received

#endif // VUSBDMX_H
