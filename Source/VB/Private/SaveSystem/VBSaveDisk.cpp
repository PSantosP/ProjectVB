// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBSaveDisk.h"

#include "SaveSystem/VBSaveGame.h"

FVBSaveDisk FVBSaveDisk::Platform()
{
	FVBSaveDisk Result;
	Result.Write = [](UVBSaveGame* Save, const FString& SlotName, int32 UserIndex, const FAsyncSaveGameToSlotDelegate& Completed)
	{
		UGameplayStatics::AsyncSaveGameToSlot(Save, SlotName, UserIndex, Completed);
	};
	Result.Delete = [](const FString& SlotName, int32 UserIndex) { UGameplayStatics::DeleteGameInSlot(SlotName, UserIndex); };
	Result.Exists = [](const FString& SlotName, int32 UserIndex) { return UGameplayStatics::DoesSaveGameExist(SlotName, UserIndex); };
	Result.Read = [](const FString& SlotName, int32 UserIndex) { return UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex); };
	Result.ReadAsync = [](const FString& SlotName, int32 UserIndex, const FAsyncLoadGameFromSlotDelegate& Completed)
	{
		UGameplayStatics::AsyncLoadGameFromSlot(SlotName, UserIndex, Completed);
	};
	return Result;
}
