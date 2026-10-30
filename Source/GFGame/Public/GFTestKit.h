#pragma once

// Dev test kit for playtests: a few blocks, crafting materials, one outfit item and a ferry pass.
// Console: gf.TestKit, and gf.Ferry.Ride to board at the nearest landing (the ferry prompt has no key yet). Auto-grant at spawn with -GFTestKit or bAutoGrantTestKit in [/Script/GFGame.GFTestKitSubsystem].
// Every grant tops items up to a target count, so granting twice adds nothing. Not compiled into Shipping.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFItemTypes.h"
#include "GFTestKit.generated.h"

class AGFPlayerState;
class UWorld;

struct GFGAME_API FGFTestKit
{
	/** Target counts. PassItem is the ferry lifetime pass item (None skips it). */
	static TArray<FGFItemStack> Contents(FName PassItem);

	/** The ferry pass item from the ferry config, or FerryPass when the config has none. */
	static FName DefaultPassItem();

	/** What Inventory still needs to reach the kit's target counts. */
	static TArray<FGFItemStack> Missing(const FGFInventory& Inventory, FName PassItem);

	/** Pure. Tops Inventory up to the kit. Returns the number of items added. */
	static int32 TopUp(FGFInventory& Inventory, FName PassItem);

	/** Server only. Tops this player's inventory up through the inventory component. Returns items added, -1 if not allowed. */
	static int32 Grant(AGFPlayerState* PlayerState);

	/** Server only. Grants every ready player in World. Returns the number of players granted. */
	static int32 GrantAll(UWorld* World);

	/** True when the process was started with -GFTestKit. */
	static bool IsRequestedOnCommandLine();
};

/** Grants the test kit once to each player whose character is ready, when auto-grant is on. */
UCLASS(Config = Game)
class GFGAME_API UGFTestKitSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Dev toggle. Off in the repo; a playtest package turns it on in its own Saved config. */
	UPROPERTY(Config)
	bool bAutoGrantTestKit = false;

	bool IsAutoGrantOn() const;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Poll();

	FTimerHandle PollHandle;
	TSet<TWeakObjectPtr<AGFPlayerState>> Granted;
};
