// Imported-art checks (art batch 8: landlord kit, SK_Player_Base v2 emotes + dye material, co-op UI textures;
// batch 3b: taming/cooking/fishing props; batch 7: arena kit, SK_Player_Base v4, SK_Dino_Raptor v5, arena UI;
// SK_Player_Base v5: Mailbox_Open, Plant_Flag, Emote_Dance, Emote_Laugh; new dinos v1: Tusksaw, Ripwing,
// Scythepaw, Lanternmaw; Big Grumble on the shared SKEL_Dino_Rex).
// They load the committed /Game/Art assets, so they fail if an import is missing or a reference breaks.
#include "Misc/AutomationTest.h"
#include "GFAnimMap.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"

#if WITH_DEV_AUTOMATION_TESTS

static constexpr EAutomationTestFlags GFArtTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtAnimMapTest, "GFGame.Art.AnimMap", GFArtTestFlags)
bool FGFArtAnimMapTest::RunTest(const FString&)
{
	struct FExpect { const TCHAR* Key; const TCHAR* Clip; bool bLoop; bool bHold; };
	const FExpect Rows[] = {
		{ TEXT("Player.Idle"), TEXT("A_Player_Base_Idle"), true, false },
		{ TEXT("Player.Walk"), TEXT("A_Player_Base_Walk"), true, false },
		{ TEXT("Player.Run"), TEXT("A_Player_Base_Run"), true, false },
		{ TEXT("Player.Jump_Start"), TEXT("A_Player_Base_Jump_Start"), false, true },
		{ TEXT("Player.Jump_Loop"), TEXT("A_Player_Base_Jump_Loop"), true, false },
		{ TEXT("Player.Jump_Land"), TEXT("A_Player_Base_Jump_Land"), false, false },
		{ TEXT("Player.Interact"), TEXT("A_Player_Base_Interact"), false, false },
		{ TEXT("Player.Emote_Sit"), TEXT("A_Player_Base_Emote_Sit"), true, false },
		// Brief 4.8 emotes reuse the batch-8 Cheer and Wave clips.
		{ TEXT("Player.Emote_Cheer"), TEXT("A_Player_Base_Cheer"), false, true },
		{ TEXT("Player.Emote_Wave"), TEXT("A_Player_Base_Wave"), false, false },
		// Batch 3b (SK_Player_Base v3) and batch 7 (v4) clips.
		{ TEXT("Player.Offer"), TEXT("A_Player_Base_Offer"), false, false },
		{ TEXT("Player.Crouch_Idle"), TEXT("A_Player_Base_Crouch_Idle"), true, false },
		{ TEXT("Player.Crouch_Walk"), TEXT("A_Player_Base_Crouch_Walk"), true, false },
		{ TEXT("Player.Fish_Cast"), TEXT("A_Player_Base_Fish_Cast"), false, false },
		{ TEXT("Player.Fish_Wait"), TEXT("A_Player_Base_Fish_Wait"), true, false },
		{ TEXT("Player.Fish_Reel"), TEXT("A_Player_Base_Fish_Reel"), false, false },
		{ TEXT("Player.Pet"), TEXT("A_Player_Base_Pet"), false, false },
		{ TEXT("Player.Knockout"), TEXT("A_Player_Base_Knockout"), false, true },
		{ TEXT("Player.Balcony_Idle"), TEXT("A_Player_Base_Balcony_Idle"), true, false },
		{ TEXT("Player.Command_Point"), TEXT("A_Player_Base_Command_Point"), false, false },
		{ TEXT("Player.Cast_Spell"), TEXT("A_Player_Base_Cast_Spell"), false, false },
		{ TEXT("Player.SlowTime_Flip"), TEXT("A_Player_Base_SlowTime_Flip"), false, false },
		{ TEXT("Player.Groan"), TEXT("A_Player_Base_Groan"), false, true },
		// SK_Player_Base v5 clips.
		{ TEXT("Player.Mailbox_Open"), TEXT("A_Player_Base_Mailbox_Open"), false, false },
		{ TEXT("Player.Plant_Flag"), TEXT("A_Player_Base_Plant_Flag"), false, false },
		{ TEXT("Player.Emote_Dance"), TEXT("A_Player_Base_Emote_Dance"), true, false },
		{ TEXT("Player.Emote_Laugh"), TEXT("A_Player_Base_Emote_Laugh"), false, false },
	};
	USkeleton* Skel = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Characters/Player/SKEL_GF_Humanoid.SKEL_GF_Humanoid"));
	TestNotNull(TEXT("SKEL_GF_Humanoid loads"), Skel);
	for (const FExpect& E : Rows)
	{
		const FGFAnimMapRow* Row = FGFAnimMap::Find(E.Key);
		if (!TestNotNull(FString::Printf(TEXT("%s row"), E.Key), Row))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s loop"), E.Key), Row->bLoop, E.bLoop);
		TestEqual(FString::Printf(TEXT("%s hold last frame"), E.Key), Row->bHoldLastFrame, E.bHold);
		UAnimSequenceBase* Anim = FGFAnimMap::Load(E.Key);
		if (TestNotNull(FString::Printf(TEXT("%s clip loads"), E.Key), Anim))
		{
			TestEqual(FString::Printf(TEXT("%s clip"), E.Key), Anim->GetName(), FString(E.Clip));
			TestTrue(FString::Printf(TEXT("%s on SKEL_GF_Humanoid"), E.Key), Skel && Anim->GetSkeleton() == Skel);
			TestTrue(FString::Printf(TEXT("%s has length"), E.Key), Anim->GetPlayLength() > 0.f);
		}
	}
	TestTrue(TEXT("Emote_Cheer and Emote_Wave are different clips"), FGFAnimMap::Load(TEXT("Player.Emote_Cheer")) != FGFAnimMap::Load(TEXT("Player.Emote_Wave")));
	TestNull(TEXT("Unknown key"), FGFAnimMap::Find(TEXT("Player.Emote_NotAClip")));
	// The planted claim flag sinks 10 cm (the clip's pole foot only reaches the ground); no other clip leaves a prop.
	for (const FExpect& E : Rows)
	{
		const FGFAnimMapRow* Row = FGFAnimMap::Find(E.Key);
		const float Want = FCString::Strcmp(E.Key, TEXT("Player.Plant_Flag")) == 0 ? -10.f : 0.f;
		TestEqual(FString::Printf(TEXT("%s planted prop offset"), E.Key), Row ? Row->PlantedPropOffsetZ : 1.f, Want);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtPlayerMaterialTest, "GFGame.Art.PlayerMaterial", GFArtTestFlags)
bool FGFArtPlayerMaterialTest::RunTest(const FString&)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Characters/Player/SK_Player_Base.SK_Player_Base"));
	if (!TestNotNull(TEXT("SK_Player_Base loads"), Mesh))
	{
		return false;
	}
	const TArray<FSkeletalMaterial>& Mats = Mesh->GetMaterials();
	TestEqual(TEXT("3 slots"), Mats.Num(), 3);
	for (const FSkeletalMaterial& M : Mats)
	{
		const FString Slot = M.MaterialSlotName.ToString();
		UMaterialInstance* MI = Cast<UMaterialInstance>(M.MaterialInterface);
		if (!TestNotNull(FString::Printf(TEXT("Slot %s has a material instance"), *Slot), MI))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("Slot %s instance"), *Slot), MI->GetName(), TEXT("MI_Player_") + Slot);
		TestEqual(FString::Printf(TEXT("Slot %s parent"), *Slot), MI->GetMaterial() ? MI->GetMaterial()->GetName() : FString(), FString(TEXT("M_GF_DyeMask")));
		UTexture* D = nullptr;
		UTexture* Mask = nullptr;
		TestTrue(FString::Printf(TEXT("Slot %s Diffuse param"), *Slot), MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Diffuse")), D) && D && D->GetName() == TEXT("T_Player_") + Slot + TEXT("_D"));
		TestTrue(FString::Printf(TEXT("Slot %s Mask param"), *Slot), MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Mask")), Mask) && Mask && Mask->GetName() == TEXT("T_Player_") + Slot + TEXT("_M"));
		if (Mask)
		{
			TestFalse(FString::Printf(TEXT("Slot %s mask is linear"), *Slot), Mask->SRGB);
		}
	}
	// Basewear_M v2: R = tank dye, B = shorts dye (was G in v1), G = player-colour trim (applied last).
	UMaterial* Master = LoadObject<UMaterial>(nullptr, TEXT("/Game/Art/Characters/Player/Materials/M_GF_DyeMask.M_GF_DyeMask"));
	if (TestNotNull(TEXT("M_GF_DyeMask loads"), Master))
	{
		for (const TCHAR* P : { TEXT("Dye_R"), TEXT("Dye_G"), TEXT("Dye_B") })
		{
			FLinearColor C;
			TestTrue(FString::Printf(TEXT("Master has %s"), P), Master->GetVectorParameterValue(FMaterialParameterInfo(P), C));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtBatch8Test, "GFGame.Art.Batch8", GFArtTestFlags)
bool FGFArtBatch8Test::RunTest(const FString&)
{
	const TCHAR* Props[] = {
		TEXT("/Game/Art/Landlord/SM_Land_Board"), TEXT("/Game/Art/Landlord/SM_Rent_Board"),
		TEXT("/Game/Art/Landlord/SM_Mailbox"), TEXT("/Game/Art/Landlord/SM_Mailbox_Flag"),
		TEXT("/Game/Art/Landlord/SM_Plot_ClaimFlag"), TEXT("/Game/Art/Landlord/SM_Plot_CornerPost"),
		TEXT("/Game/Art/Landlord/SM_Plot_BorderSegment"), TEXT("/Game/Art/Landlord/SM_Plot_Signpost"),
		TEXT("/Game/Art/Landlord/SM_Sign_ForRent"), TEXT("/Game/Art/Landlord/SM_Sign_RentDue"),
		TEXT("/Game/Art/Landlord/SM_Sign_RentDue_Sleepy"),
		TEXT("/Game/Art/Landlord/Overgrowth/SM_Overgrowth_Vine_A"), TEXT("/Game/Art/Landlord/Overgrowth/SM_Overgrowth_Vine_B"),
		TEXT("/Game/Art/Landlord/Overgrowth/SM_Overgrowth_Vine_C"),
		TEXT("/Game/Art/Items/SM_Item_Letter"), TEXT("/Game/Art/Items/SM_Item_Parcel"),
	};
	int32 Loaded = 0;
	for (const TCHAR* P : Props)
	{
		const FString Path = FString(P) + TEXT(".") + FPaths::GetBaseFilename(P);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *Path);
		if (TestNotNull(FString::Printf(TEXT("%s loads"), P), M))
		{
			++Loaded;
			TestTrue(FString::Printf(TEXT("%s has collision"), P), M->GetBodySetup() && M->GetBodySetup()->AggGeom.GetElementCount() > 0);
		}
	}
	TestEqual(TEXT("16 landlord props"), Loaded, 16);

	// Co-op UI: UserInterface2D, UI group, no mips (non-power-of-two is fine), nearest, masks linear.
	TArray<FAssetData> Found;
	IAssetRegistry::GetChecked().ScanPathsSynchronous({ TEXT("/Game/Art/UI/Coop") }, true);
	IAssetRegistry::GetChecked().GetAssetsByPath(TEXT("/Game/Art/UI/Coop"), Found, false);
	TestEqual(TEXT("54 UI textures in /Game/Art/UI/Coop"), Found.Num(), 54);
	int32 Bad = 0;
	for (const FAssetData& A : Found)
	{
		UTexture2D* T = Cast<UTexture2D>(A.GetAsset());
		if (!T)
		{
			AddError(A.AssetName.ToString() + TEXT(" is not a Texture2D"));
			++Bad;
			continue;
		}
		const bool bMask = T->GetName().EndsWith(TEXT("_M"));
		bool bOk = T->CompressionSettings == TC_EditorIcon && T->LODGroup == TEXTUREGROUP_UI && T->Filter == TF_Nearest && T->SRGB == !bMask;
#if WITH_EDITORONLY_DATA
		bOk = bOk && T->MipGenSettings == TMGS_NoMipmaps;
		if (T->GetName() == TEXT("T_UI_LoadingCard_Arriving"))
		{
			TestEqual(TEXT("Loading card width"), (int32)T->Source.GetSizeX(), 480);
			TestEqual(TEXT("Loading card height"), (int32)T->Source.GetSizeY(), 270);
		}
#endif
		if (!bOk)
		{
			AddError(T->GetName() + TEXT(" has wrong UI texture settings"));
			++Bad;
		}
	}
	TestEqual(TEXT("UI textures with wrong settings"), Bad, 0);
	return true;
}

static const TCHAR* GFArtBatch3bProps[] = {
	TEXT("/Game/Art/Blocks/SM_Block_Water"),
	TEXT("/Game/Art/Crops/Emberpepper/SM_Crop_Emberpepper_S0"),
	TEXT("/Game/Art/Crops/Emberpepper/SM_Crop_Emberpepper_S1"),
	TEXT("/Game/Art/Crops/Emberpepper/SM_Crop_Emberpepper_S2"),
	TEXT("/Game/Art/Crops/Emberpepper/SM_Crop_Emberpepper_S3"),
	TEXT("/Game/Art/Crops/Steakroot/SM_Crop_Steakroot_S0"),
	TEXT("/Game/Art/Crops/Steakroot/SM_Crop_Steakroot_S1"),
	TEXT("/Game/Art/Crops/Steakroot/SM_Crop_Steakroot_S2"),
	TEXT("/Game/Art/Crops/Steakroot/SM_Crop_Steakroot_S3"),
	TEXT("/Game/Art/Crops/Thornmelon/SM_Crop_Thornmelon_S0"),
	TEXT("/Game/Art/Crops/Thornmelon/SM_Crop_Thornmelon_S1"),
	TEXT("/Game/Art/Crops/Thornmelon/SM_Crop_Thornmelon_S2"),
	TEXT("/Game/Art/Crops/Thornmelon/SM_Crop_Thornmelon_S3"),
	TEXT("/Game/Art/Furniture/SM_Furn_Cookpot"),
	TEXT("/Game/Art/Items/SM_Item_EmberRoast"),
	TEXT("/Game/Art/Items/SM_Item_Emberpepper"),
	TEXT("/Game/Art/Items/SM_Item_FishSkewer"),
	TEXT("/Game/Art/Items/SM_Item_Fizzberry"),
	TEXT("/Game/Art/Items/SM_Item_Glowgrub"),
	TEXT("/Game/Art/Items/SM_Item_GoldenCarp"),
	TEXT("/Game/Art/Items/SM_Item_GrubJelly"),
	TEXT("/Game/Art/Items/SM_Item_JerkyBites"),
	TEXT("/Game/Art/Items/SM_Item_PetitCake"),
	TEXT("/Game/Art/Items/SM_Item_RiverFish"),
	TEXT("/Game/Art/Items/SM_Item_SmokedHaunch"),
	TEXT("/Game/Art/Items/SM_Item_Steakroot"),
	TEXT("/Game/Art/Items/SM_Item_Thornmelon"),
	TEXT("/Game/Art/Items/SM_Item_WildEgg"),
	TEXT("/Game/Art/Nodes/SM_Node_FishingSpot"),
	TEXT("/Game/Art/Nodes/SM_Node_RottenLog"),
	TEXT("/Game/Art/Nodes/SM_Node_RottenLog_Dug"),
	TEXT("/Game/Art/Nodes/SM_Node_WildNest"),
	TEXT("/Game/Art/Nodes/SM_Node_WildNest_Empty"),
	TEXT("/Game/Art/Props/SM_Prop_Bobber"),
	TEXT("/Game/Art/Props/SM_Prop_FishingRod"),
};

static const TCHAR* GFArtBatch7Props[] = {
	TEXT("/Game/Art/Blocks/SM_Block_ArenaSand"),
	TEXT("/Game/Art/Colo/SM_Colo_Balcony"),
	TEXT("/Game/Art/Colo/SM_Colo_Banner"),
	TEXT("/Game/Art/Colo/SM_Colo_Gate"),
	TEXT("/Game/Art/Colo/SM_Colo_GateBars"),
	TEXT("/Game/Art/Colo/SM_Colo_Hourglass"),
	TEXT("/Game/Art/Colo/SM_Colo_Pillar"),
	TEXT("/Game/Art/Colo/SM_Colo_RivalBalcony"),
	TEXT("/Game/Art/Colo/SM_Colo_Stands"),
	TEXT("/Game/Art/Colo/SM_Colo_StartSpot"),
	TEXT("/Game/Art/Colo/SM_Colo_StartSpot_Front"),
	TEXT("/Game/Art/Colo/SM_Colo_Torch"),
	TEXT("/Game/Art/Colo/SM_Colo_Wall_Curve"),
	TEXT("/Game/Art/Colo/SM_Colo_Wall_Straight"),
	TEXT("/Game/Art/Furniture/SM_Deco_CupTrophy_Bronze"),
	TEXT("/Game/Art/Furniture/SM_Deco_CupTrophy_Champion"),
	TEXT("/Game/Art/Furniture/SM_Deco_CupTrophy_Gold"),
	TEXT("/Game/Art/Furniture/SM_Deco_CupTrophy_Silver"),
	TEXT("/Game/Art/Furniture/SM_Deco_TrophyCase"),
	TEXT("/Game/Art/VFX/SM_VFX_BloomPetal"),
	TEXT("/Game/Art/VFX/SM_VFX_ChargeDust"),
	TEXT("/Game/Art/VFX/SM_VFX_EmberFleck"),
	TEXT("/Game/Art/VFX/SM_VFX_FizzBomb"),
	TEXT("/Game/Art/VFX/SM_VFX_RainCloud"),
	TEXT("/Game/Art/VFX/SM_VFX_RainDrop"),
	TEXT("/Game/Art/VFX/SM_VFX_RoarRing"),
	TEXT("/Game/Art/VFX/SM_VFX_SparkBolt"),
	TEXT("/Game/Art/VFX/SM_VFX_SprayCone"),
	TEXT("/Game/Art/VFX/SM_VFX_StoneWall"),
	TEXT("/Game/Art/VFX/SM_VFX_SwipeArc"),
	TEXT("/Game/Art/VFX/SM_VFX_Vine_A"),
	TEXT("/Game/Art/VFX/SM_VFX_Vine_B"),
	TEXT("/Game/Art/VFX/SM_VFX_Vine_C"),
	TEXT("/Game/Art/VFX/SM_VFX_WhistleNote"),
};

// Loads each prop and checks its material wiring: Main/All -> MI_<Name> (M_GF_Prop, or M_GF_Water for translucent
// blocks), TeamTint -> MI_GF_TeamTint. Pattern/Glass slots are left for gameplay/VFX code.
static int32 GFArtCheckProps(FAutomationTestBase& T, TArrayView<const TCHAR* const> Props)
{
	int32 Loaded = 0;
	for (const TCHAR* P : Props)
	{
		const FString Name = FPaths::GetBaseFilename(P);
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *(FString(P) + TEXT(".") + Name));
		if (!T.TestNotNull(FString::Printf(TEXT("%s loads"), P), M))
		{
			continue;
		}
		++Loaded;
		const bool bWater = Name == TEXT("SM_Block_Water");
		for (const FStaticMaterial& S : M->GetStaticMaterials())
		{
			const FString Slot = S.MaterialSlotName.ToString();
			UMaterialInstance* MI = Cast<UMaterialInstance>(S.MaterialInterface);
			if (Slot == TEXT("Main") || Slot == TEXT("All"))
			{
				if (T.TestNotNull(FString::Printf(TEXT("%s %s has an instance"), *Name, *Slot), MI))
				{
					T.TestEqual(FString::Printf(TEXT("%s %s instance"), *Name, *Slot), MI->GetName(), TEXT("MI_") + Name.RightChop(3));
					T.TestEqual(FString::Printf(TEXT("%s %s parent"), *Name, *Slot), MI->GetMaterial() ? MI->GetMaterial()->GetName() : FString(),
						FString(bWater ? TEXT("M_GF_Water") : TEXT("M_GF_Prop")));
					UTexture* D = nullptr;
					T.TestTrue(FString::Printf(TEXT("%s Diffuse set"), *Name), MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Diffuse")), D) && D != nullptr);
				}
			}
			else if (Slot == TEXT("TeamTint"))
			{
				T.TestTrue(FString::Printf(TEXT("%s TeamTint slot"), *Name), MI && MI->GetName() == TEXT("MI_GF_TeamTint"));
			}
		}
	}
	return Loaded;
}

static int32 GFArtCountTextures(FAutomationTestBase& T, const TCHAR* Dir, bool bCheckUi)
{
	TArray<FAssetData> Found;
	IAssetRegistry::GetChecked().ScanPathsSynchronous({ Dir }, true);
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(Dir), Found, false);
	int32 Count = 0;
	for (const FAssetData& A : Found)
	{
		UTexture2D* Tex = Cast<UTexture2D>(A.GetAsset());
		if (!Tex)
		{
			continue;
		}
		++Count;
		if (bCheckUi)
		{
			const bool bMask = Tex->GetName().EndsWith(TEXT("_M"));
			T.TestTrue(Tex->GetName() + TEXT(" UI settings"), Tex->LODGroup == TEXTUREGROUP_UI && Tex->Filter == TF_Nearest && Tex->SRGB == !bMask);
		}
	}
	return Count;
}

