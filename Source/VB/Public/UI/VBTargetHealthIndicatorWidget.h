// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "VBTargetHealthIndicatorWidget.generated.h"

class UPanelWidget;
class UVBEnemyHealthBarWidget;

/**
 * 락온 대상 위의 체력 바. 월드 표시기 층의 한 갈래다. 대상은 UVBTargetLockComponent::GetLockedTarget.
 * 숨김: 대상 없음, 죽음, ASC 없음, 화면 밖, 보스 바가 이미 그 보스를 보여 줌(한 적에 바 하나).
 */
UCLASS()
class VB_API UVBTargetHealthIndicatorWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

	friend class FVBEnemyBarTestAccess;

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	// 바가 들어갈 자리. C++ 가 BarClass 로 한 번 만들어 붙인다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> BarSlot;

	// 바 생김새(WBP_EnemyHealthBar). 비면 바가 없다(경고).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBEnemyHealthBarWidget> BarClass;

	// 대상 마커 위치(IVBTargetable::GetTargetMarkerLocation)에서 위로 띄우는 거리(cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float HeightAboveMarker = 90.0f;

private:
	// 바를 보일 락온 대상. 없으면 nullptr.
	AActor* PickTarget(const APlayerController& PC) const;
	void Hide();

	// NativeOnInitialized 에서 한 번 만든 바.
	UPROPERTY(Transient)
	TObjectPtr<UVBEnemyHealthBarWidget> TargetBar;

	TWeakObjectPtr<AActor> ShownTarget;
	// ShownTarget 에 바를 묶었는가(false = ASC 없는 대상이라 숨김).
	bool bShownTargetHasBar = false;
};
