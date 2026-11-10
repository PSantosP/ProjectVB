// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBQuestTrackerWidget.generated.h"

class UTextBlock;
class UVBQuestLogComponent;

/**
 * HUD 의 퀘스트 목표 줄. 따라가는 퀘스트(GetTrackedEntry)의 글을 그리고, 없으면 접는다.
 * 구성 때 PlayerState 의 일지에 묶고 해체 때 푼다. 글 칸은 C++ 로 짓는다.
 */
UCLASS()
class VB_API UVBQuestTrackerWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	void BindQuestLog(UVBQuestLogComponent* Component);
	void Refresh();

	UPROPERTY()
	TObjectPtr<UTextBlock> TrackerText;

	FVBPlayerStateBinding PlayerStateSet;
	TWeakObjectPtr<UVBQuestLogComponent> QuestLog;
	FDelegateHandle QuestLogChangedHandle;
	// 알림 신호에도 다시 그린다(알림 RPC 와 목록 복제 중 어느 쪽이 먼저 올지 모른다).
	FDelegateHandle QuestNoticeHandle;
};
