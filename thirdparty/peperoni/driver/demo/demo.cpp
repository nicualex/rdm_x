/*
 * DEMO.CPP - Demo code on how to use the vusbdmx.dll with load-time 
 * dynamic linking. If the dll is not present this demo will not start.
 * Link you code against vusbdmx.lib.
 *
 * This file is provided as is to allow an easy start with the
 * vusbdmx driver and dll.
 *
 * In case of trouble please contact driver@lighting-solutions.de or
 * call +49/40/600877-51.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>

/* the vusbdmx include file with all function definitions */
#include "vusbdmx.h"

/*
 * Send a DMX512 frame
 *
 * slots is the number of slots to send incl. startcode
 * buffer contains the dmx512 frame to send incl. startcode
 */
static void tx_dmx(const HANDLE h, const USHORT slots, const PUCHAR buffer)
{
	UCHAR  status;	// return status of the sending process

	if (   h == INVALID_HANDLE_VALUE 
		|| slots > 513)
		return;

	/* send a DMX512 frame with 200us break and 20us MaB */
	if (!vusbdmx_tx(h, 0, slots, buffer, VUSBDMX_BULK_CONFIG_BLOCK, 100e-3,
					200e-6, 20e-6, 0, &status))
		printf("ERROR: vusbdmx_tx() error\n");
	/* check transaction status, see usbdmx.h for details */
	else if (!VUSBDMX_BULK_STATUS_IS_OK(status))
		printf("ERROR: vusbdmx_tx(): status = 0x%02x\n", status);
}

/*
 * Send a special frame with 57600 baud and trailing break
 *
 * slots is the number of slots to send
 * buffer contains the frame to send
 */
static void tx_special(const HANDLE h, const USHORT slots, const PUCHAR buffer)
{
	UCHAR  status;	// return status of the sending process

	if (   h == INVALID_HANDLE_VALUE 
		|| slots > 513)
		return;

	/****
	 * 1. send frame without break and mab, send data with 57600 baud */
	if (!vusbdmx_tx(h, 0, slots, buffer, VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_SPEED | VUSBDMX_BULK_CONFIG_NORETX, 100e-3,
				    0e-6, 0e-6, 0, &status))
		printf("ERROR (data): usbdmx_tx() error\n");
	/* check transaction status, see usbdmx.h for details */
	else if (!VUSBDMX_BULK_STATUS_IS_OK(status))
		printf("ERROR (data): vusbdmx_tx(): status = 0x%02x\n", status);

	/****
	 * 2. send 500us break */
	if (!vusbdmx_tx(h, 0, 0, buffer, VUSBDMX_BULK_CONFIG_BLOCK | VUSBDMX_BULK_CONFIG_SPEED | VUSBDMX_BULK_CONFIG_NORETX, 100e-3,
					500e-6, 0e-6, 0, &status))
		printf("ERRUR (berak): usbdmx_tx() error\n");
	/* check transaction status, see usbdmx.h for details */
	else if (!VUSBDMX_BULK_STATUS_IS_OK(status))
		printf("ERROR (break): vusbdmx_tx(): status = 0x%02x\n", status);
}

#define TEST(a) if (!a) {printf("\nERROR: " #a "\n"); return 1;}

static int test_eeprom(HANDLE h)
{

	unsigned int i;
	char buffer_set[0x100];
	char buffer_get[0x100];

	// fill input buffer with random data
	for (i = 0; i < sizeof(buffer_set); ++i)
		buffer_set[i] = rand() & 0xff;

	// send data to device
	TEST(vusbdmx_eeprom_set(h, buffer_set, sizeof(buffer_set)));
	// read data back
	TEST(vusbdmx_eeprom_get(h, buffer_get, sizeof(buffer_get)));

	// compare buffer
	for (i = 0; i < sizeof(buffer_set); ++i)
	{
		if (buffer_set[i] != buffer_get[i])
		{
			printf("ERROR: EEprom content has changed at address %u, expected %u, got %u", i, buffer_set[i], buffer_get[i]);
				return -1;
		}
	}

	return 0;
}


/*
 * main demo entry: open a Rodin1 and send some data
 */
int main(int argc, char* argv[])
{
	HANDLE h;				/* handle to one interface */
	UCHAR  bufnew[0x201];	/* buffer for one dmx512 frame incl. startcode */
	USHORT version;			/* version number in BCD */
	WCHAR  product[64];		/* product string */

	/* verify VUSBDMX dll version */
	if (!VUSBDMX_DLL_VERSION_CHECK())
	{
		printf("VUSBDMX.DLL version does not match, giving up!\n");
		printf("found %i, expected %i\n", vusbdmx_version(), VUSBDMX_DLL_VERSION);
		return 1;
	}

	printf("Using VUSBDMX.DLL version 0x%x\n\n", vusbdmx_version());

    /* open the first devices (number 0) */
	if (!vusbdmx_open(0, &h))
	{
		printf("no usbdmx-interface available, giving up!\n");
		return 1;
	}

	if (!vusbdmx_product_get(h, product, sizeof(product)))
		printf("ERROR: reading product string failed\n");

	vusbdmx_device_version(h, &version);
	
	/* identify the interface */
	printf("The interface found is a %ws version 0x%04x\n", product, version);
	putchar('\n');

	test_eeprom(h);

	// prepare the buffer with all 0
	memset(bufnew, 0, sizeof(bufnew));

	printf("press any key to quit demo\n");

	while (!_kbhit())
	{
		/*
		 * send a dmx512 frame with 513 slots (incl. startcode)
		 */
		tx_dmx(h, 513, bufnew);

		/*
		 * send special frame with 100 slots
		 */
		tx_special(h, 100, bufnew);
	}


	/* close the interface */
	vusbdmx_close(h);

	printf("demo code finished\n");

	getchar();
	return 0;
}
