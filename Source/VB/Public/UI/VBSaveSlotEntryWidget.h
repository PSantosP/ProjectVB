// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveSlotEntryWidget.generated.h"

/** 슬롯 목록의 한 항목. 슬롯 메타 하나를 표시하고 눌림을 알린다. */
// 항목이 슬롯 번호를 실어 보내는 요청. 목록이 구독한다(항목은 목록을 모른다).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBSlotEntryRequest, int32, SlotIndex);

UCLASS()
class VB_API UVBSaveSlotEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category="VB|UI")
	FVBSlotEntryRequest OnSelectRequested;

	UPROPERTY(BlueprintAssignable, Category="VB|UI")
	FVBSlotEntryRequest OnDeleteRequested;

	// List 위젯이 항목 생성 시 호출. 메타 저장 후 BP 갱신 이벤트 발생.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void SetupFromMeta(const FVBSaveSlotMeta& InMeta);

	// 플레이타임 초 -> "HH:MM:SS" 표시 문자열.
	UFUNCTION(BlueprintPure, Category="VB|UI")
	static FString FormatPlayTime(double Seconds);

protected:
	virtual void NativeConstruct() override;

	// 표준 표시는 C++ 가 채운 뒤 부른다. BP 가 썸네일 같은 추가 표시를 덧붙이는 자리다.
	UFUNCTION(BlueprintImplementableEvent, Category="VB|UI")
	void OnMetaUpdated(const FVBSaveSlotMeta& InMeta);

	// 현재 항목이 표현하는 슬롯 메타.
	UPROPERTY(BlueprintReadOnly, Category="VB|UI")
	FVBSaveSlotMeta Meta;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSlotName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTimestamp;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtPlayTime;

	// 그 슬롯의 지금 챕터 제목(FVBSaveSlotMeta::ChapterTitle). 챕터가 없으면 접는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtChapter;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UButton> BtnSelect;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UButton> BtnDelete;

private:
	UFUNCTION()
	void HandleSelectClicked();

	UFUNCTION()
	void HandleDeleteClicked();

	// Meta 를 위젯 텍스트에 반영. 빈 슬롯이면 삭제 버튼을 숨긴다.
	void RefreshDisplay();
};
