/**
 * Appcelerator Titanium - licensed under the Apache Public License 2
 * see LICENSE in the root folder for details on the license.
 * Copyright (c) 2008 Appcelerator, Inc. All Rights Reserved.
 */

#include "win32_sound.h"
#include <comutil.h>
#include <stdlib.h>
#include <math.h>

namespace ti
{
	UINT Win32Sound::graphNotifyMessage = ::RegisterWindowMessage(PRODUCT_NAME"GraphNotify");

	Win32Sound::Win32Sound(std::string &url) :
		Sound(url),
		looping(false),
		callback(0),
		path(URLUtils::URLToPath(url)),
		state(STOPPED),
		graphBuilder(NULL),
		mediaControl(NULL),
		mediaEventEx(NULL),
		mediaSeeking(NULL)
	{
		InitGraphBuilder();

		// why does a sound event need an HWND?? oh windows..
		// TODO: We need to have a hidden window to listen for all these orphan 
		// messages. There is no risk of a hidden window blinking out of existence. 
		SharedValue value = Host::GetInstance()->GetGlobalObject()->GetNS(
			"UI.mainWindow.windowHandle");
		if (value->IsVoidPtr())
		{
			HWND hwnd = (HWND) value->ToVoidPtr();
			mediaEventEx->SetNotifyWindow((OAHWND)hwnd, graphNotifyMessage, 0);
			
			SharedKMethod graphCallback = 
				StaticBoundMethod::FromMethod<Win32Sound>(
					this, &Win32Sound::GraphCallback);

			Host::GetInstance()->GetGlobalObject()->CallNS(
				"UI.mainWindow.addMessageHandler", 
				Value::NewInt(graphNotifyMessage), 
				Value::NewMethod(graphCallback));
		}

		this->LoadFile();
	}

	Win32Sound::~Win32Sound()
	{
		if (graphBuilder)
			graphBuilder->Release();
		if (mediaControl)
			mediaControl->Release();
		if (mediaEventEx)
			mediaEventEx->Release();
		if (mediaSeeking)
			mediaSeeking->Release();
	}
	
	void Win32Sound::LoadFile()
	{
		BSTR pathBstr = _bstr_t(this->path.c_str());
		graphBuilder->RenderFile(pathBstr, NULL);
	}

	void Win32Sound::InitGraphBuilder()
	{
		HRESULT hr;

		hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
			IID_IGraphBuilder, (void **) &graphBuilder);
		if (FAILED(hr))
			throw ValueException::FromString("Failed creating Graph Builder for sound");
		
		hr = graphBuilder->QueryInterface(IID_IBasicAudio, (void**) &basicAudio);
		if (FAILED(hr))
			throw ValueException::FromString("Failed querying IBasicAudio for sound");
		
		hr = graphBuilder->QueryInterface(IID_IMediaControl, (void **) &mediaControl);
		if (FAILED(hr))
			throw ValueException::FromString("Failed querying IMediaControl for sound");
		
		hr = graphBuilder->QueryInterface(IID_IMediaEventEx, (void **) &mediaEventEx);
		if (FAILED(hr))
			throw ValueException::FromString("Failed querying IMediaEventEx for sound");
		
		hr = graphBuilder->QueryInterface(IID_IMediaSeeking, (void **) &mediaSeeking);
		if (FAILED(hr))
			throw ValueException::FromString("Failed querying IMediaSeeking for sound");
	}

	void Win32Sound::Play()
	{
		if (this->state != PLAYING)
		{
			this->state = PLAYING;
			mediaControl->Run();
		}
	}
	
	void Win32Sound::Pause()
	{
		if (this->state == PLAYING)
		{
			this->state = PAUSED;
			mediaControl->Stop();
		}
	}
	
	void Win32Sound::Stop()
	{
		if (this->state == PLAYING)
		{
			this->state = STOPPED;
			mediaControl->Stop();

			// Set the stream location to start at 0 (absolute beginning)
			// and have it end at NoPosition. I.E. keep playing.
			LONGLONG newStartPosition = 0;
			mediaSeeking->SetPositions(
				&newStartPosition, AM_SEEKING_AbsolutePositioning, 
				NULL, AM_SEEKING_NoPositioning);
		}
	}
	
	void Win32Sound::Reload()
	{
		this->Stop();
		this->LoadFile();
	}
	
	void Win32Sound::SetVolume(double volume)
	{
		if (volume > 1.0)
			volume = 1.0;
		else if (volume < 0.0)
			volume = 0.0;

		long newVolume = -1 * floor((1.0-volume)*4000);
		basicAudio->put_Volume(newVolume);
	}
	
	double Win32Sound::GetVolume()
	{
		long volume = 0;
		HRESULT hr = basicAudio->get_Volume(&volume);
		if (SUCCEEDED(hr))
		{
			if (volume < -4000.0)
				volume = -4000.0;
		}
		else 
		{
			throw ValueException::FromString(
				"Could not get current volume from IBasicAudio.");		
		}
		return 1.0 - (abs(volume) / 4000.0);
	}
	
	void Win32Sound::SetLooping(bool loop)
	{
		this->looping = true;
	}
	
	bool Win32Sound::IsLooping()
	{
		return this->looping;
	}
	
	bool Win32Sound::IsPlaying()
	{
		return state == PLAYING;
	}
	
	bool Win32Sound::IsPaused()
	{
		return state == PAUSED;
	}
	
	void Win32Sound::Completed()
	{
		if (!callback.isNull())
		{
			// GraphCallback already executes from the main thread 
			// (WndProc event), but we want this to execute in a deferred way.
			Host::GetInstance()->InvokeMethodOnMainThread(
				callback, ValueList(Value::NewBool(true)), false);
		}
	}
	
	void Win32Sound::GraphCallback(const ValueList& args, SharedValue result)
	{
		long code, param1, param2;

		// Get the next event on the queue and wait 
		// 0 milliseconds when the queue is empty.
		HRESULT hr = mediaEventEx->GetEvent(&code, &param1, &param2, 0);
		while (hr == S_OK)
		{ 
			mediaEventEx->FreeEventParams(code, param1, param2);
			if ((EC_COMPLETE == code) || (EC_USERABORT == code))
			{
				this->Stop();

				// Run the callback before playing.
				// The callback may want to halt looping.
				this->Completed();

				if (this->IsLooping())
					this->Play();

				this->Completed();
			} 

			hr = mediaEventEx->GetEvent(&code, &param1, &param2, 0);
		}
	}
	
	void Win32Sound::OnComplete(SharedKMethod callback)
	{
		this->callback = callback;
	}
}
