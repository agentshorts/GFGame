#include "GFElder.h"
#include "GFPlayerController.h"

AGFElder::AGFElder()
{
	TownsfolkRole = EGFTownsfolkRole::Elder;
}

FString AGFElder::LedgerKey() const
{
	return FString::Printf(TEXT("elder:%s"), *TownId.ToString());
}

void AGFElder::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		OnInteract.AddUObject(this, &AGFElder::HandleTalk);
	}
}

void AGFElder::HandleTalk(AActor* Townsfolk, APawn* Pawn)
{
	(void)Townsfolk;
	AGFPlayerController* PC = Pawn ? Cast<AGFPlayerController>(Pawn->GetController()) : nullptr;
	if (PC)
	{
		PC->OpenElderMenu(this);
	}
}
