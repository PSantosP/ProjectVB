// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "UI/Slots/VBUISlotSubsystem.h"
class UUserWidget;
class UCanvasPanel;
class UVBHUDLayoutData;
class UVBUISlotWidget;
namespace VBHUDLayout
{
	// 두 표시 영역이 같은 배치 형식과 등록 수명을 사용한다.
	VB_API void Build(UUserWidget& Owner, UCanvasPanel& Canvas, const UVBHUDLayoutData& Layout,
		TArray<TObjectPtr<UVBUISlotWidget>>& Slots, TArray<FVBUISlotHandle>& Handles);
}
