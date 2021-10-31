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
#include "CircleHostInterface.h"
#include "faux86/VM.h"
#include "kernel.h"
#include "FatFsDisk.h"

using namespace Faux86;

CircleHostInterface* CircleHostInterface::instance;

#include "Arduino.h"
#include "Portenta_Video.h"
#include "SDRAM.h"

struct edid recognized_edid;
uint32_t LCD_X_Size = 0, LCD_Y_Size = 0;

DMA2D_HandleTypeDef DMA2D_Handle;
uint8_t* DG_ScreenBuffer = NULL;

uint32_t fb;

uint32_t __ALIGNED(32) L8_CLUT[256];
static DMA2D_CLUTCfgTypeDef clut;

static void DMA2D_Init(uint16_t xsize, uint16_t ysize)
{
  /*##-1- Configure the DMA2D Mode, Color Mode and output offset #############*/
  DMA2D_Handle.Init.Mode         = DMA2D_M2M_PFC;
  DMA2D_Handle.Init.ColorMode    = DMA2D_OUTPUT_RGB565;
  DMA2D_Handle.Init.OutputOffset = 0; //stm32_getXSize() - xsize;
  DMA2D_Handle.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;  /* No Output Alpha Inversion*/
  DMA2D_Handle.Init.RedBlueSwap   = DMA2D_RB_REGULAR;     /* No Output Red & Blue swap */

  /*##-2- DMA2D Callbacks Configuration ######################################*/
  DMA2D_Handle.XferCpltCallback  = NULL;

  /*##-3- Foreground Configuration ###########################################*/
  DMA2D_Handle.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA; //DMA2D_NO_MODIF_ALPHA;
  DMA2D_Handle.LayerCfg[1].InputAlpha = 0x00;
  DMA2D_Handle.LayerCfg[1].InputColorMode = DMA2D_INPUT_L8; //DMA2D_OUTPUT_RGB565;
  //DMA2D_Handle.LayerCfg[1].ChromaSubSampling = cssMode;
  DMA2D_Handle.LayerCfg[1].InputOffset = 0; //LCD_Y_Size - ysize;
  DMA2D_Handle.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR; /* No ForeGround Red/Blue swap */
  DMA2D_Handle.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA; /* No ForeGround Alpha inversion */

  DMA2D_Handle.Instance          = DMA2D;

  /*##-4- DMA2D Initialization     ###########################################*/
  HAL_DMA2D_Init(&DMA2D_Handle);
  HAL_DMA2D_ConfigLayer(&DMA2D_Handle, 1);

  //memcpy(L8_CLUT, colors, 256 * 4);
  clut.pCLUT = (uint32_t *)L8_CLUT; //(uint32_t *)colors;
  clut.CLUTColorMode = DMA2D_CCM_ARGB8888;
  clut.Size = 0xFF;

#ifdef CORE_CM7
  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();
#endif

  HAL_DMA2D_CLUTLoad(&DMA2D_Handle, clut, 1);
  HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 100);
}

//#define DEBUG_CM7_VIDEO

size_t _expected_width;
size_t _expected_height;

static void DMA2D_CopyBuffer(uint32_t *pSrc, uint32_t *pDst)
{
  HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 100);  /* wait for the previous DMA2D transfer to ends */
  /* copy the new decoded frame to the LCD Frame buffer*/
  HAL_DMA2D_Start(&DMA2D_Handle, (uint32_t)pSrc, (uint32_t)pDst, 640, 480 /*_expected_width, _expected_height*/);
#if defined(CORE_CM7) && !defined(DEBUG_CM7_VIDEO) 
  HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 100);  /* wait for the previous DMA2D transfer to ends */
#endif
}

