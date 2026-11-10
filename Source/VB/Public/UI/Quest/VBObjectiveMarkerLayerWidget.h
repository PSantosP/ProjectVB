// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "UI/VBWorldIndicatorWidget.h"
#include "VBObjectiveMarkerLayerWidget.generated.h"

class APlayerState;
class UCanvasPanel;
class UCanvasPanelSlot;
class UVBObjectiveMarkerWidget;
class UVBQuestLogComponent;

/**
 * 목표 마커 층(전체 화면). 따라가는 퀘스트의 안 찬 목표 마커를 가까운 순 MaxMarkers 개 그린다.
 * 화면 밖이면 가장자리에 화살표로 그린다. 표시만 한다.
 */
UCLASS()
class VB_API UVBObjectiveMarkerLayerWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 마커가 놓이는 캔버스(이 위젯의 루트). 전체 화면이어야 가장자리 계산이 맞다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UCanvasPanel> MarkerCanvas;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBObjectiveMarkerWidget> MarkerClass;

	// 한꺼번에 그리는 최대 마커 수(가까운 것부터).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="1"))
	int32 MaxMarkers = 3;

	// 화면 밖 마커가 가장자리에서 떨어지는 거리(위젯 단위).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float EdgeMargin = 72.0f;

	// 마커 위치에서 위로 올려 그리는 높이(cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float WorldZOffset = 120.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D MarkerSize = FVector2D(72.0f, 72.0f);

private:
	UVBObjectiveMarkerWidget* GetOrCreateMarker(int32 Index);
	void HideFrom(int32 Index);
	// 일지는 PlayerState 에 있어 그것이 정해지면 다시 찾는다(원격 클라는 늦게 온다).
	void HandlePlayerStateSet(APlayerState* PlayerState);

	FVBPlayerStateSetRegistration PlayerStateSet;
	TWeakObjectPtr<UVBQuestLogComponent> QuestLog;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBObjectiveMarkerWidget>> Pool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UCanvasPanelSlot>> PoolSlots;
};
