#include "GFTownNodes.h"
#include "GFLog.h"
#include "GFMagicComponent.h"
#include "GFMagicShapes.h"
#include "GFMagicTables.h"
#include "GFMineable.h"
#include "GFPlayerState.h"
#include "GFVoxelTypes.h"
#include "GFVoxelWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/Material.h"
#include "Net/UnrealNetwork.h"

namespace
{
	void AddUnique(TArray<FString>& List, const FString& Key)
	{
		if (!Key.IsEmpty() && !List.Contains(Key))
		{
			List.Add(Key);
		}
	}

	FString CasterKey(const UGFMagicComponent* Caster)
	{
		if (!Caster)
		{
			return FString();
		}
		const FName Key = Caster->ContributorKey();
		return Key.IsNone() ? FString() : Key.ToString();
	}

	enum class ETownTrace : uint8
	{
		Miss,
		Blocked,
		Node
	};

	struct FTownHit
	{
		UPrimitiveComponent* Component = nullptr;
		FName BlockId;
		FVector Impact = FVector::ZeroVector;
		float Distance = 0.f;
		bool bCrystal = false;
	};

	ETownTrace TraceTown(UWorld* World, AGFPlayerState* PS, const FVector& Start, const FVector& End, bool bVoxelBlocks, FTownHit& Out)
	{
		if (!World || FVector::DistSquared(Start, End) <= 1.f)
		{
			return ETownTrace::Miss;
		}
		FCollisionQueryParams Params = FGFMineable::QueryParams(TEXT("GFTownMine"));
		if (PS)
		{
			if (APawn* Pawn = PS->GetPawn())
			{
				Params.AddIgnoredActor(Pawn);
			}
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return ETownTrace::Miss;
		}
		if (AActor* Actor = Hit.GetActor())
		{
			if (Actor->IsA(AGFVoxelWorld::StaticClass()))
			{
				return bVoxelBlocks ? ETownTrace::Blocked : ETownTrace::Miss;
			}
		}
		UPrimitiveComponent* Comp = Hit.GetComponent();
		if (!Comp)
		{
			return ETownTrace::Blocked;
		}
		FString Why;
		const bool bOk = PS ? PS->CanMineHit(Hit, Why) : FGFMineable::CanMineHit(Hit, Why);
		if (!bOk)
		{
			return ETownTrace::Blocked;
		}
		const UGFMineableAssetUserData* Data = FGFMineable::Find(FGFMineable::MeshOf(Comp));
		const FName BlockId = FGFTownNodeRules::BlockIdFor(Data);
		if (BlockId.IsNone())
		{
			return ETownTrace::Blocked;
		}
		Out.Component = Comp;
		Out.BlockId = BlockId;
		Out.Impact = Hit.ImpactPoint;
		Out.Distance = Hit.Distance;
		Out.bCrystal = Data && Data->Mode == EGFMineableMode::Slots;
		return ETownTrace::Node;
	}

	void FillCommon(FMagicHitRequest& Request, UGFMagicComponent* Caster, EGFMagicElement Element)
	{
		Request.Element = Element;
		Request.Dt = GFMagic::TickSeconds;
		Request.Rate = Caster ? FMath::Max(0.f, Caster->FieldRate()) : 1.f;
		Request.Budget = 1;
		Request.Cells.Reset();
		Request.Cells.Add(FIntVector::ZeroValue);
		if (!Caster)
		{
			return;
		}
		const FGFMagicModifiers Mods = Caster->Modifiers();
		Request.ExtraDrop = Mods.ExtraDrop;
		Request.bJackpot = Mods.bJackpot;
		Request.SmeltChance = Mods.SmeltChance;
		const FString Key = CasterKey(Caster);
		if (!Key.IsEmpty())
		{
			Request.Contributors.Add(Key);
		}
	}