void DG_DrawFrame()
{
#if 0 //def CORE_CM7
  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();
  SCB_InvalidateDCache_by_Addr((uint32_t *)DG_ScreenBuffer,  stm32_getXSize() * stm32_getYSize());
  SCB_InvalidateDCache_by_Addr((uint32_t *)fb,  stm32_getXSize() * stm32_getYSize() *2);
#endif

  DMA2D_CopyBuffer((uint32_t *)DG_ScreenBuffer, (uint32_t *)fb);
#ifdef CORE_CM7
  SCB_CleanInvalidateDCache();
  //SCB_CleanDCache_by_Addr((uint32_t *)DG_ScreenBuffer,  stm32_getXSize() * stm32_getYSize());
  //SCB_CleanDCache_by_Addr((uint32_t *)fb,  stm32_getXSize() * stm32_getYSize() *2);
#endif
	fb = getNextFrameBuffer();
}

void DG_Init()
{
  int ret = -1;

  ret = anx7625_init(0);
  if(ret < 0) {
    printf("Cannot continue, anx7625 init failed.\n");
    while(1);
  }

  anx7625_wait_hpd_event(0);
  anx7625_dp_get_edid(0, &recognized_edid);
  //edid_set_framebuffer_bits_per_pixel(&recognized_edid, 16, 0);
  //set_display_mode(&recognized_edid, EDID_MODE_720x480_60Hz);
  //anx7625_dp_start(0, &recognized_edid, EDID_MODE_1280x720_60Hz);
  anx7625_dp_start(0, &recognized_edid, EDID_MODE_640x480_60Hz);

  LCD_X_Size = stm32_getXSize();
  LCD_Y_Size = stm32_getYSize();

  SDRAM.begin(getFramebufferEnd());

 	fb = getNextFrameBuffer();

	//fb = getNextFrameBuffer();

  stm32_LCD_Clear(0xFFFFFF00);
  stm32_LCD_Clear(0xFFFFFF00);
}

#include <stdarg.h>

void Faux86::log(Faux86::LogChannel channel, const char* message, ...)
{
#if (USE_SERIAL_LOGGING || FAKE_FRAMEBUFFER)
	va_list myargs;
	va_start(myargs, message);
	
	//if(channel == Faux86::LogChannel::LogRaw)
	//{
	//	CString Message;
	//	Message.Format (message, myargs);
	//	CLogger::Get()->GetTarget()->Write ((const char *) Message, Message.GetLength ());
	//}
	//else
	{
		CLogger::Get()->WriteV("Faux86", LogNotice, message, myargs);
	}
	va_end(myargs);
#endif

  char buffer[256];
  va_list args;
  va_start (args, message);
  vsnprintf (buffer,256, message, args);
  if (channel == Faux86::LogChannel::LogRaw) {
  	printf("%s", buffer);
  	if (buffer[0] > 0x7F) {
  		printf("\n");
  	}
  	fflush(stdout);
  } else {
  	printf("%s\n", buffer);  	
  }
  va_end (args);
}

void CircleFrameBufferInterface::init(uint32_t desiredWidth, uint32_t desiredHeight)
{	
	if (DG_ScreenBuffer == NULL) {
		DG_ScreenBuffer = (uint8_t*)ea_malloc(stm32_getXSize() * stm32_getYSize());
	}
	memset(DG_ScreenBuffer, 0, stm32_getXSize() * stm32_getYSize());

	log(Log, "requesting size %d %d", desiredWidth, desiredHeight);
	log(Log, "got size %d %d", stm32_getXSize(), stm32_getYSize());

	_expected_width = desiredWidth;
	_expected_height = desiredHeight;

	surface = new RenderSurface();
	surface->width = desiredWidth;
	surface->height = desiredHeight;
	surface->pitch = desiredWidth;
	surface->pixels = (uint8_t*)DG_ScreenBuffer;
	DMA2D_Init(getSurface()->width, getSurface()->height);
}

