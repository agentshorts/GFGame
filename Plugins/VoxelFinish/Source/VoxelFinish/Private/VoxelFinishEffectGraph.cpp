#include "VoxelFinishEffectGraph.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace VoxelFinishGraph
{
	static int32 OpCost(const FString& Type)
	{
		if (Type == TEXT("add") || Type == TEXT("mul") || Type == TEXT("sin") || Type == TEXT("step"))
		{
			return 1;
		}
		if (Type == TEXT("lerp") || Type == TEXT("clamp") || Type == TEXT("scroll_uv"))
		{
			return 2;
		}
		if (Type == TEXT("smoothstep"))
		{
			return 3;
		}
		if (Type == TEXT("hue_shift") || Type == TEXT("rotate_uv"))
		{
			return 4;
		}
		return 0;
	}

	static bool IsKnownType(const FString& Type)
	{
		return Type == TEXT("input") || Type == TEXT("const") || Type == TEXT("subeffect")
			|| Type == TEXT("add") || Type == TEXT("mul") || Type == TEXT("lerp") || Type == TEXT("sin")
			|| Type == TEXT("step") || Type == TEXT("smoothstep") || Type == TEXT("clamp")
			|| Type == TEXT("hue_shift") || Type == TEXT("scroll_uv") || Type == TEXT("rotate_uv");
	}

	static FString NodeRef(const FString& Input)
	{
		int32 Dot = INDEX_NONE;
		if (Input.FindChar(TEXT('.'), Dot))
		{
			return Input.Left(Dot);
		}
		return Input;
	}

	static bool IsDag(const FEffectGraph& Graph, FString& OutError)
	{
		TMap<FString, const FEffectGraphNode*> ById;
		for (const FEffectGraphNode& Node : Graph.Nodes)
		{
			if (Node.Id.IsEmpty() || Node.Id.Contains(TEXT("/")) || Node.Id.Contains(TEXT(".")))
			{
				OutError = FString::Printf(TEXT("%s: node id '%s' is empty or contains '/' or '.'"), *Graph.EffectId, *Node.Id);
				return false;
			}
			if (ById.Contains(Node.Id))
			{
				OutError = FString::Printf(TEXT("%s: duplicate node id '%s'"), *Graph.EffectId, *Node.Id);
				return false;
			}
			ById.Add(Node.Id, &Node);
		}

		enum class EColor : uint8 { White, Gray, Black };
		struct FDagWalk
		{
			const TMap<FString, const FEffectGraphNode*>& ById;
			const FString& EffectId;
			FString& Error;
			TMap<FString, EColor> Color;

			bool Visit(const FString& Id)
			{
				const EColor* Found = Color.Find(Id);
				if (Found && *Found == EColor::Gray)
				{
					Error = FString::Printf(TEXT("%s: node cycle at '%s'"), *EffectId, *Id);
					return false;
				}
				if (Found && *Found == EColor::Black)
				{
					return true;
				}
				const FEffectGraphNode* Node = ById.FindRef(Id);
				if (!Node)
				{
					Error = FString::Printf(TEXT("%s: missing node '%s'"), *EffectId, *Id);
					return false;
				}
				Color.Add(Id, EColor::Gray);
				for (const FString& Input : Node->Inputs)
				{
					if (!Visit(NodeRef(Input)))
					{
						return false;
					}
				}
				Color.Add(Id, EColor::Black);
				return true;
			}
		};

		FDagWalk Walk{ ById, Graph.EffectId, OutError };
		for (const FEffectGraphNode& Node : Graph.Nodes)
		{
			if (!Walk.Visit(Node.Id))
			{
				return false;
			}
		}
		return true;
	}
}

bool FEffectGraphRegistry::Add(const FEffectGraph& Graph, FString* OutError)
{
	if (Graph.EffectId.IsEmpty() || Graph.Revision < 1)
	{
		if (OutError)
		{
			*OutError = TEXT("effect id and revision >= 1 are required");
		}
		return false;
	}
	for (const FEffectGraph& Existing : Graphs)
	{
		if (Existing.EffectId == Graph.EffectId && Existing.Revision == Graph.Revision)
		{
			if (OutError)
			{
				*OutError = FString::Printf(TEXT("%s rev %d is already registered"), *Graph.EffectId, Graph.Revision);
			}
			return false;
		}
	}
	Graphs.Add(Graph);
	return true;
}

