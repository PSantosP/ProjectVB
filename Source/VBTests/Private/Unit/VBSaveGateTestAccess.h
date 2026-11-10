#pragma once

#include "SaveSystem/VBSaveGameSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
class FVBSaveGateTestAccess
{
public:
	static int32 GetActiveSlot(const UVBSaveGameSubsystem& Save) { return Save.ActiveSlotIndex; }
	static void SetStartContext(UVBSaveGameSubsystem& Save, const FVBGameStartContext& Context) { Save.GameStartContext = Context; }
	static bool GetPendingContinue(const UVBSaveGameSubsystem& Save) { return Save.bPendingContinueApply; }
	static void SetDispatch(UVBSaveGameSubsystem& Save,
		TFunction<void(UVBSaveGame*, const FString&, int32, const FAsyncSaveGameToSlotDelegate&)> Dispatch)
	{
		Save.Disk.Write = MoveTemp(Dispatch);
	}

	static void SetDeleteDispatch(UVBSaveGameSubsystem& Save, TFunction<void(const FString&, int32)> Dispatch)
	{
		Save.Disk.Delete = MoveTemp(Dispatch);
	}

	static void SetReadDisk(UVBSaveGameSubsystem& Save, TFunction<bool(const FString&, int32)> Exists,
		TFunction<USaveGame*(const FString&, int32)> Read)
	{
		Save.Disk.Exists = MoveTemp(Exists);
		Save.Disk.Read = MoveTemp(Read);
	}

	static void SetReadAsync(UVBSaveGameSubsystem& Save,
		TFunction<void(const FString&, int32, const FAsyncLoadGameFromSlotDelegate&)> ReadAsync)
	{
		Save.Disk.ReadAsync = MoveTemp(ReadAsync);
	}

	static int32 ResolveSlot(const UVBSaveGameSubsystem& Save) { return Save.ResolveSaveSlot(); }
	static void RestoreDisk(UVBSaveGameSubsystem& Save) { Save.Disk = FVBSaveDisk::Platform(); }
};
#endif
