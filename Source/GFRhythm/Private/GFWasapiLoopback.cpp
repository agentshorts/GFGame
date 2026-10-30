#include "GFWasapiLoopback.h"
#include "GFLog.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace GFWasapi
{
	// PKEY_Device_FriendlyName, defined locally to avoid INITGUID / extra libs.
	static const PROPERTYKEY FriendlyNameKey = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

	/** RAII COM init; tolerates an apartment already initialised with another mode. */
	struct FScopedCom
	{
		bool bUninit = false;
		FScopedCom() { bUninit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
		~FScopedCom() { if (bUninit) { CoUninitialize(); } }
	};

	template <typename T> void SafeRelease(T*& P) { if (P) { P->Release(); P = nullptr; } }

	static IMMDeviceEnumerator* CreateEnumerator()
	{
		IMMDeviceEnumerator* Enumerator = nullptr;
		CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&Enumerator));
		return Enumerator;
	}

	static FString GetId(IMMDevice* Device)
	{
		LPWSTR Id = nullptr;
		FString Out;
		if (Device && SUCCEEDED(Device->GetId(&Id)) && Id)
		{
			Out = FString(Id);
			CoTaskMemFree(Id);
		}
		return Out;
	}

	static FString GetFriendlyName(IMMDevice* Device)
	{
		FString Out;
		IPropertyStore* Props = nullptr;
		if (Device && SUCCEEDED(Device->OpenPropertyStore(STGM_READ, &Props)))
		{
			PROPVARIANT Var;
			PropVariantInit(&Var);
			if (SUCCEEDED(Props->GetValue(FriendlyNameKey, &Var)) && Var.vt == VT_LPWSTR && Var.pwszVal)
			{
				Out = FString(Var.pwszVal);
			}
			PropVariantClear(&Var);
			SafeRelease(Props);
		}
		return Out;
	}

	static bool IsFloat32(const WAVEFORMATEX* Fmt)
	{
		if (!Fmt || Fmt->wBitsPerSample != 32)
		{
			return false;
		}
		if (Fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
		{
			return true;
		}
		// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {00000003-0000-0010-8000-00aa00389b71}
		return Fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Fmt)->SubFormat.Data1 == 3;
	}
}
#endif

FGFWasapiLoopback::FGFWasapiLoopback() = default;

FGFWasapiLoopback::~FGFWasapiLoopback()
{
	StopCapture();
}

TArray<FGFAudioOutputDevice> FGFWasapiLoopback::EnumerateOutputDevices()
{
	TArray<FGFAudioOutputDevice> Out;
	FGFAudioOutputDevice& Default = Out.AddDefaulted_GetRef();
	Default.FriendlyName = TEXT("System default output");
	Default.bIsSystemDefault = true;

#if PLATFORM_WINDOWS
	GFWasapi::FScopedCom Com;
	IMMDeviceEnumerator* Enumerator = GFWasapi::CreateEnumerator();
	if (!Enumerator)
	{
		return Out;
	}
	const FString DefaultId = GetDefaultOutputDeviceId();
	IMMDeviceCollection* Collection = nullptr;
	// eRender only: capture endpoints (microphones) are never enumerated.
	if (SUCCEEDED(Enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &Collection)))
	{
		UINT Count = 0;
		Collection->GetCount(&Count);
		for (UINT i = 0; i < Count; ++i)
		{
			IMMDevice* Device = nullptr;
			if (SUCCEEDED(Collection->Item(i, &Device)))
			{
				FGFAudioOutputDevice& D = Out.AddDefaulted_GetRef();
				D.Id = GFWasapi::GetId(Device);
				D.FriendlyName = GFWasapi::GetFriendlyName(Device);
				D.bIsSystemDefault = (D.Id == DefaultId);
				GFWasapi::SafeRelease(Device);
			}
		}
		GFWasapi::SafeRelease(Collection);
	}
	GFWasapi::SafeRelease(Enumerator);
#endif
	return Out;
}

FString FGFWasapiLoopback::GetDefaultOutputDeviceId()
{
	FString Id;
#if PLATFORM_WINDOWS
	GFWasapi::FScopedCom Com;
	if (IMMDeviceEnumerator* Enumerator = GFWasapi::CreateEnumerator())
	{
		IMMDevice* Device = nullptr;
		if (SUCCEEDED(Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Device)))
		{
			Id = GFWasapi::GetId(Device);
			GFWasapi::SafeRelease(Device);
		}
		GFWasapi::SafeRelease(Enumerator);
	}
#endif
	return Id;
}