void CircleFrameBufferInterface::resize(uint32_t desiredWidth, uint32_t desiredHeight)
{
	if(surface->width == desiredWidth && surface->height == desiredHeight)
		return;

	log(Log, "requesting size %d %d", desiredWidth, desiredHeight);
	log(Log, "got size %d %d", stm32_getXSize(), stm32_getYSize());

	_expected_width = desiredWidth;
	_expected_height = desiredHeight;

	ea_free(DG_ScreenBuffer);

	DG_ScreenBuffer = (uint8_t*)ea_malloc(stm32_getXSize() * stm32_getYSize());
	memset(DG_ScreenBuffer, 0, stm32_getXSize() * stm32_getYSize());

	surface = new RenderSurface();
	surface->width = desiredWidth;
	surface->height = desiredHeight;
	surface->pitch = desiredWidth;
	surface->pixels = (uint8_t*)DG_ScreenBuffer;
}

RenderSurface* CircleFrameBufferInterface::getSurface()
{ 
	if (surface == nullptr) {
		init(640, 400);
	}
	return surface;
}

static Palette* current_palette = NULL;

void CircleFrameBufferInterface::setPalette(Palette* palette)
{
	if (palette == current_palette) {
		return;
	}
	current_palette = palette;
	for(int n = 0; n < 256; n++)
	{
		uint32_t colour = (0xff << 24) | (palette->colours[n].r << 16) | (palette->colours[n].g << 8) | (palette->colours[n].b);
		L8_CLUT[n] = colour;
	}
	DMA2D_Init(getSurface()->width, getSurface()->height);
}


int i = 0;
void CircleFrameBufferInterface::present() {
	DG_DrawFrame();
}

void CircleAudioInterface::init(VM& vm)
{
	//pwmSound = new PWMSound(vm.audio, &interruptSystem, vm.audio.sampleRate);
	//pwmSound->Start();
}

void CircleAudioInterface::shutdown()
{
}

CircleTimerInterface::CircleTimerInterface()
{
	lastTimerSample = millis();
}

uint64_t CircleTimerInterface::getHostFreq()
{
	return 1000;
}

uint64_t CircleTimerInterface::getTicks()
{
	uint32_t timerSample = millis();
	uint32_t delta = timerSample >= lastTimerSample ? (timerSample - lastTimerSample) : (0xffffffff - lastTimerSample) + timerSample;
	lastTimerSample = timerSample;
	currentTick += delta;
	return currentTick;
}

DiskInterface* CircleHostInterface::openFile(const char* filename)
{
	return FatFsDisk::open(filename);
} 

CircleHostInterface::CircleHostInterface()
{
	instance = this;
	
	/*
	keyboard = (CUSBKeyboardDevice *) deviceNameService.GetDevice ("ukbd1", FALSE);
	
	if(keyboard)
	{
		keyboard->RegisterKeyStatusHandlerRaw (keyStatusHandlerRaw);
	}
	
	CMouseDevice* mouse = (CMouseDevice *) deviceNameService.GetDevice ("mouse1", FALSE);
	
	if(mouse)
	{
		mouse->RegisterStatusHandler(mouseStatusHandler);
	}
	*/
}

void CircleHostInterface::tick(VM& vm)
{
	while(inputBufferSize > 0)
	{
		InputEvent& event = inputBuffer[inputBufferPos];
		
		switch(event.eventType)
		{
			case EventType::KeyPress:
			{
				vm.input.handleKeyDown(event.scancode);
			}
			break;
			case EventType::KeyRelease:
			{
				vm.input.handleKeyUp(event.scancode);
			}
			break;
			case EventType::MousePress:
			{
				vm.mouse.handleButtonDown(event.mouseButton);
			}
			break;
			case EventType::MouseRelease:
			{
				vm.mouse.handleButtonUp(event.mouseButton);
			}
			break;
			case EventType::MouseMove:
			{
				vm.mouse.handleMove(event.mouseMotionX, event.mouseMotionY);
			}
			break;
		}
		
		inputBufferPos++;
		inputBufferSize --;
		if(inputBufferPos >= MaxInputBufferSize)
		{
			inputBufferPos = 0;
		}
	}
}

void CircleHostInterface::queueEvent(InputEvent& inEvent)
{
	if(inputBufferSize < MaxInputBufferSize)
	{
		int writePos = (inputBufferPos + inputBufferSize) % MaxInputBufferSize;
		inputBuffer[writePos] = inEvent;
		instance->inputBufferSize ++;
	}
}

