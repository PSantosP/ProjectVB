// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/WeakObjectPtrTemplates.h"

class APlayerController;
class UVBPlayerReadinessComponent;
struct FVBPlayerPreparationSnapshot;

// 로딩 과정 판정 - 지켜보기 상태 없이 답하는 값 함수.
namespace VBPlayerLoadingRules
{
	// 로딩 과정 판정. Pawn 이 없으면 기다리고, 아니면 초기화, 저장, 복구, ability system 중 첫 이유를 낸다.
	bool ShouldShowLoadingScreen(const APlayerController& Controller, FString& OutReason);

	// blocked, unavailable mask 에서 허용 목록의 첫 로딩 이유를 고른다.
	VB_API bool SelectLoadingReason(const FVBPlayerPreparationSnapshot& Snapshot, FString& OutReason);
}

// 로컬 권위 PC 의 로딩 과정 등록과 지금 Pawn 의 준비 신호 구독을 맡는다. 준비 상태의 정본은 UVBPlayerReadinessComponent 다.
class FVBPlayerLoadingWatch
{
public:
	// 로딩 화면 담당 클래스에 PC 를 등록하고 현재 Pawn 의 준비 신호를 구독한다. 담당 클래스가 없으면 아무것도 안 한다.
	void Register(APlayerController& Controller);

	// Pawn 이 바뀌었다. 등록돼 있으면 새 Pawn 을 듣고 바로 다시 판정하게 한다.
	void HandlePawnChanged(APlayerController& Controller);

	// 구독을 풀고 등록을 해제한다.
	void Shutdown(APlayerController& Controller);

private:
	void RebindPreparationSource(APlayerController& Controller);

	TWeakObjectPtr<UVBPlayerReadinessComponent> BoundReadiness;
	FDelegateHandle PreparationChangedHandle;
	bool bRegistered = false;
};