	TArray<FVector> ConeEnds(const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, int32 Count)
	{
		TArray<FVector> Ends;
		const FVector N = Dir.GetSafeNormal();
		const float Reach = FMath::Max(1.f, Range);
		Ends.Add(Origin + N * Reach);
		if (Count <= 1)
		{
			return Ends;
		}
		const FRotationMatrix Basis(N.ToOrientationRotator());
		const FVector Right = Basis.GetUnitAxis(EAxis::Y);
		const FVector Up = Basis.GetUnitAxis(EAxis::Z);
		const float Radius = Reach * FMath::Tan(FMath::DegreesToRadians(FMath::Max(0.f, HalfAngleDeg)));
		const int32 Extra = Count - 1;
		for (int32 Index = 0; Index < Extra; ++Index)
		{
			const float Angle = 2.f * PI * static_cast<float>(Index) / static_cast<float>(Extra);
			const float Ring = (Index % 3 == 0) ? 1.f : 0.55f;
			Ends.Add(Origin + N * Reach + (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle)) * Radius * Ring);
		}
		return Ends;
	}
}

AGFTownMineRelay::AGFTownMineRelay()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
}

void AGFTownMineRelay::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(!HasAuthority());
}

void AGFTownMineRelay::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGFTownMineRelay, Nodes);
}

void AGFTownMineRelay::SetNodes(const TArray<FGFTownNodeRep>& In)
{
	if (Nodes.Num() == In.Num())
	{
		bool bSame = true;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FGFTownNodeRep& A = Nodes[Index];
			const FGFTownNodeRep& B = In[Index];
			if (A.Key != B.Key || A.bDepleted != B.bDepleted || A.bCrystal != B.bCrystal || A.Actor != B.Actor)
			{
				bSame = false;
				break;
			}
		}
		if (bSame)
		{
			return;
		}
	}
	Nodes = In;
	ForceNetUpdate();
}

void AGFTownMineRelay::OnRep_Nodes()
{
	TSet<FString> Live;
	for (const FGFTownNodeRep& Node : Nodes)
	{
		Live.Add(Node.Key);
		ApplyOne(Node, true);
	}
	for (const FGFTownNodeRep& Old : Applied)
	{
		if (!Live.Contains(Old.Key))
		{
			ApplyOne(Old, false);
		}
	}
	Applied = Nodes;
}

void AGFTownMineRelay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		return;
	}
	for (const FGFTownNodeRep& Node : Nodes)
	{
		ApplyOne(Node, true);
	}
}

void AGFTownMineRelay::ApplyOne(const FGFTownNodeRep& Node, bool bDepleted) const
{
	AActor* Target = Node.Actor.Get();
	if (!Target)
	{
		Target = FGFTownNodeRules::FindActor(GetWorld(), Node.Key);
	}
	FGFTownNodeRules::ApplyVisual(Target, bDepleted, Node.bCrystal);
}

FName FGFTownNodeRules::BlockIdFor(const UGFMineableAssetUserData* Data)
{
	if (!Data)
	{
		return NAME_None;
	}
	if (Data->Mode == EGFMineableMode::WholeMesh)
	{
		return TEXT("Log");
	}
	if (Data->Mode == EGFMineableMode::Slots)
	{
		return TEXT("CrystalVein");
	}
	return NAME_None;
}

void FGFTownNodeRules::DropTable(FName BlockId, TArray<FGFRewardEntry>& Out)
{
	Out.Reset();
	const FName Item = FGFMagicCatalog::BlockDrop(BlockId);
	if (Item.IsNone())
	{
		return;
	}
	FGFRewardEntry Entry;
	Entry.ItemId = Item;
	Entry.Min = 1;
	Entry.Max = 1;
	Entry.Chance = 1.f;
	Out.Add(Entry);
}

FString FGFTownNodeRules::KeyFor(const AActor* Actor)
{
	if (!Actor)
	{
		return FString();
	}
	FString Name = Actor->GetName();
	if (Name.StartsWith(TEXT("UEDPIE_")))
	{
		int32 First = INDEX_NONE;
		if (Name.FindChar(TEXT('_'), First))
		{
			const int32 Second = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, First + 1);
			if (Second != INDEX_NONE)
			{
				Name = Name.Mid(Second + 1);
			}
		}
	}
	return Name;
}

FString FGFTownNodeRules::KeyFor(const UPrimitiveComponent* Component)
{
	if (!Component)
	{
		return FString();
	}
	if (const AActor* Owner = Component->GetOwner())
	{
		return KeyFor(Owner);
	}
	return Component->GetName();
}

