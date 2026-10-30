#include "GFModeTypes.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

void FGFModeBytes::Write(UScriptStruct* Struct, const void* Data, TArray<uint8>& Out)
{
	Out.Reset();
	FMemoryWriter Writer(Out, /*bIsPersistent*/ true);
	FObjectAndNameAsStringProxyArchive Ar(Writer, /*bLoadIfFindFails*/ false);
	Ar.ArIsSaveGame = true;
	Struct->SerializeItem(Ar, const_cast<void*>(Data), nullptr);
}

bool FGFModeBytes::Read(UScriptStruct* Struct, void* Data, const TArray<uint8>& In)
{
	if (In.IsEmpty())
	{
		return false;
	}
	FMemoryReader Reader(In, /*bIsPersistent*/ true);
	FObjectAndNameAsStringProxyArchive Ar(Reader, /*bLoadIfFindFails*/ true);
	Ar.ArIsSaveGame = true;
	Struct->SerializeItem(Ar, Data, nullptr);
	return !Reader.IsError();
}
