/*
  Faux86: A portable, open-source 8086 PC emulator.
  Copyright (C)2018 James Howard
  Based on Fake86
  Copyright (C)2010-2013 Mike Chambers

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include "kernel.h"
#include "CircleHostInterface.h"
#include "faux86/VM.h"
#include "CircleDeviceDisk.h"
#include "FatFsDisk.h"
#include "SDRAM.h"
#include <new>
#include "RPC_internal.h"
#include "Keymap.h"

// Embedded disks / ROMS
#if USE_EMBEDDED_BOOT_FLOPPY
#include "../data/dosboot.h"
#endif
#if USE_EMBEDDED_ROM_IMAGES
#include "../data/asciivga.h"
#include "../data/pcxtbios.h"
#include "../data/videorom.h"
#include "../data/rombasic.h"
#endif

#define REQUIRED_KERNEL_MEMORY (0x2000000)

#if KERNEL_MAX_SIZE < REQUIRED_KERNEL_MEMORY
//#error "Not enough kernel space: change KERNEL_MAX_SIZE in circle/sysconfig.h to at least 32 megabytes and recompile circle"
#endif

#define AUDIO_SAMPLE_RATE 44100

static const char FromKernel[] = "kernel";

using namespace Faux86;

CKernel::CKernel (void) : wifi_data(&root, 1), ota_data(&root, 2), wifi_data_fs("wlan"), ota_data_fs("fs")

{
}

CKernel::~CKernel (void)
{
}

static Faux86::VM* _vm = NULL;

uint8_t keys[3] = {0, 0, 0};

void on_key(uint8_t k1, uint8_t k2, uint8_t k3) {
	if (_vm) {
		if (k1 != 0) {
			_vm->input.handleKeyDown(modifier2xtMapping[k1]);
		} else {
			_vm->input.handleKeyUp(modifier2xtMapping[keys[0]]);			
		}

/*
		if (k2 != 0) {
			_vm->input.handleKeyDown(usb2xtMapping[k2]);
		} else {
			_vm->input.handleKeyUp(usb2xtMapping[keys[1]]);			
		}
*/

		if (k3 != 0) {
			_vm->input.handleKeyDown(usb2xtMapping[k3]);
		} else {
			_vm->input.handleKeyUp(usb2xtMapping[keys[2]]);			
		}

		printf("received %x %x %x\n", k1, k2, k3);
		keys[0] = k1;
		keys[1] = k2;
		keys[2] = k3;
	}
}

void DG_Init();

bool CKernel::Initialize ()
{
	bool bOK = true;

	// Video begin
	// USBHOST begin
	DG_Init();

	RPC1.begin();
  RPC1.bind("on_key", on_key);

	wifi_data_fs.mount(&wifi_data);
	ota_data_fs.mount(&ota_data);

	if(bOK)
	{
		HostInterface = new CircleHostInterface();
		vmConfig = new Config(HostInterface);
		
		vmConfig->audio.sampleRate = 44100;

		log(Log, "Attempting to load system files\n");
		
		vmConfig->biosFile = HostInterface->openFile("/wlan/pcxtbios.bin");
		vmConfig->videoRomFile = HostInterface->openFile("/wlan/videorom.bin");
		vmConfig->asciiFile = HostInterface->openFile("/wlan/asciivga.dat");
		vmConfig->diskDriveA = HostInterface->openFile("/wlan/dosboot.img");

#if USE_EMBEDDED_ROM_IMAGES
		if(!vmConfig->biosFile)
			vmConfig->biosFile = new EmbeddedDisk(pcxtbios, sizeof(pcxtbios));
		if(!vmConfig->videoRomFile)
			vmConfig->videoRomFile = new EmbeddedDisk(videorom, sizeof(videorom));
		if(!vmConfig->asciiFile)
			vmConfig->asciiFile = new EmbeddedDisk(asciivga, sizeof(asciivga));
		if(!vmConfig->romBasicFile)
			vmConfig->romBasicFile = new EmbeddedDisk(rombasic, sizeof(rombasic));
#endif
#if USE_EMBEDDED_BOOT_FLOPPY
		if(!vmConfig->diskDriveA)
			vmConfig->diskDriveA = new EmbeddedDisk(dosboot, sizeof(dosboot));
#endif

		if(!vmConfig->diskDriveA) {
			auto dos = Faux86::FatFsDisk::open("/fs/dosboot.img");
			vmConfig->diskDriveA = dos;
		}

		if(!vmConfig->diskDriveC) {
			auto dos = Faux86::FatFsDisk::open("/fs/wolf.img");
			vmConfig->diskDriveC = dos;
		}

		vmConfig->diskDriveD = new CircleDeviceDisk(&ota_data);

		// TODO
		//vmConfig->parseCommandLine(argc, argv);
		
		log(Log, "Creating VM\n");
		void* place = ea_malloc(sizeof(Faux86::VM));

		vm = new (place) Faux86::VM(vmConfig);

		log(Log, "Init VM\n");
		
		bOK = vm->init();
		_vm = vm;
	}
	
	return bOK;
}

void DG_DrawFrame();
int getNextFrameBuffer();

TShutdownMode CKernel::Run (void)
{
	while (vm->simulate())
	{
		HostInterface->tick(*vm);
		//m_Timer.MsDelay (0);
	}

	return ShutdownHalt;
}

