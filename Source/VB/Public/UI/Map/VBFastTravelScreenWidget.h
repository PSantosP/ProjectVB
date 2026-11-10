// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBComponentSubscription.h"
#include "UI/VBListDetailScreenWidget.h"
#include "GameplayTagContainer.h"
#include "Map/VBFastTravelComponent.h"
#include "VBFastTravelScreenWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * 빠른 이동 목적지 화면(UI.Screen.FastTravel). 여닫기는 PC 가 관리하고, 이 화면은 뷰만 그린다.
 * 버튼 상태는 표시용이고 판정은 서버가 한다. 뒤로 가기, 닫기 = 닫아 달라는 요청.
 */
UCLASS(Abstract)
class VB_API UVBFastTravelScreenWidget : public UVBListDetailScreenWidget
{
	GENERATED_BODY()

public:
	UVBFastTravelScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual int32 BuildRows() override;
	virtual void RefreshDetail() override;
	virtual void Select(int32 RowIndex) override;
	virtual TArray<UWidget*> GetDetailActions() const override;

	// 줄 옆 글. {0} = 지역 이름(지역 정의의 DisplayName, 없으면 비움).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText RegionFormat;

	// 못 가는 까닭(Ok 는 비운다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBFastTravelResult, FText> ResultTexts;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtTitle;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtDestination;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtStatus;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnTravel;

private:
	void RequestCloseTravel();
	FText RegionName(const FGameplayTag& RegionId) const;

	UFUNCTION()
	void HandleTravelClicked();

	TVBComponentSubscription<UVBFastTravelComponent> FastTravel;
	// 목록 줄 번호 -> 목적지 ID. 고른 것은 ID 로 들어 보기가 바뀌어도 유지된다.
	TArray<FGameplayTag> RowPoints;
	FGameplayTag SelectedPoint;
};