AActor* FGFTownNodeRules::FindActor(UWorld* World, const FString& Key)
{
	if (!World || Key.IsEmpty())
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (KeyFor(*It) == Key)
		{
			return *It;
		}
	}
	return nullptr;
}

void FGFTownNodeRules::ApplyVisual(AActor* Actor, bool bDepleted, bool bCrystal)
{
	if (!Actor)
	{
		return;
	}
	if (bCrystal)
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}
			if (bDepleted)
			{
				UMaterialInterface* Body = Mesh->GetMaterial(0);
				if (!Body)
				{
					Body = UMaterial::GetDefaultMaterial(MD_Surface);
				}
				Mesh->SetMaterial(1, Body);
			}
			else if (UStaticMesh* Asset = Mesh->GetStaticMesh())
			{
				Mesh->SetMaterial(1, Asset->GetMaterial(1));
			}
		}
		return;
	}
	Actor->SetActorHiddenInGame(bDepleted);
	Actor->SetActorEnableCollision(!bDepleted);
}

FMagicHitOutput FGFTownNodeRules::ApplyHit(FMagicWound& Wound, const FMagicHitRequest& Request, FName BlockId)
{
	const FIntVector Cell = FIntVector::ZeroValue;
	TMap<FIntVector, FMagicWound> Wounds;
	Wounds.Add(Cell, Wound);
	FMagicHitRequest Req = Request;
	Req.Cells.Reset();
	Req.Cells.Add(Cell);
	Req.Budget = 1;
	FMagicWorldView View;
	View.BlockAt = [BlockId](const FIntVector& At) { return At == FIntVector::ZeroValue ? BlockId : NAME_None; };
	View.IsPlaced = [](const FIntVector&) { return false; };
	View.IsBlockedLand = [](const FIntVector&) { return false; };
	View.Hardness = [](FName Id) { return FGFMagicCatalog::BlockHardness(Id); };
	View.DropOf = [](FName Id) { return FGFMagicCatalog::BlockDrop(Id); };
	View.Affinity = [](FName Id) { return FGFMagicCatalog::AffinityOrDefault(Id); };
	const FMagicHitOutput Out = FGFMagicRules::Apply(Wounds, Req, View);
	if (const FMagicWound* Left = Wounds.Find(Cell))
	{
		Wound = *Left;
	}
	else
	{
		Wound = FMagicWound();
	}
	return Out;
}

void FGFTownNodeRules::Grant(UGFMagicComponent* Who, const FMagicBreak& Break, const FVector& From, double Now)
{
	if (!Who || Break.DropCount <= 0 || !Break.TransformTo.IsNone())
	{
		return;
	}
	Who->NoteBreak(Now);
	TArray<FGFRewardEntry> Table;
	DropTable(Break.BlockId, Table);
	if (Table.Num() > 0 && !Break.DropItem.IsNone())
	{
		Table[0].ItemId = Break.DropItem;
	}
	TArray<FGFItemStack> Stacks;
	FRandomStream Rng(0);
	FGFRewards::Roll(Table, Rng, static_cast<float>(Break.DropCount), Stacks);
	for (const FGFItemStack& Stack : Stacks)
	{
		Who->ReceiveDrop(Stack.ItemId, Stack.Count, From);
	}
}

void FGFTownNodeField::Reset()
{
	if (AGFTownMineRelay* Live = Relay.Get())
	{
		Live->Destroy();
	}
	Relay = nullptr;
	Nodes.Empty();
	Smears.Empty();
	bDirty = false;
}

void FGFTownNodeField::SetRegrowSeconds(float Seconds)
{
	RegrowSeconds = FMath::Max(0.f, Seconds);
}

float FGFTownNodeField::Remaining(const UPrimitiveComponent* Component) const
{
	if (const FNode* Node = Find(Component))
	{
		if (Node->bDepleted)
		{
			return 0.f;
		}
		if (Node->Wound.bStarted)
		{
			return Node->Wound.Remaining;
		}
		return FGFMagicRules::IntegrityOf(FGFMagicCatalog::BlockHardness(Node->BlockId));
	}
	const UGFMineableAssetUserData* Data = FGFMineable::Find(FGFMineable::MeshOf(Component));
	const FName BlockId = FGFTownNodeRules::BlockIdFor(Data);
	if (BlockId.IsNone())
	{
		return -1.f;
	}
	return FGFMagicRules::IntegrityOf(FGFMagicCatalog::BlockHardness(BlockId));
}

