// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "UI/VBComponentSubscription.h"
#include "UI/VBListDetailScreenWidget.h"
#include "VBQuestJournalScreenWidget.generated.h"

class APlayerState;
class UInputAction;
class UTextBlock;
class UVBQuestDefinition;
class UVBQuestLogComponent;

/**
 * 퀘스트 일지 화면(UI.Screen.Journal). 표시만 한다: OnQuestLogChanged 를 듣고 다시 그리고, 목표 글은 VBQuestText 가 만든다.
 * HUD 가 연다. 닫기는 뒤로 가기 또는 같은 입력을 한 번 더.
 */
UCLASS(Abstract)
class VB_API UVBQuestJournalScreenWidget : public UVBListDetailScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual int32 BuildRows() override;
	virtual void RefreshDetail() override;
	virtual void Select(int32 RowIndex) override;

	// 화면이 떠 있는 동안 이 입력을 한 번 더 누르면 닫는다(HUD 가 여는 입력과 같은 IA).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TObjectPtr<UInputAction> CloseInputAction;

	// 끝난 퀘스트 줄의 옆 글.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CompletedDetail = NSLOCTEXT("VBQuestJournal", "CompletedDetail", "완료");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtQuestTitle;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtQuestDetail;

private:
	void HandleCloseInput();
	void BindQuestLog(APlayerState* PlayerState);

	FVBPlayerStateSetRegistration PlayerStateSet;
	TVBComponentSubscription<UVBQuestLogComponent> QuestLog;
	TWeakObjectPtr<const UVBQuestDefinition> Selected;
	// 줄 번호 -> 퀘스트(줄 위젯은 번호만 알린다).
	TArray<TWeakObjectPtr<const UVBQuestDefinition>> RowQuests;
};
