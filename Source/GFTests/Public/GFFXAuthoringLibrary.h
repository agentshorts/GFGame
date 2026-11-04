#pragma once

// Editor-only helpers for authoring Niagara assets from editor Python.
// Engine Niagara types such as stateless modules are not exposed to Python, so these read and
// write any edit property through its text form and request a system compile.

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GFFXAuthoringLibrary.generated.h"

class UNiagaraSystem;

UCLASS()
class GFTESTS_API UGFFXAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Names of the properties on the object's class, with their C++ type. */
	UFUNCTION(BlueprintCallable, Category = "GF|FX Authoring")
	static TArray<FString> ListProperties(UObject* Object);

	/** Exported text of one property. Empty when the property is missing. */
	UFUNCTION(BlueprintCallable, Category = "GF|FX Authoring")
	static FString GetPropertyText(UObject* Object, FName PropertyName);

	/** Imports text into one property and runs the edit notifications. False when the import fails. */
	UFUNCTION(BlueprintCallable, Category = "GF|FX Authoring")
	static bool SetPropertyText(UObject* Object, FName PropertyName, const FString& Text);

	/** Objects whose outer chain reaches Outer. */
	UFUNCTION(BlueprintCallable, Category = "GF|FX Authoring")
	static TArray<UObject*> InnerObjects(UObject* Outer);

	/** Requests a full compile and waits for it. False when the system is null or not valid afterwards. */
	UFUNCTION(BlueprintCallable, Category = "GF|FX Authoring")
	static bool CompileSystem(UNiagaraSystem* System);
};
