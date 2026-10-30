#pragma once

#include "CoreMinimal.h"
#include "VoxelFinish.h"

/** One node of an authored effect graph. Limits are checked; the graph is not evaluated. */
struct VOXELFINISH_API FEffectGraphNode
{
	FString Id;
	FString Type;
	TArray<FString> Inputs;
	FString SubEffectId;
	/** INDEX_NONE means the latest revision of SubEffectId. */
	int32 SubEffectRev = INDEX_NONE;
};

struct VOXELFINISH_API FEffectGraph
{
	FString EffectId;
	int32 Revision = 1;
	/** FFinish::EffectId this graph backs ("effect_index" in the JSON). INDEX_NONE when the file has none. */
	int32 EffectIndex = INDEX_NONE;
	TArray<FEffectGraphNode> Nodes;
};

struct VOXELFINISH_API FEffectValidation
{
	bool bAccepted = false;
	FString Reason;
	int32 AuthoredNodes = 0;
	int32 FlattenedNodes = 0;
	int32 FlattenedOps = 0;
	int32 Depth = 0;
	int32 CombinedSteps = 0;
};

/**
 * Registry of authored graphs. Flattening inlines SubEffects, rejects cycles,
 * and enforces authored / flattened size limits. It does not shade texels.
 */
class VOXELFINISH_API FEffectGraphRegistry
{
public:
	/** Refuses a second registration of the same effect id and revision. */
	bool Add(const FEffectGraph& Graph, FString* OutError = nullptr);

	const FEffectGraph* Find(const FString& EffectId, int32 Revision) const;

	FEffectValidation Validate(const FString& EffectId, int32 Revision = INDEX_NONE) const;

	/** Parses one graph object. Does not add it. */
	static bool ParseGraphJson(const FString& JsonText, FEffectGraph& OutGraph, FString& OutError);

	/** Loads every .json file under the plugin Content/Effects/Starter folder. */
	int32 LoadStarterGraphs(FString* OutError = nullptr);

	int32 NumGraphs() const { return Graphs.Num(); }

	/** EffectIndex of every registered graph that passes Validate. Used to build the paint allow-list. */
	void CollectAcceptedEffectIndexes(TSet<int32>& OutIndexes) const;

private:
	struct FFlatStats
	{
		int32 Nodes = 0;
		int32 Ops = 0;
		int32 Depth = 0;
	};

	bool Flatten(const FEffectGraph& Graph, int32 Depth, TArray<FString>& Stack, FEffectValidation& Out, FFlatStats& Stats) const;

	TArray<FEffectGraph> Graphs;
};