bool FGFWasapiLoopback::StartCapture(const FString& DeviceIdOrEmpty)
{
#if PLATFORM_WINDOWS
	if (Thread)
	{
		return true;
	}
	RequestedDeviceId = DeviceIdOrEmpty;
	bStopRequested = false;
	bFellBack = false;
	Thread = FRunnableThread::Create(this, TEXT("GFWasapiLoopback"), 0, TPri_AboveNormal);
	return Thread != nullptr;
#else
	return false;
#endif
}

void FGFWasapiLoopback::StopCapture()
{
	if (Thread)
	{
		bStopRequested = true;
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
	bCapturing = false;
}

int32 FGFWasapiLoopback::ReadSamples(TArray<float>& Out, double& OutFirstSampleSeconds)
{
	FScopeLock Lock(&BufferLock);
	Out = MoveTemp(Buffer);
	Buffer.Reset();
	OutFirstSampleSeconds = BufferStartSeconds;
	return Out.Num();
}

uint32 FGFWasapiLoopback::Run()
{
#if PLATFORM_WINDOWS
	using namespace GFWasapi;
	FScopedCom Com;
	IMMDeviceEnumerator* Enumerator = CreateEnumerator();
	IMMDevice* Device = nullptr;
	IAudioClient* Client = nullptr;
	IAudioCaptureClient* Capture = nullptr;
	WAVEFORMATEX* Fmt = nullptr;

	auto Cleanup = [&]()
	{
		if (Client) { Client->Stop(); }
		if (Fmt) { CoTaskMemFree(Fmt); Fmt = nullptr; }
		SafeRelease(Capture);
		SafeRelease(Client);
		SafeRelease(Device);
		SafeRelease(Enumerator);
		bCapturing = false;
	};

	if (!Enumerator)
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: no device enumerator"));
		return 1;
	}
	if (!RequestedDeviceId.IsEmpty())
	{
		Enumerator->GetDevice(*RequestedDeviceId, &Device);
		if (!Device)
		{
			bFellBack = true; // device gone: fall back to system default
		}
	}
	if (!Device && FAILED(Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Device)))
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: no default output device"));
		Cleanup();
		return 1;
	}
	if (FAILED(Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client)))
		|| FAILED(Client->GetMixFormat(&Fmt)))
	{
		Cleanup();
		return 1;
	}
	if (!IsFloat32(Fmt))
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: mix format is not float32 (tag %d, %d bits); live mode unsupported for now"), Fmt->wFormatTag, Fmt->wBitsPerSample);
		Cleanup();
		return 1;
	}
	const REFERENCE_TIME BufferDuration = 10000000; // 1 s in 100 ns units
	if (FAILED(Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, BufferDuration, 0, Fmt, nullptr))
		|| FAILED(Client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&Capture)))
		|| FAILED(Client->Start()))
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: loopback init failed"));
		Cleanup();
		return 1;
	}
	SampleRate = static_cast<int32>(Fmt->nSamplesPerSec);
	const int32 Channels = FMath::Max<int32>(1, Fmt->nChannels);
	const int32 MaxSamples = SampleRate * MaxBufferedSeconds;
	bCapturing = true;
	UE_LOG(LogGF, Log, TEXT("WASAPI: loopback capturing at %d Hz, %d ch"), SampleRate, Channels);

	while (!bStopRequested)
	{
		FPlatformProcess::Sleep(0.005f);
		UINT32 PacketFrames = 0;
		while (SUCCEEDED(Capture->GetNextPacketSize(&PacketFrames)) && PacketFrames > 0)
		{
			BYTE* Data = nullptr;
			UINT32 Frames = 0;
			DWORD Flags = 0;
			UINT64 QpcPosition = 0;
			if (FAILED(Capture->GetBuffer(&Data, &Frames, &Flags, nullptr, &QpcPosition)))
			{
				break;
			}
			// QPC position is in 100 ns units; convert to the FPlatformTime clock (also QPC-based on Windows).
			const double PacketSeconds = static_cast<double>(QpcPosition) * 1e-7;
			const bool bSilent = (Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
			const float* In = reinterpret_cast<const float*>(Data);
			{
				FScopeLock Lock(&BufferLock);
				if (Buffer.Num() == 0)
				{
					BufferStartSeconds = PacketSeconds;
				}
				for (UINT32 f = 0; f < Frames; ++f)
				{
					float Sum = 0.f;
					if (!bSilent)
					{
						for (int32 c = 0; c < Channels; ++c)
						{
							Sum += In[f * Channels + c];
						}
					}
					Buffer.Add(Sum / Channels);
				}
				if (Buffer.Num() > MaxSamples)
				{
					const int32 Drop = Buffer.Num() - MaxSamples;
					Buffer.RemoveAt(0, Drop);
					BufferStartSeconds += static_cast<double>(Drop) / SampleRate;
				}
			}
			Capture->ReleaseBuffer(Frames);
		}
	}
	Cleanup();
#endif
	return 0;
}
