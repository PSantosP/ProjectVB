// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveSlotListWidget.generated.h"

class UVBSaveGameSubsystem;

/** 슬롯 목록의 용도. 선택 시 새 게임 시작 / 로드 / 현재 진행 저장(레벨을 열지 않음). */
UENUM(BlueprintType)
enum class EVBSlotListMode : uint8
{
	NewGame,
	Load,
	Save
};

/** 멀티 슬롯 선택 화면. 새 게임, 로드, 저장이 같은 목록을 쓰고 동작만 모드로 나눈다. */
UCLASS()
class VB_API UVBSaveSlotListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UVBSaveSlotListWidget(const FObjectInitializer& ObjectInitializer);

	// 화면을 열 때 BP 가 용도를 지정.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void SetListMode(EVBSlotListMode InMode);

	// 전체 슬롯 메타 조회. BP 가 이 배열로 엔트리 위젯들을 생성한다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	TArray<FVBSaveSlotMeta> RefreshSlots() const;

	// 슬롯 선택 시 BP 가 호출. 모드에 따라 새 게임 시작 / 로드로 분기.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void SelectSlot(int32 SlotIndex);

	// 슬롯 삭제 후 OnSlotsChanged 로 BP UI 갱신을 알린다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void DeleteSlot(int32 SlotIndex);

	// 목록을 비우고 현재 슬롯 메타로 다시 채운다. 화면 진입/삭제 후에 부른다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void RebuildEntries();

protected:
	virtual void NativeConstruct() override;

	// 항목 위젯 클래스. 비면 목록이 비고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<class UVBSaveSlotEntryWidget> EntryWidgetClass;

	// UMG 이름 바인딩(노출 키워드가 없어 Category 없음).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitle;

	// 모드별 화면 제목. 생성자가 기본값을 넣는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBSlotListMode, FText> ModeTitles;

	// 항목 사이 간격. 배치는 컨테이너 몫이라 항목이 아니라 여기 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FMargin EntrySpacing = FMargin(0.f, 3.f);

private:
	UFUNCTION()
	void HandleEntrySelect(int32 SlotIndex);

	UFUNCTION()
	void HandleEntryDelete(int32 SlotIndex);

	// 저장 완료 후 목록 갱신. 기다리는 슬롯이 아니면 무시한다.
	UFUNCTION()
	void HandleSaveCompleted(int32 SlotIndex, bool bSuccess);

	// 이 화면이 저장을 걸고 기다리는 슬롯(없으면 INDEX_NONE). 다른 길의 저장 완료를 거른다. 마지막 요청이 이긴다.
	int32 PendingSaveSlot = INDEX_NONE;

	// 모드에 따라 제목 문구를 맞춘다.
	void RefreshTitle();

protected:
	// 로드 완료 콜백 - 성공 시 게임 레벨 오픈.
	UFUNCTION()
	void HandleLoadCompleted(int32 SlotIndex, bool bSuccess);

	// 슬롯 목록이 바뀌었을 때(삭제 등) BP UI 재구성 훅.
	UFUNCTION(BlueprintImplementableEvent, Category="VB|UI")
	void OnSlotsChanged();

	EVBSlotListMode ListMode = EVBSlotListMode::Load;

	// 간격 경고는 목록당 한 번만.
	bool bEntrySpacingWarned = false;
};
