// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/GameplayStatics.h"

class UVBSaveGame;

// 저장 슬롯의 디스크 입출력. 기본값은 플랫폼 저장소이고, 시험은 이 함수들을 바꿔 실제 슬롯에 닿지 않는다.
struct VB_API FVBSaveDisk
{
	TFunction<void(UVBSaveGame*, const FString&, int32, const FAsyncSaveGameToSlotDelegate&)> Write;
	TFunction<void(const FString&, int32)> Delete;
	TFunction<bool(const FString&, int32)> Exists;
	TFunction<USaveGame*(const FString&, int32)> Read;
	TFunction<void(const FString&, int32, const FAsyncLoadGameFromSlotDelegate&)> ReadAsync;

	static FVBSaveDisk Platform();
};