bool FGFTownNodeField::IsDepleted(const UPrimitiveComponent* Component) const
{
	const FNode* Node = Find(Component);
	return Node && Node->bDepleted;
}

const FGFTownNodeField::FNode* FGFTownNodeField::Find(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		return nullptr;
	}
	return Nodes.Find(FGFTownNodeRules::KeyFor(Component));
}

FGFTownNodeField::FNode& FGFTownNodeField::FindOrAdd(const UPrimitiveComponent* Component, FName BlockId, bool bCrystal, const FVector& Impact)
{
	const FString Key = FGFTownNodeRules::KeyFor(Component);
	FNode& Node = Nodes.FindOrAdd(Key);
	if (Node.Key.IsEmpty())
	{
		Node.Key = Key;
		Node.Component = const_cast<UPrimitiveComponent*>(Component);
		Node.Actor = Component ? Component->GetOwner() : nullptr;
		Node.BlockId = BlockId;
		Node.bCrystal = bCrystal;
	}
	Node.LastImpact = Impact;
	return Node;
}

void FGFTownNodeField::CommitBreak(FNode& Node, const FMagicBreak& Break, const FVector& At, double Now, UGFMagicComponent* Fallback, TFunctionRef<UGFMagicComponent*(const FString&)> FindCaster)
{
	if (Node.bDepleted)
	{
		return;
	}
	Node.bDepleted = true;
	Node.RegrowAt = Now + static_cast<double>(RegrowSeconds);
	Node.Wound = FMagicWound();
	Node.Freeze = FFreeze();
	FGFTownNodeRules::ApplyVisual(Node.Actor.Get(), true, Node.bCrystal);
	bDirty = true;
	bool bPaid = false;
	for (const FString& Key : Break.Contributors)
	{
		if (UGFMagicComponent* Who = FindCaster(Key))
		{
			FGFTownNodeRules::Grant(Who, Break, At, Now);
			bPaid = true;
		}
	}
	if (!bPaid && Fallback)
	{
		FGFTownNodeRules::Grant(Fallback, Break, At, Now);
	}
	UE_LOG(LogGF, Log, TEXT("Town node %s broke, drop %s x%d, regrow in %.0fs"), *Node.Key, *Break.DropItem.ToString(), Break.DropCount, RegrowSeconds);
}

void FGFTownNodeField::Publish(UWorld* World)
{
	if (!bDirty || !World)
	{
		return;
	}
	bDirty = false;
	AGFTownMineRelay* Live = Relay.Get();
	if (!Live)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Live = World->SpawnActor<AGFTownMineRelay>(AGFTownMineRelay::StaticClass(), FTransform::Identity, Params);
		Relay = Live;
	}
	if (!Live)
	{
		return;
	}
	TArray<FGFTownNodeRep> Next;
	for (const TPair<FString, FNode>& Pair : Nodes)
	{
		if (!Pair.Value.bDepleted)
		{
			continue;
		}
		FGFTownNodeRep Rep;
		Rep.Key = Pair.Key;
		Rep.bDepleted = true;
		Rep.bCrystal = Pair.Value.bCrystal;
		if (AActor* Actor = Pair.Value.Actor.Get())
		{
			if (Actor->GetIsReplicated())
			{
				Rep.Actor = Actor;
			}
		}
		Next.Add(Rep);
	}
	Next.Sort([](const FGFTownNodeRep& A, const FGFTownNodeRep& B) { return A.Key < B.Key; });
	Live->SetNodes(Next);
}