void CircleHostInterface::queueEvent(EventType eventType, u16 scancode)
{
	if(scancode != 0)
	{
		InputEvent newEvent;
		newEvent.eventType = eventType;
		newEvent.scancode = scancode;
		queueEvent(newEvent);
	}
}

void CircleHostInterface::mouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY)
{
	InputEvent newEvent;

#if 0
	// Mouse presses
	if((nButtons & MOUSE_BUTTON_LEFT) && !(instance->lastMouseButtons & MOUSE_BUTTON_LEFT))
	{
		newEvent.eventType = EventType::MousePress;
		newEvent.mouseButton = SerialMouse::ButtonType::Left;
		instance->queueEvent(newEvent);
	}
	if((nButtons & MOUSE_BUTTON_RIGHT) && !(instance->lastMouseButtons & MOUSE_BUTTON_RIGHT))
	{
		newEvent.eventType = EventType::MousePress;
		newEvent.mouseButton = SerialMouse::ButtonType::Right;
		instance->queueEvent(newEvent);
	}
	
	// Mouse releases
	if(!(nButtons & MOUSE_BUTTON_LEFT) && (instance->lastMouseButtons & MOUSE_BUTTON_LEFT))
	{
		newEvent.eventType = EventType::MouseRelease;
		newEvent.mouseButton = SerialMouse::ButtonType::Left;
		instance->queueEvent(newEvent);
	}
	if(!(nButtons & MOUSE_BUTTON_RIGHT) && (instance->lastMouseButtons & MOUSE_BUTTON_RIGHT))
	{
		newEvent.eventType = EventType::MouseRelease;
		newEvent.mouseButton = SerialMouse::ButtonType::Right;
		instance->queueEvent(newEvent);
	}
#endif

	// Motion events	
	if(nDisplacementX != 0 || nDisplacementY != 0)
	{
		newEvent.eventType = EventType::MouseMove;
		newEvent.mouseMotionX = (int8_t) nDisplacementX;
		newEvent.mouseMotionY = (int8_t) nDisplacementY;
		instance->queueEvent(newEvent);
	}
	
	instance->lastMouseButtons = nButtons;
}

/*
void CircleHostInterface::keyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
	for(int n = 0; n < 8; n++)
	{
		int mask = 1 << n;
		bool wasPressed = (instance->lastModifiers & mask) != 0;
		bool isPressed = (ucModifiers & mask) != 0;
		if(!wasPressed && isPressed)
		{
			instance->queueEvent(EventType::KeyPress, modifier2xtMapping[n]);
		}
		else if(wasPressed && !isPressed)
		{
			instance->queueEvent(EventType::KeyRelease, modifier2xtMapping[n]);
		}
	}
		
	for(int n = 0; n < 6; n++)
	{
		if(instance->lastRawKeys[n] != 0)
		{
			bool inNewBuffer = false;
			
			for(int i = 0; i < 6; i++)
			{
				if(instance->lastRawKeys[n] == RawKeys[i])
				{
					inNewBuffer = true;
					break;
				}
			}
			
			if(!inNewBuffer && instance->inputBufferSize < MaxInputBufferSize)
			{
				instance->queueEvent(EventType::KeyRelease, usb2xtMapping[instance->lastRawKeys[n]]);
			}
		}
	}

	for(int n = 0; n < 6; n++)
	{
		if(RawKeys[n] != 0)
		{
			bool inLastBuffer = false;
			
			for(int i = 0; i < 6; i++)
			{
				if(instance->lastRawKeys[i] == RawKeys[n])
				{
					inLastBuffer = true;
					break;
				}
			}
			
			if(!inLastBuffer && instance->inputBufferSize < MaxInputBufferSize)
			{
				instance->queueEvent(EventType::KeyPress, usb2xtMapping[RawKeys[n]]);
			}
		}
	}

	for(int n = 0; n < 6; n++)
	{
		instance->lastRawKeys[n] = RawKeys[n];
	}

	instance->lastModifiers = ucModifiers;
}
*/