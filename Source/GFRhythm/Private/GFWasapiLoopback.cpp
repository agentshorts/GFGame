#include "GFWasapiLoopback.h"
#include "GFLog.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Event.h"

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

FGFWasapiLoopback::FGFWasapiLoopback()
{
	ReadyEvent = FPlatformProcess::GetSynchEventFromPool(true);
}

FGFWasapiLoopback::~FGFWasapiLoopback()
{
	StopCapture();
	FPlatformProcess::ReturnSynchEventToPool(ReadyEvent);
	ReadyEvent = nullptr;
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

FString FGFWasapiLoopback::ResolveListenDevice(const FString& SavedId, const TArray<FGFAudioOutputDevice>& ActiveOutputs, bool& bOutFellBack)
{
	bOutFellBack = false;
	if (SavedId.IsEmpty())
	{
		return FString();
	}
	for (const FGFAudioOutputDevice& D : ActiveOutputs)
	{
		if (!D.Id.IsEmpty() && D.Id == SavedId)
		{
			return SavedId;
		}
	}
	bOutFellBack = true;
	return FString();
}

bool FGFWasapiLoopback::BeginCapture(const FString& DeviceIdOrEmpty)
{
#if PLATFORM_WINDOWS
	if (Thread)
	{
		return GetStatus() == EGFCaptureStatus::Starting || GetStatus() == EGFCaptureStatus::Capturing;
	}
	RequestedDeviceId = DeviceIdOrEmpty;
	bStopRequested = false;
	bFellBack = false;
	SampleRate = 0;
	ReadyEvent->Reset();
	SetStatus(EGFCaptureStatus::Starting);
	Thread = FRunnableThread::Create(this, TEXT("GFWasapiLoopback"), 0, TPri_AboveNormal);
	if (!Thread)
	{
		SetStatus(EGFCaptureStatus::Failed);
		return false;
	}
	return true;
#else
	SetStatus(EGFCaptureStatus::Failed);
	return false;
#endif
}

bool FGFWasapiLoopback::WaitUntilReady(float TimeoutSeconds)
{
	if (GetStatus() == EGFCaptureStatus::Starting)
	{
		ReadyEvent->Wait(FTimespan::FromSeconds(FMath::Max(0.f, TimeoutSeconds)));
	}
	return GetStatus() == EGFCaptureStatus::Capturing;
}

void FGFWasapiLoopback::SetStatus(EGFCaptureStatus NewStatus)
{
	Status = static_cast<uint8>(NewStatus);
	if (NewStatus != EGFCaptureStatus::Starting && ReadyEvent)
	{
		ReadyEvent->Trigger();
	}
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
	const EGFCaptureStatus S = GetStatus();
	if (S == EGFCaptureStatus::Starting || S == EGFCaptureStatus::Capturing)
	{
		SetStatus(EGFCaptureStatus::Idle);
	}
}

int32 FGFWasapiLoopback::ReadSamples(TArray<float>& Out, double& OutFirstSampleSeconds)
{
	FScopeLock Lock(&BufferLock);
	return Ring.Read(Out, OutFirstSampleSeconds);
}

int64 FGFWasapiLoopback::GetDroppedSamples() const
{
	FScopeLock Lock(&BufferLock);
	return Ring.GetDropped();
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
	};
	auto Fail = [&](const TCHAR* Why, HRESULT Hr)
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: %s (0x%08x)"), Why, static_cast<uint32>(Hr));
		Cleanup();
		SetStatus(EGFCaptureStatus::Failed);
		return 1u;
	};

	if (!Enumerator)
	{
		return Fail(TEXT("no device enumerator"), E_FAIL);
	}
	if (!RequestedDeviceId.IsEmpty())
	{
		Enumerator->GetDevice(*RequestedDeviceId, &Device);
		if (Device)
		{
			// Only ever open render (output) endpoints: an id that points at a capture device (a mic) is refused.
			IMMEndpoint* Endpoint = nullptr;
			EDataFlow Flow = eCapture;
			const bool bIsRender = SUCCEEDED(Device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(&Endpoint)))
				&& SUCCEEDED(Endpoint->GetDataFlow(&Flow)) && Flow == eRender;
			SafeRelease(Endpoint);
			if (!bIsRender)
			{
				UE_LOG(LogGF, Warning, TEXT("WASAPI: saved Listen to device is not an output; using the system default"));
				SafeRelease(Device);
			}
		}
		if (!Device)
		{
			bFellBack = true; // device gone: fall back to system default
		}
	}
	HRESULT Hr = S_OK;
	if (!Device && FAILED(Hr = Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Device)))
	{
		return Fail(TEXT("no default output device"), Hr);
	}
	if (FAILED(Hr = Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&Client)))
		|| FAILED(Hr = Client->GetMixFormat(&Fmt)))
	{
		return Fail(TEXT("couldn't open the output device"), Hr);
	}
	if (!IsFloat32(Fmt))
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: mix format is not float32 (tag %d, %d bits); live mode unsupported for now"), Fmt->wFormatTag, Fmt->wBitsPerSample);
		return Fail(TEXT("unsupported mix format"), E_FAIL);
	}
	const REFERENCE_TIME BufferDuration = 10000000; // 1 s in 100 ns units
	if (FAILED(Hr = Client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, BufferDuration, 0, Fmt, nullptr))
		|| FAILED(Hr = Client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&Capture)))
		|| FAILED(Hr = Client->Start()))
	{
		return Fail(TEXT("loopback init failed"), Hr);
	}
	const int32 Rate = static_cast<int32>(Fmt->nSamplesPerSec);
	const int32 Channels = FMath::Max<int32>(1, Fmt->nChannels);
	{
		FScopeLock Lock(&BufferLock);
		Ring.Reset(Rate * MaxBufferedSeconds, Rate);
	}
	Discontinuities = 0;
	BadTimestamps = 0;
	SampleRate = Rate;
	SetStatus(EGFCaptureStatus::Capturing);
	UE_LOG(LogGF, Log, TEXT("WASAPI: loopback capturing at %d Hz, %d ch"), Rate, Channels);

	TArray<float> Mono;
	Mono.Reserve(Rate / 10);
	bool bLost = false;
	while (!bStopRequested && !bLost)
	{
		FPlatformProcess::Sleep(0.005f);
		for (;;)
		{
			UINT32 PacketFrames = 0;
			Hr = Capture->GetNextPacketSize(&PacketFrames);
			if (FAILED(Hr))
			{
				bLost = true; // AUDCLNT_E_DEVICE_INVALIDATED and friends: the device is gone
				break;
			}
			if (PacketFrames == 0)
			{
				break;
			}
			BYTE* Data = nullptr;
			UINT32 Frames = 0;
			DWORD Flags = 0;
			UINT64 QpcPosition = 0;
			Hr = Capture->GetBuffer(&Data, &Frames, &Flags, nullptr, &QpcPosition);
			if (FAILED(Hr))
			{
				bLost = true;
				break;
			}
			if (Hr == AUDCLNT_S_BUFFER_EMPTY)
			{
				break;
			}
			if (Frames == 0)
			{
				Capture->ReleaseBuffer(0);
				break;
			}
			// Every packet is timed from its own device timestamp, with the QPC -> FPlatformTime offset re-read
			// alongside it (FPlatformTime adds a fixed 16777216 s on Windows), so nothing is extrapolated.
			const double QpcOffset = FGFAudioClock::MeasureQpcOffset();
			const double PacketNow = FGFAudioClock::Now();
			const double PacketSeconds = static_cast<double>(Frames) / Rate;
			const bool bStampOk = (Flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) == 0;
			const double StartSeconds = FGFAudioClock::PacketStartTime(QpcPosition, bStampOk, QpcOffset, PacketNow, PacketSeconds);
			if (!FGFAudioClock::IsStampUsable(QpcPosition, bStampOk, QpcOffset, PacketNow))
			{
				++BadTimestamps;
			}
			// The device lost samples before this packet: the ring must not stitch it onto the previous one.
			const bool bDiscontinuity = (Flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
			if (bDiscontinuity)
			{
				++Discontinuities;
			}
			const bool bSilent = (Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
			const float* In = reinterpret_cast<const float*>(Data);
			// Downmix outside the lock; the lock only covers the ring copy.
			Mono.SetNumUninitialized(Frames, EAllowShrinking::No);
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
				Mono[f] = Sum / Channels;
			}
			Capture->ReleaseBuffer(Frames);
			FScopeLock Lock(&BufferLock);
			Ring.Write(Mono.GetData(), Mono.Num(), StartSeconds, bDiscontinuity);
		}
	}
	if (bLost)
	{
		UE_LOG(LogGF, Warning, TEXT("WASAPI: output device lost (0x%08x)"), static_cast<uint32>(Hr));
	}
	Cleanup();
	SetStatus(bLost ? EGFCaptureStatus::DeviceLost : EGFCaptureStatus::Idle);
	return bLost ? 1 : 0;
#else
	SetStatus(EGFCaptureStatus::Failed);
	return 1;
#endif
}
