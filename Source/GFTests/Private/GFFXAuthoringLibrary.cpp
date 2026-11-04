#include "GFFXAuthoringLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

TArray<FString> UGFFXAuthoringLibrary::ListProperties(UObject* Object)
{
	TArray<FString> Out;
	if (!Object)
	{
		return Out;
	}
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		Out.Add(FString::Printf(TEXT("%s %s"), *It->GetCPPType(), *It->GetName()));
	}
	return Out;
}

FString UGFFXAuthoringLibrary::GetPropertyText(UObject* Object, FName PropertyName)
{
	if (!Object)
	{
		return FString();
	}
	const FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return FString();
	}
	FString Text;
	Property->ExportTextItem_InContainer(Text, Object, nullptr, Object, PPF_None);
	return Text;
}

bool UGFFXAuthoringLibrary::SetPropertyText(UObject* Object, FName PropertyName, const FString& Text)
{
	if (!Object)
	{
		return false;
	}
	FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
	if (!Property)
	{
		return false;
	}
	Object->Modify();
	Object->PreEditChange(Property);
	const TCHAR* Result = Property->ImportText_InContainer(*Text, Object, Object, PPF_None);
	FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
	Object->PostEditChangeProperty(Event);
	return Result != nullptr;
}

TArray<UObject*> UGFFXAuthoringLibrary::InnerObjects(UObject* Outer)
{
	TArray<UObject*> Out;
	if (Outer)
	{
		GetObjectsWithOuter(Outer, Out, true);
	}
	return Out;
}

bool UGFFXAuthoringLibrary::CompileSystem(UNiagaraSystem* System)
{
	if (!System)
	{
		return false;
	}
	System->Modify();
	System->RequestCompile(true);
	System->WaitForCompilationComplete(true);
	System->MarkPackageDirty();
	return System->IsValid();
}
