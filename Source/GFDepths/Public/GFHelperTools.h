#pragma once

// Rented-dino helper tools for Hallow Night water. The lab registry
// (Tools/DinoChatLab/src/logic/helperTools.ts) parses a model reply and names a script.
// This is the small game-side copy: same three tools, same offers, same locomotion gate.
// It does not read model text. The caller already has the tool name.
// Water dinos float. Flyers carry. Everyone else keeps their feet on the ground.

#include "CoreMinimal.h"
#include <initializer_list>

/** Land keeps its feet down. Water floats. Flyer carries. */
enum class EGFHelperLocomotion : uint8
{
	Land,
	Water,
	Flyer,
};

struct GFDEPTHS_API FGFHelperTool
{
	FName Id;
	FString Offer;
	FName BehaviorId;
	EGFHelperLocomotion Locomotion = EGFHelperLocomotion::Land;
	FString Verb;
	FString Summary;
};

struct GFDEPTHS_API FGFHelperBearer
{
	FString Id;
	TArray<EGFHelperLocomotion> Traits;
};

struct GFDEPTHS_API FGFHelperArgs
{
	FString Where;
	FString Area;
	FString Level;
	bool bWhere = false;
	bool bArea = false;
	bool bLevel = false;
};

enum class EGFHelperFail : uint8
{
	None,
	UnknownTool,
	NotOffered,
	BadArgs,
};

struct GFDEPTHS_API FGFHelperResolution
{
	bool bOk = false;
	/** The caller sent no tool. Nothing loads. */
	bool bDecline = false;
	EGFHelperFail Fail = EGFHelperFail::None;
	FString Detail;
	FName ToolId;
	FName BehaviorId;
	FString Offer;
	FGFHelperArgs Args;
};

struct GFDEPTHS_API FGFHelperTools
{
	static const FName CatchPlayer;
	static const FName FloatWater;
	static const FName FlyOver;

	static const TArray<FGFHelperTool>& Catalog();
	/** Species on the lab roster. Anything else is land. "Ripwing" and "ripwing" match. */
	static EGFHelperLocomotion LocomotionFor(const FString& DinoId);
	static FGFHelperBearer BearerFor(const FString& DinoId);
	/** An empty list is land. Use this when a caller already knows the traits. */
	static FGFHelperBearer BearerFor(const FString& DinoId, std::initializer_list<EGFHelperLocomotion> Traits);
	static TArray<FGFHelperTool> ToolsFor(const FGFHelperBearer& Bearer);
	/** Empty when the dino has nothing to offer. */
	static FString UnpromptedOffer(const FGFHelperBearer& Bearer, const FString& Level);
	static FString SystemLine(const FGFHelperBearer& Bearer);
	/**
	 * Check the tool name and the one argument that tool cares about.
	 * catch_player may include where. float_water_area needs area. fly_player_over needs level.
	 * A none tool id is a decline.
	 */
	static FGFHelperResolution Resolve(const FGFHelperBearer& Bearer, FName ToolId, const FGFHelperArgs& Args);
};