const FEffectGraph* FEffectGraphRegistry::Find(const FString& EffectId, int32 Revision) const
{
	const FEffectGraph* Best = nullptr;
	for (const FEffectGraph& Graph : Graphs)
	{
		if (Graph.EffectId != EffectId)
		{
			continue;
		}
		if (Revision == INDEX_NONE)
		{
			if (!Best || Graph.Revision > Best->Revision)
			{
				Best = &Graph;
			}
		}
		else if (Graph.Revision == Revision)
		{
			return &Graph;
		}
	}
	return Best;
}

bool FEffectGraphRegistry::Flatten(const FEffectGraph& Graph, int32 Depth, TArray<FString>& Stack, FEffectValidation& Out, FFlatStats& Stats) const
{
	Out.AuthoredNodes = FMath::Max(Out.AuthoredNodes, Graph.Nodes.Num());
	if (Graph.Nodes.Num() > VoxelFinish::MaxAuthoredNodes)
	{
		Out.Reason = FString::Printf(TEXT("%s: %d authored nodes > %d"), *Graph.EffectId, Graph.Nodes.Num(), VoxelFinish::MaxAuthoredNodes);
		return false;
	}
	FString DagError;
	if (!VoxelFinishGraph::IsDag(Graph, DagError))
	{
		Out.Reason = DagError;
		return false;
	}
	if (Stack.Contains(Graph.EffectId))
	{
		Out.Reason = FString::Printf(TEXT("%s: effect cycle (%s)"), *Graph.EffectId, *FString::Join(Stack, TEXT(" -> ")));
		return false;
	}

	Stack.Add(Graph.EffectId);
	FFlatStats Mine;
	Mine.Depth = Depth;
	for (const FEffectGraphNode& Node : Graph.Nodes)
	{
		if (!VoxelFinishGraph::IsKnownType(Node.Type))
		{
			Out.Reason = FString::Printf(TEXT("%s: unknown node type '%s'"), *Graph.EffectId, *Node.Type);
			Stack.Pop();
			return false;
		}
		if (Node.Type == TEXT("subeffect"))
		{
			if (Depth + 1 > VoxelFinish::MaxSubEffectDepth)
			{
				Out.Reason = FString::Printf(TEXT("%s: SubEffect depth > %d"), *Graph.EffectId, VoxelFinish::MaxSubEffectDepth);
				Stack.Pop();
				return false;
			}
			const FEffectGraph* Child = Find(Node.SubEffectId, Node.SubEffectRev);
			if (!Child)
			{
				Out.Reason = FString::Printf(TEXT("%s: missing SubEffect %s"), *Graph.EffectId, *Node.SubEffectId);
				Stack.Pop();
				return false;
			}
			FFlatStats ChildStats;
			if (!Flatten(*Child, Depth + 1, Stack, Out, ChildStats))
			{
				Stack.Pop();
				return false;
			}
			Mine.Nodes += ChildStats.Nodes;
			Mine.Ops += ChildStats.Ops;
			Mine.Depth = FMath::Max(Mine.Depth, ChildStats.Depth);
		}
		else
		{
			++Mine.Nodes;
			Mine.Ops += VoxelFinishGraph::OpCost(Node.Type);
		}
	}
	Stack.Pop();
	Stats = Mine;
	return true;
}

FEffectValidation FEffectGraphRegistry::Validate(const FString& EffectId, int32 Revision) const
{
	FEffectValidation Result;
	const FEffectGraph* Graph = Find(EffectId, Revision);
	if (!Graph)
	{
		Result.Reason = FString::Printf(TEXT("unknown effect %s"), *EffectId);
		return Result;
	}
	Result.AuthoredNodes = Graph->Nodes.Num();
	TArray<FString> Stack;
	FFlatStats Stats;
	if (!Flatten(*Graph, 0, Stack, Result, Stats))
	{
		return Result;
	}
	Result.FlattenedNodes = Stats.Nodes;
	Result.FlattenedOps = Stats.Ops;
	Result.Depth = Stats.Depth;
	Result.CombinedSteps = Stats.Nodes;
	if (Result.FlattenedNodes > VoxelFinish::MaxFlattenedNodes)
	{
		Result.Reason = FString::Printf(TEXT("%s: %d flattened nodes > %d"), *EffectId, Result.FlattenedNodes, VoxelFinish::MaxFlattenedNodes);
		return Result;
	}
	if (Result.FlattenedOps > VoxelFinish::MaxFlattenedOps)
	{
		Result.Reason = FString::Printf(TEXT("%s: op cost %d > %d"), *EffectId, Result.FlattenedOps, VoxelFinish::MaxFlattenedOps);
		return Result;
	}
	if (Result.CombinedSteps > VoxelFinish::MaxCombinedSteps)
	{
		Result.Reason = FString::Printf(TEXT("%s: %d combined steps > %d"), *EffectId, Result.CombinedSteps, VoxelFinish::MaxCombinedSteps);
		return Result;
	}
	Result.bAccepted = true;
	return Result;
}

