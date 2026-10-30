#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GFNetTypes.h"
#include "GFLocalProfileSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FGFOnCharacterStored, const FGFCharacterData&);

/**
 * This machine's own character (PLAN.md section 2a: "your stuff is yours"). Uploaded to whichever world you
 * join; the host writes changes back here. GFSave persists it as UGFCharacterSave.
 */
UCLASS()
class GFNET_API UGFLocalProfileSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	const FGFCharacterData& GetCharacter() const { return Character; }
	void SetCharacter(const FGFCharacterData& Data) { Character = Data; }

	/** Write-back from the server. Ignores anything older than what we have. */
	void StoreFromServer(const FGFCharacterData& Data);

	int32 NumWriteBacks() const { return WriteBacks; }

	FGFOnCharacterStored OnCharacterStored;

private:
	FGFCharacterData Character;
	int32 WriteBacks = 0;
};