static int32 GFArtCountAnims(const TCHAR* Dir, USkeleton* Skel)
{
	TArray<FAssetData> Found;
	IAssetRegistry::GetChecked().ScanPathsSynchronous({ Dir }, true);
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(Dir), Found, false);
	int32 Count = 0;
	for (const FAssetData& A : Found)
	{
		UAnimSequenceBase* Anim = Cast<UAnimSequenceBase>(A.GetAsset());
		Count += (Anim && Anim->GetSkeleton() == Skel && Anim->GetPlayLength() > 0.f) ? 1 : 0;
	}
	return Count;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtBatch3bTest, "GFGame.Art.Batch3b", GFArtTestFlags)
bool FGFArtBatch3bTest::RunTest(const FString&)
{
	TestEqual(TEXT("35 batch-3b props"), GFArtCheckProps(*this, MakeArrayView(GFArtBatch3bProps)), 35);
	TestEqual(TEXT("3 taming UI textures"), GFArtCountTextures(*this, TEXT("/Game/Art/UI/Taming"), true), 3);
	// Water block: translucent, constant opacity 0.65, UV panner 0.05 U/s (manifest unreal_material_hint).
	UMaterial* Water = LoadObject<UMaterial>(nullptr, TEXT("/Game/Art/Materials/M_GF_Water.M_GF_Water"));
	if (TestNotNull(TEXT("M_GF_Water loads"), Water))
	{
		TestEqual(TEXT("M_GF_Water is translucent"), (int32)Water->BlendMode, (int32)BLEND_Translucent);
	}
	UMaterialInstance* WMI = LoadObject<UMaterialInstance>(nullptr, TEXT("/Game/Art/Blocks/Materials/MI_Block_Water.MI_Block_Water"));
	if (TestNotNull(TEXT("MI_Block_Water loads"), WMI))
	{
		float Opacity = 0.f, PanU = 0.f;
		TestTrue(TEXT("Opacity param"), WMI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")), Opacity));
		TestTrue(TEXT("PanSpeedU param"), WMI->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PanSpeedU")), PanU));
		TestEqual(TEXT("Water opacity"), Opacity, 0.65f, 1e-3f);
		TestEqual(TEXT("Water pan U"), PanU, 0.05f, 1e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtBatch7Test, "GFGame.Art.Batch7", GFArtTestFlags)
bool FGFArtBatch7Test::RunTest(const FString&)
{
	TestEqual(TEXT("34 batch-7 props"), GFArtCheckProps(*this, MakeArrayView(GFArtBatch7Props)), 34);
	struct FSock { const TCHAR* Mesh; const TCHAR* Socket; };
	const FSock Socks[] = {
		{ TEXT("/Game/Art/Colo/SM_Colo_Balcony.SM_Colo_Balcony"), TEXT("Stand") },
		{ TEXT("/Game/Art/Colo/SM_Colo_Balcony.SM_Colo_Balcony"), TEXT("Hourglass") },
		{ TEXT("/Game/Art/Colo/SM_Colo_RivalBalcony.SM_Colo_RivalBalcony"), TEXT("Stand") },
		{ TEXT("/Game/Art/Colo/SM_Colo_RivalBalcony.SM_Colo_RivalBalcony"), TEXT("Hourglass") },
		{ TEXT("/Game/Art/Furniture/SM_Deco_TrophyCase.SM_Deco_TrophyCase"), TEXT("Trophy_1") },
		{ TEXT("/Game/Art/Furniture/SM_Deco_TrophyCase.SM_Deco_TrophyCase"), TEXT("Trophy_2") },
		{ TEXT("/Game/Art/Furniture/SM_Deco_TrophyCase.SM_Deco_TrophyCase"), TEXT("Trophy_3") },
		{ TEXT("/Game/Art/Furniture/SM_Deco_TrophyCase.SM_Deco_TrophyCase"), TEXT("Trophy_4") },
	};
	for (const FSock& S : Socks)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, S.Mesh);
		TestTrue(FString::Printf(TEXT("%s socket %s"), S.Mesh, S.Socket), M && M->FindSocket(S.Socket) != nullptr);
	}
	UMaterial* Tint = LoadObject<UMaterial>(nullptr, TEXT("/Game/Art/Materials/M_GF_TeamTint.M_GF_TeamTint"));
	if (TestNotNull(TEXT("M_GF_TeamTint loads"), Tint))
	{
		FLinearColor C;
		TestTrue(TEXT("TeamColor param"), Tint->GetVectorParameterValue(FMaterialParameterInfo(TEXT("TeamColor")), C));
	}
	TestEqual(TEXT("62 arena UI textures (v2)"), GFArtCountTextures(*this, TEXT("/Game/Art/UI/Arena"), true), 62);
	USkeleton* Human = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Characters/Player/SKEL_GF_Humanoid.SKEL_GF_Humanoid"));
	USkeleton* Raptor = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Dinos/Raptor/SKEL_Dino_Raptor.SKEL_Dino_Raptor"));
	TestNotNull(TEXT("SKEL_Dino_Raptor loads"), Raptor);
	TestEqual(TEXT("SK_Player_Base v5: 27 clips"), GFArtCountAnims(TEXT("/Game/Art/Characters/Player/Anims"), Human), 27);
	TestEqual(TEXT("SK_Dino_Raptor v5: 17 clips"), GFArtCountAnims(TEXT("/Game/Art/Dinos/Raptor/Anims"), Raptor), 17);
	for (const TCHAR* Clip : { TEXT("Attack_Pounce"), TEXT("Attack_Rake"), TEXT("Call"), TEXT("Stunned"), TEXT("Cheer"), TEXT("Refuse") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/Dinos/Raptor/Anims/A_Dino_Raptor_%s.A_Dino_Raptor_%s"), Clip, Clip);
		UAnimSequenceBase* A = LoadObject<UAnimSequenceBase>(nullptr, *Path);
		TestTrue(FString::Printf(TEXT("Raptor %s on SKEL_Dino_Raptor"), Clip), A && A->GetSkeleton() == Raptor);
	}
	return true;
}

// New dinos v1 (Tusksaw, Ripwing, Scythepaw, Lanternmaw): one skeleton each, every clip in DT_AnimMap.
struct FGFNewDinoClip { const TCHAR* Dino; const TCHAR* Clip; bool bLoop; bool bHold; };
static const FGFNewDinoClip GFArtNewDinoClips[] = {
	{ TEXT("Tusksaw"), TEXT("Idle"), true, false },
	{ TEXT("Tusksaw"), TEXT("Walk"), true, false },
	{ TEXT("Tusksaw"), TEXT("Run"), true, false },
	{ TEXT("Tusksaw"), TEXT("Attack_Bite"), false, false },
	{ TEXT("Tusksaw"), TEXT("Hit"), false, false },
	{ TEXT("Tusksaw"), TEXT("Faint"), false, true },
	{ TEXT("Tusksaw"), TEXT("Rest"), true, false },
	{ TEXT("Tusksaw"), TEXT("Sniff"), false, false },
	{ TEXT("Tusksaw"), TEXT("Eat"), false, false },
	{ TEXT("Tusksaw"), TEXT("Happy"), false, false },
	{ TEXT("Tusksaw"), TEXT("Threat"), true, false },
	{ TEXT("Tusksaw"), TEXT("Refuse"), false, false },
	{ TEXT("Tusksaw"), TEXT("Tamed"), false, false },
	{ TEXT("Tusksaw"), TEXT("Stunned"), true, false },
	{ TEXT("Tusksaw"), TEXT("Cheer"), false, false },
	{ TEXT("Tusksaw"), TEXT("Burrow_In"), false, true },
	{ TEXT("Tusksaw"), TEXT("Burrowed_Loop"), true, false },
	{ TEXT("Tusksaw"), TEXT("Burrow_Out"), false, false },
	{ TEXT("Tusksaw"), TEXT("Attack_SpineSweep"), false, false },
	{ TEXT("Tusksaw"), TEXT("Brace_Burrow"), false, false },
	{ TEXT("Tusksaw"), TEXT("Burrow_Move"), true, false },
	{ TEXT("Ripwing"), TEXT("Idle"), true, false },
	{ TEXT("Ripwing"), TEXT("Perch_Idle"), true, false },
	{ TEXT("Ripwing"), TEXT("Walk"), true, false },
	{ TEXT("Ripwing"), TEXT("Run"), true, false },
	{ TEXT("Ripwing"), TEXT("Glide_Loop"), true, false },
	{ TEXT("Ripwing"), TEXT("Flap_Loop"), true, false },
	{ TEXT("Ripwing"), TEXT("Dive"), false, true },
	{ TEXT("Ripwing"), TEXT("Dive_Loop"), true, false },
	{ TEXT("Ripwing"), TEXT("Takeoff"), false, true },
	{ TEXT("Ripwing"), TEXT("Land_Strike"), false, false },
	{ TEXT("Ripwing"), TEXT("Grab_Lift"), false, true },
	{ TEXT("Ripwing"), TEXT("Carry_Loop"), true, false },
	{ TEXT("Ripwing"), TEXT("Drop"), false, false },
	{ TEXT("Ripwing"), TEXT("Flap_Buff"), false, false },
	{ TEXT("Ripwing"), TEXT("Attack_Bite"), false, false },
	{ TEXT("Ripwing"), TEXT("Threat"), true, false },
	{ TEXT("Ripwing"), TEXT("Happy_Catch"), false, false },
	{ TEXT("Ripwing"), TEXT("Hit"), false, false },
	{ TEXT("Ripwing"), TEXT("Faint"), false, true },
	{ TEXT("Ripwing"), TEXT("Rest"), true, false },
	{ TEXT("Ripwing"), TEXT("Sniff"), false, false },
	{ TEXT("Ripwing"), TEXT("Eat"), false, false },
	{ TEXT("Ripwing"), TEXT("Happy"), false, false },
	{ TEXT("Ripwing"), TEXT("Refuse"), false, false },
	{ TEXT("Ripwing"), TEXT("Tamed"), false, false },
	{ TEXT("Ripwing"), TEXT("Stunned"), true, false },
	{ TEXT("Ripwing"), TEXT("Cheer"), false, false },
	{ TEXT("Ripwing"), TEXT("Ferry_Takeoff"), false, true },
	{ TEXT("Ripwing"), TEXT("Ferry_Carry_Fly"), true, false },
	{ TEXT("Ripwing"), TEXT("Ferry_Land"), false, true },
	{ TEXT("Scythepaw"), TEXT("Idle"), true, false },
	{ TEXT("Scythepaw"), TEXT("Walk"), true, false },
	{ TEXT("Scythepaw"), TEXT("Run"), true, false },
	{ TEXT("Scythepaw"), TEXT("Attack_Bite"), false, false },
	{ TEXT("Scythepaw"), TEXT("Attack_ScytheSweep"), false, false },
	{ TEXT("Scythepaw"), TEXT("FluffUp"), false, true },
	{ TEXT("Scythepaw"), TEXT("Attack_LeapSlam"), false, false },
	{ TEXT("Scythepaw"), TEXT("Threat"), true, false },
	{ TEXT("Scythepaw"), TEXT("Sit_Happy"), false, false },
	{ TEXT("Scythepaw"), TEXT("Hit"), false, false },
	{ TEXT("Scythepaw"), TEXT("Faint"), false, true },
	{ TEXT("Scythepaw"), TEXT("Rest"), true, false },
	{ TEXT("Scythepaw"), TEXT("Sniff"), false, false },
	{ TEXT("Scythepaw"), TEXT("Eat"), false, false },
	{ TEXT("Scythepaw"), TEXT("Happy"), false, false },
	{ TEXT("Scythepaw"), TEXT("Refuse"), false, false },
	{ TEXT("Scythepaw"), TEXT("Tamed"), false, false },
	{ TEXT("Scythepaw"), TEXT("Stunned"), true, false },
	{ TEXT("Scythepaw"), TEXT("Cheer"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Idle"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Submerged_Idle"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Walk"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Run"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Swim"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Attack_Bite"), false, false },
	{ TEXT("Lanternmaw"), TEXT("LurePulse"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Attack_Gulp"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Belch"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Threat"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Sniff"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Eat"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Happy"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Refuse"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Tamed"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Hit"), false, false },
	{ TEXT("Lanternmaw"), TEXT("Faint"), false, true },
	{ TEXT("Lanternmaw"), TEXT("Rest"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Stunned"), true, false },
	{ TEXT("Lanternmaw"), TEXT("Cheer"), false, false },
};

// Component-space ref-pose transform of a bone (walks the parent chain).
static FTransform GFArtRefBoneCS(const FReferenceSkeleton& Ref, int32 Bone)
{
	FTransform T = FTransform::Identity;
	while (Bone != INDEX_NONE)
	{
		T = T * Ref.GetRefBonePose()[Bone];
		Bone = Ref.GetParentIndex(Bone);
	}
	return T;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtNewDinosTest, "GFGame.Art.NewDinos", GFArtTestFlags)
bool FGFArtNewDinosTest::RunTest(const FString&)
{
	struct FDino { const TCHAR* Name; int32 Bones; int32 Clips; };
	const FDino Dinos[] = { { TEXT("Tusksaw"), 27, 21 }, { TEXT("Ripwing"), 24, 30 }, { TEXT("Scythepaw"), 33, 19 }, { TEXT("Lanternmaw"), 28, 20 } };
	TMap<FString, USkeleton*> Skels;
	int32 Rows = 0;
	for (const FDino& D : Dinos)
	{
		const FString Dir = FString::Printf(TEXT("/Game/Art/Dinos/%s"), D.Name);
		USkeleton* Skel = LoadObject<USkeleton>(nullptr, *FString::Printf(TEXT("%s/SKEL_Dino_%s.SKEL_Dino_%s"), *Dir, D.Name, D.Name));
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s/SK_Dino_%s.SK_Dino_%s"), *Dir, D.Name, D.Name));
		Skels.Add(D.Name, Skel);
		if (!TestNotNull(FString::Printf(TEXT("SKEL_Dino_%s loads"), D.Name), Skel) || !TestNotNull(FString::Printf(TEXT("SK_Dino_%s loads"), D.Name), Mesh))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("SK_Dino_%s uses its own skeleton"), D.Name), Mesh->GetSkeleton() == Skel);
		TestEqual(FString::Printf(TEXT("SK_Dino_%s bones"), D.Name), Mesh->GetRefSkeleton().GetNum(), D.Bones);
		TestEqual(FString::Printf(TEXT("SK_Dino_%s Body slot"), D.Name), Mesh->GetMaterials().Num(), 1);
		TestEqual(FString::Printf(TEXT("SK_Dino_%s clips"), D.Name), GFArtCountAnims(*(Dir + TEXT("/Anims")), Skel), D.Clips);
		int32 Listed = 0;
		for (const FGFNewDinoClip& C : GFArtNewDinoClips)
		{
			Listed += FCString::Strcmp(C.Dino, D.Name) == 0 ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("%s DT_AnimMap rows listed"), D.Name), Listed, D.Clips);
	}
	for (const FGFNewDinoClip& C : GFArtNewDinoClips)
	{
		const FString Key = FString::Printf(TEXT("%s.%s"), C.Dino, C.Clip);
		const FGFAnimMapRow* Row = FGFAnimMap::Find(*Key);
		if (!TestNotNull(Key + TEXT(" row"), Row))
		{
			continue;
		}
		++Rows;
		TestEqual(Key + TEXT(" loop"), Row->bLoop, C.bLoop);
		TestEqual(Key + TEXT(" hold last frame"), Row->bHoldLastFrame, C.bHold);
		TestEqual(Key + TEXT(" planted prop offset"), Row->PlantedPropOffsetZ, 0.f);
		UAnimSequenceBase* Anim = FGFAnimMap::Load(*Key);
		if (TestNotNull(Key + TEXT(" clip loads"), Anim))
		{
			TestEqual(Key + TEXT(" clip"), Anim->GetName(), FString::Printf(TEXT("A_Dino_%s_%s"), C.Dino, C.Clip));
			USkeleton* const* Skel = Skels.Find(C.Dino);
			TestTrue(Key + TEXT(" on its dino skeleton"), Skel && *Skel && Anim->GetSkeleton() == *Skel);
			TestTrue(Key + TEXT(" has length"), Anim->GetPlayLength() > 0.f);
		}
	}
	TestEqual(TEXT("90 new dino DT_AnimMap rows"), Rows, 90);

	// Sockets from the manifests; ref-pose positions match the Blender bone-space offsets (UE component space).
	struct FSock { const TCHAR* Dino; const TCHAR* Socket; const TCHAR* Bone; FVector Local; FVector RefPos; };
	const FSock Socks[] = {
		{ TEXT("Lanternmaw"), TEXT("Lure"), TEXT("lure_bulb"), FVector(0.0, 0.0, 9.38), FVector(106.25, 0.0, 121.87) },
		{ TEXT("Ripwing"), TEXT("Grab"), TEXT("talon_l"), FVector(0.0, 0.0, 7.5), FVector(-12.5, -12.5, 3.125) },
		// Ripwing v2: midpoint between both closed talons; the ferry basket's Grip socket snaps here.
		{ TEXT("Ripwing"), TEXT("FerryGrip"), TEXT("talon_l"), FVector(0.0, 12.5, 3.75), FVector(-16.25, 0.0, 3.125) },
	};
	for (const FSock& S : Socks)
	{
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/Art/Dinos/%s/SK_Dino_%s.SK_Dino_%s"), S.Dino, S.Dino, S.Dino));
		USkeletalMeshSocket* Sock = Mesh ? Mesh->FindSocket(S.Socket) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s socket %s"), S.Dino, S.Socket), Sock))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s socket %s bone"), S.Dino, S.Socket), Sock->BoneName, FName(S.Bone));
		TestTrue(FString::Printf(TEXT("%s socket %s offset"), S.Dino, S.Socket), Sock->RelativeLocation.Equals(S.Local, 0.01));
		const int32 Bone = Mesh->GetRefSkeleton().FindBoneIndex(Sock->BoneName);
		if (TestTrue(FString::Printf(TEXT("%s bone %s exists"), S.Dino, S.Bone), Bone != INDEX_NONE))
		{
			const FVector Pos = (Sock->GetSocketLocalTransform() * GFArtRefBoneCS(Mesh->GetRefSkeleton(), Bone)).GetLocation();
			TestTrue(FString::Printf(TEXT("%s socket %s ref-pose position %s"), S.Dino, S.Socket, *Pos.ToString()), Pos.Equals(S.RefPos, 0.05));
		}
	}
	return true;
}

// Big Grumble (giant dino event art): mesh in /Game/Art/Dinos/BigGrumble on the shared SKEL_Dino_Rex
// (/Game/Art/Dinos/Rex, created by this import so a later SK_Dino_Rex reuses it), 8 clips in DT_AnimMap, Mouth socket.
struct FGFBigGrumbleClip { const TCHAR* Clip; bool bLoop; bool bHold; };
static const FGFBigGrumbleClip GFArtBigGrumbleClips[] = {
	{ TEXT("Taunt_Idle"), true, false },
	{ TEXT("Sniff_Boundary"), false, false },
	{ TEXT("Prowl_Circle"), true, false },
	{ TEXT("Snort_Laugh"), false, false },
	{ TEXT("FakeOut_Lunge"), false, false },
	{ TEXT("Scoop_Toss_Gulp"), false, true },
	{ TEXT("Satisfied_BellyPat"), false, true },
	{ TEXT("Waddle_Off"), true, false },
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtBigGrumbleTest, "GFGame.Art.BigGrumble", GFArtTestFlags)
bool FGFArtBigGrumbleTest::RunTest(const FString&)
{
	USkeleton* Rex = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Dinos/Rex/SKEL_Dino_Rex.SKEL_Dino_Rex"));
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Dinos/BigGrumble/SK_Dino_BigGrumble.SK_Dino_BigGrumble"));
	if (!TestNotNull(TEXT("SKEL_Dino_Rex loads"), Rex) || !TestNotNull(TEXT("SK_Dino_BigGrumble loads"), Mesh))
	{
		return false;
	}
	TestTrue(TEXT("SK_Dino_BigGrumble uses SKEL_Dino_Rex"), Mesh->GetSkeleton() == Rex);
	TestEqual(TEXT("SK_Dino_BigGrumble bones"), Mesh->GetRefSkeleton().GetNum(), 25);
	TestEqual(TEXT("SKEL_Dino_Rex bones"), Rex->GetReferenceSkeleton().GetNum(), 25);
	TestEqual(TEXT("SK_Dino_BigGrumble Body slot"), Mesh->GetMaterials().Num(), 1);
	TestFalse(TEXT("No skeleton in the BigGrumble folder"), FPackageName::DoesPackageExist(TEXT("/Game/Art/Dinos/BigGrumble/SKEL_Dino_Rex")));
	TestEqual(TEXT("BigGrumble clips on SKEL_Dino_Rex"), GFArtCountAnims(TEXT("/Game/Art/Dinos/BigGrumble/Anims"), Rex), 8);
	int32 Rows = 0;
	for (const FGFBigGrumbleClip& C : GFArtBigGrumbleClips)
	{
		const FString Key = FString::Printf(TEXT("BigGrumble.%s"), C.Clip);
		const FGFAnimMapRow* Row = FGFAnimMap::Find(*Key);
		if (!TestNotNull(Key + TEXT(" row"), Row))
		{
			continue;
		}
		++Rows;
		TestEqual(Key + TEXT(" loop"), Row->bLoop, C.bLoop);
		TestEqual(Key + TEXT(" hold last frame"), Row->bHoldLastFrame, C.bHold);
		TestEqual(Key + TEXT(" planted prop offset"), Row->PlantedPropOffsetZ, 0.f);
		UAnimSequenceBase* Anim = FGFAnimMap::Load(*Key);
		if (TestNotNull(Key + TEXT(" clip loads"), Anim))
		{
			TestEqual(Key + TEXT(" clip"), Anim->GetName(), FString::Printf(TEXT("A_Dino_BigGrumble_%s"), C.Clip));
			TestTrue(Key + TEXT(" on SKEL_Dino_Rex"), Anim->GetSkeleton() == Rex);
			TestTrue(Key + TEXT(" has length"), Anim->GetPlayLength() > 0.f);
		}
	}
	TestEqual(TEXT("8 BigGrumble DT_AnimMap rows"), Rows, 8);

	// Mouth socket (manifest: jaw bone space [0, 112.5, 0] cm; Blender ref pose puts it at 312.5 forward, 343.75 up).
	USkeletalMeshSocket* Sock = Mesh->FindSocket(TEXT("Mouth"));
	if (TestNotNull(TEXT("Mouth socket"), Sock))
	{
		TestEqual(TEXT("Mouth socket bone"), Sock->BoneName, FName(TEXT("jaw")));
		TestTrue(TEXT("Mouth socket offset"), Sock->RelativeLocation.Equals(FVector(0.0, 0.0, 112.5), 0.01));
		const int32 Bone = Mesh->GetRefSkeleton().FindBoneIndex(Sock->BoneName);
		if (TestTrue(TEXT("jaw bone exists"), Bone != INDEX_NONE))
		{
			const FVector Pos = (Sock->GetSocketLocalTransform() * GFArtRefBoneCS(Mesh->GetRefSkeleton(), Bone)).GetLocation();
			TestTrue(FString::Printf(TEXT("Mouth ref-pose position %s"), *Pos.ToString()), Pos.Equals(FVector(312.5, 0.0, 343.75), 0.05));
		}
	}
	return true;
}

// Rex (rex_v1): SK_Dino_Rex in /Game/Art/Dinos/Rex on the existing SKEL_Dino_Rex (shared with Big Grumble, still
// 25 bones), 21 clips at 30 fps in DT_AnimMap (Rex.<Clip>), Mouth socket on the jaw.
struct FGFRexClip { const TCHAR* Clip; bool bLoop; bool bHold; int32 Frames; };
static const FGFRexClip GFArtRexClips[] = {
	{ TEXT("Idle"), true, false, 60 }, { TEXT("Walk"), true, false, 32 }, { TEXT("Run"), true, false, 20 },
	{ TEXT("Threat"), true, false, 30 }, { TEXT("Attack_Bite"), false, false, 15 }, { TEXT("Roar"), false, false, 30 },
	{ TEXT("Eat"), false, false, 30 }, { TEXT("Happy"), false, false, 36 }, { TEXT("Tamed"), false, false, 48 },
	{ TEXT("Hit"), false, false, 9 }, { TEXT("Faint"), false, true, 30 }, { TEXT("Rest"), true, false, 60 },
	{ TEXT("Sniff"), false, false, 30 }, { TEXT("Refuse"), false, false, 24 }, { TEXT("Stunned"), true, false, 24 },
	{ TEXT("Cheer"), false, false, 30 }, { TEXT("Attack_TailSwipe"), false, false, 20 }, { TEXT("Stomp"), false, false, 18 },
	{ TEXT("Attack_Chomp"), false, false, 24 }, { TEXT("Slam"), false, false, 24 }, { TEXT("Enrage"), false, false, 40 },
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtRexTest, "GFGame.Art.Rex", GFArtTestFlags)
bool FGFArtRexTest::RunTest(const FString&)
{
	USkeleton* Rex = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Dinos/Rex/SKEL_Dino_Rex.SKEL_Dino_Rex"));
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Art/Dinos/Rex/SK_Dino_Rex.SK_Dino_Rex"));
	if (!TestNotNull(TEXT("SKEL_Dino_Rex loads"), Rex) || !TestNotNull(TEXT("SK_Dino_Rex loads"), Mesh))
	{
		return false;
	}
	TestTrue(TEXT("SK_Dino_Rex uses SKEL_Dino_Rex"), Mesh->GetSkeleton() == Rex);
	TestEqual(TEXT("SK_Dino_Rex bones"), Mesh->GetRefSkeleton().GetNum(), 25);
	TestEqual(TEXT("SKEL_Dino_Rex bones (no new bones)"), Rex->GetReferenceSkeleton().GetNum(), 25);
	TestEqual(TEXT("SK_Dino_Rex Body slot"), Mesh->GetMaterials().Num(), 1);
	TestEqual(TEXT("Rex clips on SKEL_Dino_Rex"), GFArtCountAnims(TEXT("/Game/Art/Dinos/Rex/Anims"), Rex), 21);
	int32 Rows = 0;
	for (const FGFRexClip& C : GFArtRexClips)
	{
		const FString Key = FString::Printf(TEXT("Rex.%s"), C.Clip);
		const FGFAnimMapRow* Row = FGFAnimMap::Find(*Key);
		if (!TestNotNull(Key + TEXT(" row"), Row))
		{
			continue;
		}
		++Rows;
		TestEqual(Key + TEXT(" loop"), Row->bLoop, C.bLoop);
		TestEqual(Key + TEXT(" hold last frame"), Row->bHoldLastFrame, C.bHold);
		TestEqual(Key + TEXT(" planted prop offset"), Row->PlantedPropOffsetZ, 0.f);
		UAnimSequenceBase* Anim = FGFAnimMap::Load(*Key);
		if (TestNotNull(Key + TEXT(" clip loads"), Anim))
		{
			TestEqual(Key + TEXT(" clip"), Anim->GetName(), FString::Printf(TEXT("A_Dino_Rex_%s"), C.Clip));
			TestTrue(Key + TEXT(" on SKEL_Dino_Rex"), Anim->GetSkeleton() == Rex);
			TestTrue(FString::Printf(TEXT("%s length %f at 30 fps"), *Key, Anim->GetPlayLength()), FMath::IsNearlyEqual(Anim->GetPlayLength(), C.Frames / 30.f, 0.001f));
		}
	}
	TestEqual(TEXT("21 Rex DT_AnimMap rows"), Rows, 21);

	USkeletalMeshSocket* Sock = Mesh->FindSocket(TEXT("Mouth"));
	if (TestNotNull(TEXT("Mouth socket"), Sock))
	{
		TestEqual(TEXT("Mouth socket bone"), Sock->BoneName, FName(TEXT("jaw")));
		TestTrue(TEXT("Mouth socket offset"), Sock->RelativeLocation.Equals(FVector(0.0, 0.0, 112.5), 0.01));
	}
	return true;
}

// Every committed dino skeletal mesh loads with its own skeleton (catches a mesh left pointing at a deleted
// import-time skeleton or redirector). Meshes on a shared skeleton are listed in Shared.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtDinoMeshesTest, "GFGame.Art.DinoMeshes", GFArtTestFlags)
bool FGFArtDinoMeshesTest::RunTest(const FString&)
{
	IAssetRegistry& Reg = IAssetRegistry::GetChecked();
	Reg.ScanPathsSynchronous({ TEXT("/Game/Art/Dinos") }, true);
	TArray<FAssetData> Found;
	Reg.GetAssetsByPath(TEXT("/Game/Art/Dinos"), Found, true);
	TSet<FString> Names;
	const TMap<FString, FString> Shared = { { TEXT("SK_Dino_BigGrumble"), TEXT("/Game/Art/Dinos/Rex/SKEL_Dino_Rex.SKEL_Dino_Rex") } };
	for (const FAssetData& A : Found)
	{
		if (A.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName())
		{
			continue;
		}
		const FString Name = A.AssetName.ToString();
		Names.Add(Name);
		USkeletalMesh* Mesh = Cast<USkeletalMesh>(A.GetAsset());
		if (!TestNotNull(Name + TEXT(" loads"), Mesh))
		{
			continue;
		}
		USkeleton* Skel = Mesh->GetSkeleton();
		if (!TestNotNull(Name + TEXT(" has a skeleton"), Skel))
		{
			continue;
		}
		const FString* SharedSkel = Shared.Find(Name);
		const FString Want = SharedSkel ? *SharedSkel : FString::Printf(TEXT("%s/SKEL_%s.SKEL_%s"), *A.PackagePath.ToString(), *Name.RightChop(3), *Name.RightChop(3));
		TestEqual(Name + TEXT(" skeleton"), Skel->GetPathName(), Want);
		TestTrue(Name + TEXT(" skeleton matches the mesh bones"), Skel->GetReferenceSkeleton().GetNum() == Mesh->GetRefSkeleton().GetNum() && Mesh->GetRefSkeleton().GetNum() > 0);
	}
	for (const TCHAR* N : { TEXT("SK_Dino_Raptor"), TEXT("SK_Dino_Tusksaw"), TEXT("SK_Dino_Ripwing"), TEXT("SK_Dino_Scythepaw"), TEXT("SK_Dino_Lanternmaw"), TEXT("SK_Dino_BigGrumble"), TEXT("SK_Dino_Rex") })
	{
		TestTrue(FString::Printf(TEXT("%s is committed"), N), Names.Contains(N));
	}
	return true;
}

// Splits one CSV line into fields (double-quoted fields may contain commas; "" is a literal quote).
static TArray<FString> GFArtCsvFields(const FString& Line)
{
	TArray<FString> Out;
	FString Cur;
	bool bQuoted = false;
	for (int32 i = 0; i < Line.Len(); ++i)
	{
		const TCHAR C = Line[i];
		if (bQuoted)
		{
			if (C == TEXT('"') && i + 1 < Line.Len() && Line[i + 1] == TEXT('"')) { Cur.AppendChar(C); ++i; }
			else if (C == TEXT('"')) { bQuoted = false; }
			else { Cur.AppendChar(C); }
		}
		else if (C == TEXT('"')) { bQuoted = true; }
		else if (C == TEXT(',')) { Out.Add(Cur); Cur.Reset(); }
		else { Cur.AppendChar(C); }
	}
	Out.Add(Cur);
	return Out;
}

// Every imported mesh under /Game/Art has a row in docs/LicenseLedger.csv (asset_path names its handoff/inbox
// folder), and no row credits a bot or is marked PRIVATE. Tools/check_ledger.py checks the same against the
// importer's full list (texture sets included) without Unreal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtLedgerTest, "GFGame.Art.LedgerCoverage", GFArtTestFlags)
bool FGFArtLedgerTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	if (!TestTrue(TEXT("docs/LicenseLedger.csv loads"), FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/LicenseLedger.csv")))) || !TestTrue(TEXT("ledger has rows"), Lines.Num() > 1))
	{
		return false;
	}
	const TArray<FString> Header = GFArtCsvFields(Lines[0]);
	const int32 PathCol = Header.IndexOfByKey(FString(TEXT("asset_path")));
	const int32 CreatorCol = Header.IndexOfByKey(FString(TEXT("creator")));
	if (!TestTrue(TEXT("ledger header has asset_path and creator"), PathCol != INDEX_NONE && CreatorCol != INDEX_NONE))
	{
		return false;
	}
	TArray<FString> Paths;
	for (int32 i = 1; i < Lines.Num(); ++i)
	{
		if (Lines[i].IsEmpty())
		{
			continue;
		}
		const TArray<FString> F = GFArtCsvFields(Lines[i]);
		if (!TestTrue(FString::Printf(TEXT("ledger line %d has every column"), i + 1), F.Num() >= Header.Num()))
		{
			continue;
		}
		Paths.Add(F[PathCol].Replace(TEXT("/"), TEXT("\\")));
		const FString& Creator = F[CreatorCol];
		const bool bProjectCredit = Creator == TEXT("GFGame project (own work)") || Creator == TEXT("project artist (Blender)");
		TestTrue(FString::Printf(TEXT("ledger line %d creator is a project credit"), i + 1), bProjectCredit);
		TestFalse(FString::Printf(TEXT("ledger line %d is not PRIVATE"), i + 1), Lines[i].Contains(TEXT("PRIVATE"), ESearchCase::CaseSensitive));
	}
	auto HasRow = [&Paths](const FString& Name)
	{
		const FString Key = TEXT("handoff\\inbox\\") + Name;
		for (const FString& P : Paths)
		{
			int32 At = P.Find(Key, ESearchCase::CaseSensitive);
			while (At != INDEX_NONE)
			{
				const int32 End = At + Key.Len();
				if (End == P.Len() || FString(TEXT("\\ ;,(")).Contains(P.Mid(End, 1)))
				{
					return true;
				}
				At = P.Find(Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, End);
			}
		}
		return false;
	};

	IAssetRegistry& Reg = IAssetRegistry::GetChecked();
	Reg.ScanPathsSynchronous({ TEXT("/Game/Art") }, true);
	TArray<FAssetData> Found;
	Reg.GetAssetsByPath(TEXT("/Game/Art"), Found, true);
	int32 Meshes = 0;
	for (const FAssetData& A : Found)
	{
		if (A.AssetClassPath != UStaticMesh::StaticClass()->GetClassPathName() && A.AssetClassPath != USkeletalMesh::StaticClass()->GetClassPathName())
		{
			continue;
		}
		++Meshes;
		const FString Name = A.AssetName.ToString();
		TestTrue(Name + TEXT(" has a ledger row"), HasRow(Name));
	}
	TestTrue(TEXT("found imported meshes"), Meshes > 100);
	return true;
}

// Ferry kit (ferry_v1): perch and basket props with collision and attach sockets, ferry UI icons,
// and the crest-tint material instance for ferry Ripwings (clips and FerryGrip are checked in NewDinos).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFArtFerryTest, "GFGame.Art.Ferry", GFArtTestFlags)
bool FGFArtFerryTest::RunTest(const FString&)
{
	struct FSock { const TCHAR* Name; FVector Loc; };
	struct FProp { const TCHAR* Mesh; const TCHAR* MI; FSock Socks[2]; };
	const FProp Props[] = {
		{ TEXT("SM_FerryPerch"), TEXT("MI_FerryPerch"), { { TEXT("PerchPoint"), FVector(-95.0, 0.0, 262.5) }, { TEXT("BasketSpot"), FVector(0.0, 0.0, 12.5) } } },
		{ TEXT("SM_FerryBasket"), TEXT("MI_FerryBasket"), { { TEXT("Grip"), FVector(0.0, 0.0, 193.75) }, { TEXT("Seat"), FVector(-25.0, 0.0, 12.5) } } },
	};
	for (const FProp& P : Props)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Art/Props/Ferry/%s.%s"), P.Mesh, P.Mesh));
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), P.Mesh), M))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s has collision"), P.Mesh), M->GetBodySetup() && M->GetBodySetup()->AggGeom.GetElementCount() > 0);
		const TArray<FStaticMaterial>& Mats = M->GetStaticMaterials();
		TestTrue(FString::Printf(TEXT("%s uses %s"), P.Mesh, P.MI), Mats.Num() == 1 && Mats[0].MaterialInterface && Mats[0].MaterialInterface->GetName() == P.MI);
		for (const FSock& S : P.Socks)
		{
			UStaticMeshSocket* Sock = M->FindSocket(FName(S.Name));
			if (TestNotNull(FString::Printf(TEXT("%s socket %s"), P.Mesh, S.Name), Sock))
			{
				TestTrue(FString::Printf(TEXT("%s socket %s at %s"), P.Mesh, S.Name, *Sock->RelativeLocation.ToString()), Sock->RelativeLocation.Equals(S.Loc, 0.01));
			}
		}
	}

	// Ferry UI icons: UserInterface2D, UI group, nearest, no mips, 128x128.
	TArray<FAssetData> Found;
	IAssetRegistry::GetChecked().ScanPathsSynchronous({ TEXT("/Game/Art/UI/Ferry") }, true);
	IAssetRegistry::GetChecked().GetAssetsByPath(TEXT("/Game/Art/UI/Ferry"), Found, false);
	TestEqual(TEXT("2 UI textures in /Game/Art/UI/Ferry"), Found.Num(), 2);
	for (const FAssetData& A : Found)
	{
		UTexture2D* T = Cast<UTexture2D>(A.GetAsset());
		if (!TestNotNull(A.AssetName.ToString() + TEXT(" is a Texture2D"), T))
		{
			continue;
		}
		bool bOk = T->CompressionSettings == TC_EditorIcon && T->LODGroup == TEXTUREGROUP_UI && T->Filter == TF_Nearest && T->SRGB;
#if WITH_EDITORONLY_DATA
		bOk = bOk && T->MipGenSettings == TMGS_NoMipmaps && T->Source.GetSizeX() == 128 && T->Source.GetSizeY() == 128;
#endif
		TestTrue(T->GetName() + TEXT(" UI settings"), bOk);
	}

	// Ferry birds: crest (mask R) tinted sunflower #FFC93C on the skeletal dye master.
	UMaterialInstance* MI = LoadObject<UMaterialInstance>(nullptr, TEXT("/Game/Art/Dinos/Ripwing/Materials/MI_Dino_Ripwing_Ferry.MI_Dino_Ripwing_Ferry"));
	if (TestNotNull(TEXT("MI_Dino_Ripwing_Ferry loads"), MI))
	{
		TestTrue(TEXT("MI_Dino_Ripwing_Ferry parent is M_GF_DyeMask"), MI->Parent && MI->Parent->GetName() == TEXT("M_GF_DyeMask"));
		FLinearColor Dye;
		const FLinearColor Want = FLinearColor::FromSRGBColor(FColor(0xFF, 0xC9, 0x3C));
		TestTrue(TEXT("MI_Dino_Ripwing_Ferry Dye_R is #FFC93C"), MI->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Dye_R")), Dye) && Dye.Equals(Want, 0.003f));
		UTexture* Mask = nullptr;
		TestTrue(TEXT("MI_Dino_Ripwing_Ferry Mask is T_Dino_Ripwing_M"), MI->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Mask")), Mask) && Mask && Mask->GetName() == TEXT("T_Dino_Ripwing_M"));
	}
	return true;
}

#endif