bool FEffectGraphRegistry::ParseGraphJson(const FString& JsonText, FEffectGraph& OutGraph, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("graph JSON did not parse");
		return false;
	}
	if (!Root->TryGetStringField(TEXT("effect_id"), OutGraph.EffectId))
	{
		Root->TryGetStringField(TEXT("id"), OutGraph.EffectId);
	}
	if (OutGraph.EffectId.IsEmpty())
	{
		OutError = TEXT("graph is missing effect_id");
		return false;
	}
	OutGraph.EffectIndex = INDEX_NONE;
	double EffectIndex = 0.0;
	if (Root->TryGetNumberField(TEXT("effect_index"), EffectIndex) && EffectIndex >= 0.0 && EffectIndex <= static_cast<double>(MAX_int32))
	{
		OutGraph.EffectIndex = static_cast<int32>(EffectIndex);
	}
	OutGraph.Revision = 1;
	const TSharedPtr<FJsonObject>* RevObj = nullptr;
	if (Root->TryGetObjectField(TEXT("rev"), RevObj) && RevObj && (*RevObj)->HasField(TEXT("number")))
	{
		OutGraph.Revision = static_cast<int32>((*RevObj)->GetNumberField(TEXT("number")));
	}
	else if (Root->HasField(TEXT("revision")))
	{
		OutGraph.Revision = static_cast<int32>(Root->GetNumberField(TEXT("revision")));
	}

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	if (!Root->TryGetArrayField(TEXT("nodes"), Nodes))
	{
		OutError = TEXT("graph is missing nodes");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Nodes)
	{
		const TSharedPtr<FJsonObject> NodeObj = Value->AsObject();
		if (!NodeObj.IsValid())
		{
			OutError = TEXT("node is not an object");
			return false;
		}
		FEffectGraphNode Node;
		NodeObj->TryGetStringField(TEXT("id"), Node.Id);
		NodeObj->TryGetStringField(TEXT("type"), Node.Type);
		const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
		if (NodeObj->TryGetArrayField(TEXT("inputs"), Inputs))
		{
			for (const TSharedPtr<FJsonValue>& Input : *Inputs)
			{
				Node.Inputs.Add(Input->AsString());
			}
		}
		if (Node.Type == TEXT("subeffect"))
		{
			const TSharedPtr<FJsonObject>* Params = nullptr;
			const TSharedPtr<FJsonObject>* Ref = nullptr;
			if (NodeObj->TryGetObjectField(TEXT("params"), Params) && Params && (*Params)->TryGetObjectField(TEXT("ref"), Ref) && Ref)
			{
				(*Ref)->TryGetStringField(TEXT("effect_id"), Node.SubEffectId);
				FString RevText;
				if ((*Ref)->TryGetStringField(TEXT("rev"), RevText) && RevText == TEXT("latest"))
				{
					Node.SubEffectRev = INDEX_NONE;
				}
				else if ((*Ref)->HasField(TEXT("rev")))
				{
					Node.SubEffectRev = static_cast<int32>((*Ref)->GetNumberField(TEXT("rev")));
				}
			}
		}
		OutGraph.Nodes.Add(MoveTemp(Node));
	}
	return true;
}

void FEffectGraphRegistry::CollectAcceptedEffectIndexes(TSet<int32>& OutIndexes) const
{
	for (const FEffectGraph& Graph : Graphs)
	{
		if (Graph.EffectIndex != INDEX_NONE && Validate(Graph.EffectId, Graph.Revision).bAccepted)
		{
			OutIndexes.Add(Graph.EffectIndex);
		}
	}
}

int32 FEffectGraphRegistry::LoadStarterGraphs(FString* OutError)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelFinish"));
	if (!Plugin.IsValid())
	{
		if (OutError)
		{
			*OutError = TEXT("VoxelFinish plugin is not mounted");
		}
		return 0;
	}
	const FString Dir = FPaths::Combine(Plugin->GetContentDir(), TEXT("Effects/Starter"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.json")), true, false);
	int32 Loaded = 0;
	for (const FString& File : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(Dir, File)))
		{
			continue;
		}
		FEffectGraph Graph;
		FString Error;
		if (!ParseGraphJson(Text, Graph, Error) || !Add(Graph, &Error))
		{
			if (OutError && OutError->IsEmpty())
			{
				*OutError = Error;
			}
			continue;
		}
		++Loaded;
	}
	return Loaded;
}