void FGFTownNodeField::CastShapes(UGFMagicComponent* Caster, int32& Budget, double Now)
{
	if (!Caster || !Caster->IsFieldHeld())
	{
		return;
	}
	UWorld* World = Caster->GetWorld();
	if (!World)
	{
		return;
	}
	const FGFMiningSpellRow* Spell = FGFMagicCatalog::FieldSpell(Caster->GetElement());
	if (!Spell)
	{
		return;
	}
	const FName Shape = Spell->Shape.IsNone() ? FName(TEXT("Cone")) : Spell->Shape;
	if (Shape == TEXT("Melee") || Shape == TEXT("Spray") || Shape == TEXT("Claw"))
	{
		return;
	}
	AGFPlayerState* PS = Cast<AGFPlayerState>(Caster->GetOwner());
	const FVector Origin = Caster->CastOrigin();
	FVector Dir = (Caster->GetAimPoint() - Origin).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const float Range = Caster->FieldRange();
	if (Shape == TEXT("Freeze"))
	{
		FTownHit Hit;
		if (TraceTown(World, PS, Origin, Origin + Dir * Range, true, Hit) != ETownTrace::Node)
		{
			return;
		}
		FNode& Node = FindOrAdd(Hit.Component, Hit.BlockId, Hit.bCrystal, Hit.Impact);
		if (Node.bDepleted || Node.Freeze.bLocked)
		{
			return;
		}
		const FGFBlockAffinityRow Affinity = FGFMagicCatalog::AffinityOrDefault(Node.BlockId);
		const float Hardness = FMath::Max(0.05f, FGFMagicCatalog::BlockHardness(Node.BlockId));
		const float FreezeRate = Spell->FreezeRate > 0.f ? Spell->FreezeRate : 1.f;
		Node.Freeze.Progress += FreezeRate * FMath::Max(0.f, Affinity.Frost) / Hardness * GFMagic::TickSeconds * Caster->FieldRate();
		AddUnique(Node.Freeze.Contributors, CasterKey(Caster));
		Node.Freeze.Bonus = FMath::Max(Node.Freeze.Bonus, Caster->Modifiers().FrostBonus);
		if (Node.Freeze.Progress >= 1.f)
		{
			Node.Freeze.bLocked = true;
			Node.Freeze.Progress = 1.f;
			const float Contract = Spell->ContractionSeconds > 0.f ? Spell->ContractionSeconds : 1.2f;
			Node.Freeze.ShatterAt = Now + Contract;
		}
		return;
	}

	if (Budget <= 0)
	{
		return;
	}
	const int32 Rays = FGFMagicShapes::ConeRayCount(Range, Spell->ConeHalfAngle, GFBlockSize, Spell->MinRays, Spell->MaxRays);
	TMap<UPrimitiveComponent*, FTownHit> Best;
	for (const FVector& End : ConeEnds(Origin, Dir, Range, Spell->ConeHalfAngle, Rays))
	{
		FTownHit Hit;
		if (TraceTown(World, PS, Origin, End, true, Hit) != ETownTrace::Node || !Hit.Component)
		{
			continue;
		}
		if (FTownHit* Have = Best.Find(Hit.Component))
		{
			if (Hit.Distance < Have->Distance)
			{
				*Have = Hit;
			}
		}
		else
		{
			Best.Add(Hit.Component, Hit);
		}
	}
	TArray<FTownHit> Ordered;
	Best.GenerateValueArray(Ordered);
	Ordered.Sort([](const FTownHit& A, const FTownHit& B) { return A.Distance < B.Distance; });
	for (const FTownHit& Hit : Ordered)
	{
		if (Budget <= 0)
		{
			break;
		}
		FNode& Node = FindOrAdd(Hit.Component, Hit.BlockId, Hit.bCrystal, Hit.Impact);
		if (Node.bDepleted)
		{
			continue;
		}
		FMagicHitRequest Request;
		FillCommon(Request, Caster, Caster->GetElement());
		const float Power = Spell->ConePower > 0.f ? Spell->ConePower : Spell->IntegrityPerSecond;
		Request.IntegrityPerSecond = Power;
		const float Scale = FGFMagicShapes::SparkFalloff(Hit.Distance, Spell->FalloffStartCm, Range, Spell->FalloffMin);
		Request.CellScale.Add(FIntVector::ZeroValue, Scale);
		const FMagicHitOutput Out = FGFTownNodeRules::ApplyHit(Node.Wound, Request, Node.BlockId);
		Budget = FMath::Max(0, Budget - Out.Spent);
		if (Out.Breaks.Num() > 0)
		{
			// The hitter is Fallback, so the drop is paid once. The contributor list is the same person.
			CommitBreak(Node, Out.Breaks[0], Hit.Impact, Now, Caster, [](const FString&) { return nullptr; });
		}
	}
}

