// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBWorldIndicatorLayerWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UVBInteractPromptWidget;
class UVBObjectiveMarkerLayerWidget;
class UVBRegisteredIndicatorsWidget;
class UVBOffscreenThreatWidget;
class UVBNearbyEnemyBarsWidget;
class UVBTargetHealthIndicatorWidget;
class UVBWorldIndicatorWidget;

/**
 * 월드 표시기 층(WBP_WorldIndicatorLayer, 전체 화면). HUD 가 맨 아래(ZOrder -1)에 붙인다.
 * 갈래(목표 마커, 화면 밖 위협, 상호작용 프롬프트)를 겹쳐 두고 매 틱 FVBIndicatorView 로 갈래마다 UpdateIndicator 를 부른다.
 * 무엇을 보일지는 갈래가 정하고, 이 층은 데이터를 구독하지 않는다.
 */
UCLASS()
class VB_API UVBWorldIndicatorLayerWidget : public UUserWidget
{
	GENERATED_BODY()

	friend class FVBHUDRowTestAccess;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 갈래가 놓이는 캔버스(이 위젯의 루트). 전체 화면이어야 가장자리 계산이 맞다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UCanvasPanel> IndicatorCanvas;

	// 목표 마커 층(WBP_ObjectiveMarkerLayer). 캔버스 전체에 깐다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBObjectiveMarkerLayerWidget> ObjectiveMarkerLayerClass;

	// 화면 밖 위협 화살표(WBP_OffscreenThreat).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBOffscreenThreatWidget> OffscreenThreatClass;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D OffscreenThreatSize = FVector2D(64.0f, 64.0f);

	// 표시기 목록의 요청(처형 키 표시 등)을 요청마다 그리는 갈래(WBP_RegisteredIndicators). 캔버스 전체에 깔고 상호작용 프롬프트 아래에 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBRegisteredIndicatorsWidget> RegisteredIndicatorsClass;

	// 상호작용 프롬프트(WBP_InteractPrompt).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBInteractPromptWidget> InteractPromptClass;

	// 프롬프트 위젯의 어느 점이 대상 위치에 오나. (0.5, 1) = 아래 가운데 - 대상 바로 위에 뜬다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D InteractPromptAlignment = FVector2D(0.5f, 1.0f);

	// 근처 적 체력 바 층(WBP_NearbyEnemyBars, 전체 화면). 락온 대상 바 아래.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBNearbyEnemyBarsWidget> NearbyEnemyBarsClass;

	// 락온 대상 체력 바(WBP_TargetHealthIndicator).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBTargetHealthIndicatorWidget> TargetHealthIndicatorClass;

	// 바 위젯의 어느 점이 대상 위 자리에 오나. (0.5, 1) = 아래 가운데.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D TargetHealthAlignment = FVector2D(0.5f, 1.0f);

private:
	// 같은 UObject 가 다시 구성돼도 한 번만 만든다 - NativeDestruct 는 Slate 리소스만 놓고 위젯 트리, 슬롯은 남는다(설계 11.1).
	void CreateIndicators();
	void AddIndicator(TSubclassOf<UVBWorldIndicatorWidget> Class, const TCHAR* What, ESlateVisibility InitialVisibility,
		TFunctionRef<void(UCanvasPanelSlot&)> Arrange);

	bool bIndicatorsCreated = false;

	// 붙인 순서 그대로. 자리는 갈래의 Slot 이 든다 - 따로 들지 않는다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBWorldIndicatorWidget>> Indicators;
};
