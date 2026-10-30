#include "GFBeatHopGateSubsystem.h"
#include "GFBuiltInSongs.h"

TArray<FName> UGFBeatHopGateSubsystem::GetBuiltInSongIds() const
{
	return FGFBuiltInSongs::SongIds();
}

bool UGFBeatHopGateSubsystem::CanStartRun(EGFBeatHopMode Mode, int32 NumPlayers, FName SongId, EGFBeatDifficulty Difficulty, FString& OutReason) const
{
	OutReason.Reset();
	return FGFBeatHopSessionRules::CanStart(Mode, NumPlayers, SongId, Difficulty, &OutReason);
}