void FGFTownNodeField::HitSwing(UGFMagicComponent* Caster, const FMagicSwing& Swing, int32& Budget, double Now)
{
	if (!Caster || Budget <= 0)
	{
		return;
	}
	UWorld* World = Caster->GetWorld();
	if (!World)
	{
		return;
	}
	FVector Facing = Swing.bPitch ? Swing.AimDir : Swing.Forward;
	if (Facing.IsNearlyZero())
	{
		Facing = FVector::ForwardVector;
	}
	Facing = Facing.GetSafeNormal();
	const FVector Origin = Caster->CastOrigin() + Facing * FMath::Max(0.f, Swing.LungeCm);
	FVector Right = FVector::CrossProduct(FVector::UpVector, Facing).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}
	const FVector Up = FVector::UpVector;
	const int32 Width = FMath::Max(1, Swing.Width);
	const int32 Height = FMath::Max(1, Swing.Height);
	const float DepthCm = static_cast<float>(FMath::Max(1, Swing.Depth)) * GFBlockSize;
	AGFPlayerState* PS = Cast<AGFPlayerState>(Caster->GetOwner());
	TMap<UPrimitiveComponent*, FTownHit> Best;
	for (int32 X = 0; X < Width; ++X)
	{
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const float Ox = (static_cast<float>(X) - (static_cast<float>(Width) - 1.f) * 0.5f) * GFBlockSize;
			const float Oy = static_cast<float>(Y) * GFBlockSize;
			const FVector Start = Origin + Right * Ox + Up * Oy;
			FTownHit Hit;
			if (TraceTown(World, PS, Start, Start + Facing * DepthCm, true, Hit) != ETownTrace::Node || !Hit.Component)
			{
				continue;
			}
			if (FTownHit* Have = Best.Find(Hit.Component))
			{
				if (Hit.Distance < Have->Distance)
				{
					*Have = Hit;
				}
			}
			else
			{
				Best.Add(Hit.Component, Hit);
			}
		}
	}
	for (const TPair<UPrimitiveComponent*, FTownHit>& Pair : Best)
	{
		if (Budget <= 0)
		{
			break;
		}
		const FTownHit& Hit = Pair.Value;
		FNode& Node = FindOrAdd(Hit.Component, Hit.BlockId, Hit.bCrystal, Hit.Impact);
		if (Node.bDepleted)
		{
			continue;
		}
		FMagicHitRequest Request;
		FillCommon(Request, Caster, EGFMagicElement::Root);
		Request.bImpulse = true;
		Request.ImpulsePower = Swing.Power * (Swing.Rhythm > 0.f ? Swing.Rhythm : 1.f);
		const FMagicHitOutput Out = FGFTownNodeRules::ApplyHit(Node.Wound, Request, Node.BlockId);
		Budget = FMath::Max(0, Budget - Out.Spent);
		if (Out.Breaks.Num() > 0)
		{
			CommitBreak(Node, Out.Breaks[0], Hit.Impact, Now, Caster, [](const FString&) { return nullptr; });
		}
	}
}

bool FGFTownNodeField::ConsumeGlob(UWorld* World, const FMagicGlob& Glob, const FVector& From, const FVector& To, const FGFMiningSpellRow& Spell, double Now, UGFMagicComponent* Owner)
{
	AGFPlayerState* PS = Owner ? Cast<AGFPlayerState>(Owner->GetOwner()) : nullptr;
	FTownHit Hit;
	const ETownTrace Kind = TraceTown(World, PS, From, To, false, Hit);
	if (Kind == ETownTrace::Miss)
	{
		return false;
	}
	if (Kind == ETownTrace::Blocked)
	{
		return true;
	}
	FNode& Node = FindOrAdd(Hit.Component, Hit.BlockId, Hit.bCrystal, Hit.Impact);
	if (Node.bDepleted)
	{
		return true;
	}
	FSmear Smear;
	Smear.Key = Node.Key;
	const float Life = Spell.SmearSeconds > 0.f ? Spell.SmearSeconds : 2.f;
	Smear.Until = Now + Life;
	const float Dissolve = Spell.PuddleDissolve > 0.f ? Spell.PuddleDissolve : 80.f;
	Smear.PerSecond = 0.5f * Dissolve;
	Smear.Rate = Glob.Rate > 0.f ? Glob.Rate : 1.f;
	if (!Glob.Owner.IsEmpty())
	{
		Smear.Owners.Add(Glob.Owner);
	}
	Smears.Add(Smear);
	return true;
}

