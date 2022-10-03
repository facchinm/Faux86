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
#pragma once

#include "faux86/HostSystemInterface.h"
#include "faux86/Renderer.h"
#include "faux86/SerialMouse.h"

namespace Faux86
{	
	class CircleFrameBufferInterface : public FrameBufferInterface
	{
	public:
		virtual void init(uint32_t desiredWidth, uint32_t desiredHeight) override;
		virtual RenderSurface* getSurface() override;
		virtual void resize(uint32_t desiredWidth, uint32_t desiredHeight) override;
		virtual void present() override;

		virtual void setPalette(Palette* palette) override;

	private:
		uint8_t* frameBuffer = nullptr;
		RenderSurface* surface = nullptr;
	};

	class CircleAudioInterface : public AudioInterface
	{
	public:
		CircleAudioInterface() {}
		virtual void init(VM& vm) override;
		virtual void shutdown() override;

	private:
	};
	
	class CircleTimerInterface : public TimerInterface
	{
	public:
		CircleTimerInterface();
		virtual uint64_t getHostFreq() override;
		virtual uint64_t getTicks() override;

	private:		
		uint64_t currentTick = 0;
		uint64_t lastTimerSample = 0;
	};
	
	class CircleHostInterface : public HostSystemInterface
	{
	public:
		CircleHostInterface();
		
		void tick(VM& vm);
		
		virtual FrameBufferInterface& getFrameBuffer() override { return frameBuffer; }
		virtual TimerInterface& getTimer() override { return timer; }
		virtual AudioInterface& getAudio() override { return audio; }
		virtual DiskInterface* openFile(const char* filename) override;

	private:
		enum class EventType
		{
			KeyPress,
			KeyRelease,
			MousePress,
			MouseRelease,
			MouseMove
		};

		struct InputEvent
		{
			EventType eventType;
			
			union
			{
				uint16_t scancode;
				
				SerialMouse::ButtonType mouseButton;
				
				struct
				{
					int8_t mouseMotionX;
					int8_t mouseMotionY;
				};
			};
		};
	

		void queueEvent(InputEvent& inEvent);
		void queueEvent(EventType eventType, uint16_t scancode);
		
		static void mouseStatusHandler (unsigned nButtons, int nDisplacementX, int nDisplacementY);
		static void keyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6]);

		CircleFrameBufferInterface frameBuffer;
		CircleAudioInterface audio;
		CircleTimerInterface timer;

		static CircleHostInterface* instance;
		
		//CUSBKeyboardDevice* keyboard; 
		
		static constexpr int MaxInputBufferSize = 16;
		
		uint8_t lastModifiers;
		uint8_t lastRawKeys[6];
		InputEvent inputBuffer[MaxInputBufferSize];
		int inputBufferPos = 0;
		int inputBufferSize = 0;
		unsigned lastMouseButtons = 0;
	};
}
