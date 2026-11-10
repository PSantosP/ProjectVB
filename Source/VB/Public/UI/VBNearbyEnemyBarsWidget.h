// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "UI/VBAttributeBarWidget.h" // EVBBarNumberFormat
#include "VBNearbyEnemyBarsWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UVBEnemyHealthBarWidget;

/**
 * 근처 적 체력 바 층(전체 화면). 월드 표시기 층의 한 갈래로 바를 모아 두고 다시 쓴다.
 * 고르기는 VBEnemyBars::PickNearby 로 SelectionInterval 마다, 자리는 매 프레임. 바는 페이드로 들고 난다.
 */
UCLASS()
class VB_API UVBNearbyEnemyBarsWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

protected:
	virtual void NativeDestruct() override;

	// 바가 놓이는 캔버스(이 위젯의 루트). 층 전체를 덮는다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UCanvasPanel> BarCanvas;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBEnemyHealthBarWidget> BarClass;

	// 이 거리(cm) 안의 적만.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float ShowRadius = 1500.0f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	int32 MaxBars = 4;

	// 고르기 주기(초). 자리는 매 프레임 따라간다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float SelectionInterval = 0.2f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float FadeInSeconds = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float FadeOutSeconds = 0.3f;

	// 이름 줄 + 바 + 숫자 줄의 크기.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FVector2D BarSize = FVector2D(140.0f, 50.0f);

	// 근처 바의 숫자 모양. 바 클래스 기본값을 이 층에서만 덮는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	EVBBarNumberFormat NumberFormat = EVBBarNumberFormat::CurrentAndMax;

	// 대상 마커 위치(IVBTargetable::GetTargetMarkerLocation)에서 위로 띄우는 거리(cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float HeightAboveMarker = 90.0f;

private:
	friend class FVBEnemyBarTestAccess;

	struct FEntry
	{
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<UVBEnemyHealthBarWidget> Bar;
		float Opacity = 0.0f;
		bool bLeaving = false;
	};

	void Select(const FVBIndicatorView& View);
	void Release(FEntry& Entry);
	UVBEnemyHealthBarWidget* AcquireBar();
	FVector AnchorOf(const AActor& Target) const;
	void FindExclusions(const FVBIndicatorView& View, const AActor*& OutLocked, const AActor*& OutBoss) const;

	TArray<FEntry> Entries;

	// 만든 바 전부(쉬는 것 포함). 재구성돼도 캔버스 자식으로 남는다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBEnemyHealthBarWidget>> Pool;

	double NextSelectTime = 0.0;
	double LastUpdateTime = -1.0;
};