void FGFTownNodeField::TickOngoing(UWorld* World, double Now, int32& Budget, TFunctionRef<UGFMagicComponent*(const FString&)> FindCaster)
{
	TArray<FSmear> Kept;
	for (const FSmear& Smear : Smears)
	{
		if (Now > Smear.Until)
		{
			continue;
		}
		FNode* Node = Nodes.Find(Smear.Key);
		if (!Node || Node->bDepleted || !Node->Component.IsValid())
		{
			continue;
		}
		if (Budget <= 0)
		{
			Kept.Add(Smear);
			continue;
		}
		UGFMagicComponent* Owner = nullptr;
		float Smelt = 0.f;
		for (const FString& Key : Smear.Owners)
		{
			if (UGFMagicComponent* Who = FindCaster(Key))
			{
				Smelt = FMath::Max(Smelt, Who->Modifiers().SmeltChance);
				if (!Owner)
				{
					Owner = Who;
				}
			}
		}
		FMagicHitRequest Request;
		FillCommon(Request, Owner, EGFMagicElement::Magma);
		Request.Contributors = Smear.Owners;
		Request.bImpulse = true;
		Request.ImpulsePower = Smear.PerSecond * GFMagic::TickSeconds;
		Request.Rate = Smear.Rate > 0.f ? Smear.Rate : 1.f;
		Request.SmeltChance = Smelt;
		const FMagicHitOutput Out = FGFTownNodeRules::ApplyHit(Node->Wound, Request, Node->BlockId);
		Budget = FMath::Max(0, Budget - Out.Spent);
		if (Out.Breaks.Num() > 0)
		{
			CommitBreak(*Node, Out.Breaks[0], Node->LastImpact, Now, Owner, FindCaster);
		}
		else
		{
			Kept.Add(Smear);
		}
	}
	Smears = MoveTemp(Kept);

	for (TPair<FString, FNode>& Pair : Nodes)
	{
		FNode& Node = Pair.Value;
		if (Node.bDepleted || !Node.Freeze.bLocked || Node.Freeze.ShatterAt > Now + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		if (Budget <= 0)
		{
			break;
		}
		const FGFBlockAffinityRow Affinity = FGFMagicCatalog::AffinityOrDefault(Node.BlockId);
		FMagicBreak Break;
		Break.BlockId = Node.BlockId;
		Break.Contributors = Node.Freeze.Contributors;
		if (!Affinity.FrostTransform.IsNone())
		{
			Break.TransformTo = Affinity.FrostTransform;
			Break.DropCount = 0;
		}
		else
		{
			Break.DropItem = FGFMagicCatalog::BlockDrop(Node.BlockId);
			const int32 Bonus = (Node.Freeze.Bonus > 0 && Affinity.FreezeSpread >= 0.999f) ? Node.Freeze.Bonus : 0;
			Break.DropCount = 1 + Bonus;
		}
		UGFMagicComponent* Owner = Break.Contributors.Num() > 0 ? FindCaster(Break.Contributors[0]) : nullptr;
		CommitBreak(Node, Break, Node.LastImpact, Now, Owner, FindCaster);
		Budget = FMath::Max(0, Budget - 1);
	}

	for (TPair<FString, FNode>& Pair : Nodes)
	{
		FNode& Node = Pair.Value;
		if (!Node.bDepleted || Now + KINDA_SMALL_NUMBER < Node.RegrowAt)
		{
			continue;
		}
		Node.bDepleted = false;
		Node.Wound = FMagicWound();
		Node.Freeze = FFreeze();
		FGFTownNodeRules::ApplyVisual(Node.Actor.Get(), false, Node.bCrystal);
		bDirty = true;
		UE_LOG(LogGF, Log, TEXT("Town node %s regrew"), *Node.Key);
	}
	Publish(World);
}
