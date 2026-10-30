// Imported-art checks (art batch 8: landlord kit, SK_Player_Base v2 emotes + dye material, co-op UI textures;
// batch 3b: taming/cooking/fishing props; batch 7: arena kit, SK_Player_Base v4, SK_Dino_Raptor v5, arena UI).
// They load the committed /Game/Art assets, so they fail if an import is missing or a reference breaks.
#include "Misc/AutomationTest.h"
#include "GFAnimMap.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
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
	TestNull(TEXT("Unknown key"), FGFAnimMap::Find(TEXT("Player.Emote_Dance")));
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
	TestEqual(TEXT("SK_Player_Base v4: 23 clips"), GFArtCountAnims(TEXT("/Game/Art/Characters/Player/Anims"), Human), 23);
	TestEqual(TEXT("SK_Dino_Raptor v5: 17 clips"), GFArtCountAnims(TEXT("/Game/Art/Dinos/Raptor/Anims"), Raptor), 17);
	for (const TCHAR* Clip : { TEXT("Attack_Pounce"), TEXT("Attack_Rake"), TEXT("Call"), TEXT("Stunned"), TEXT("Cheer"), TEXT("Refuse") })
	{
		const FString Path = FString::Printf(TEXT("/Game/Art/Dinos/Raptor/Anims/A_Dino_Raptor_%s.A_Dino_Raptor_%s"), Clip, Clip);
		UAnimSequenceBase* A = LoadObject<UAnimSequenceBase>(nullptr, *Path);
		TestTrue(FString::Printf(TEXT("Raptor %s on SKEL_Dino_Raptor"), Clip), A && A->GetSkeleton() == Raptor);
	}
	return true;
}

#endif
