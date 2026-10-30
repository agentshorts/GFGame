#include "GFRhythmSettings.h"

UGFRhythmSettings::UGFRhythmSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("BeatHop");
}

void UGFRhythmSettings::SetListenDevice(const FString& EndpointIdOrEmptyForDefault)
{
	ListenDeviceId = EndpointIdOrEmptyForDefault;
}

float UGFRhythmSettings::GetLiveLatencyMs() const
{
	const float* Found = LiveLatencyMsByDevice.Find(ListenDeviceId);
	return Found ? FMath::Clamp(*Found, -400.f, 400.f) : 0.f;
}

void UGFRhythmSettings::SaveSettings()
{
	SaveConfig();
}
