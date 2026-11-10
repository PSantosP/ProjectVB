// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBAttributeBarWidget.h"
// 이 헤더를 통해 바 부품 타입을 쓰는 곳(HUD 시험 등)이 있어 그대로 둔다 - TextBlock 은 부모로 옮긴 HealthText 의 타입이다.
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBHealthBarWidget.generated.h"

class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 플레이어 체력, 보호막 바(WBP_HUD 의 BindWidget 자식) - 구성될 때 스스로 소유 PC 의 PlayerState ASC 에 바인딩하고, HUD 는 데이터를 전달하지 않는다
 * 체력 바, 잔상 바, 묶기는 부모(UVBAttributeBarWidget)가 하고, 여기는 보호막, 피격 흔들림, 저체력 맥동이다
 * InitializeWithASC 는 다른 소유자, 시험용이고, 자동 바인딩과 섞이면 마지막에 준 쪽이 이긴다
 */
UCLASS()
class VB_API UVBHealthBarWidget : public UVBAttributeBarWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	virtual void OnASCBound(UAbilitySystemComponent& ASC) override;
	virtual void OnUnbound() override;
	virtual void OnHealthRefreshed(float Percent) override;
	virtual void OnHealthDropped() override;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> ShieldBar;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> DamagePunch;

	UPROPERTY(Transient, meta=(BindWidgetAnim))
	TObjectPtr<UWidgetAnimation> LowHealthPulse;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float LowHealthThreshold = 0.3f;

	bool bIsLowHealth = false;

private:
	friend class FVBHUDRowTestAccess;

	FVBAttributeSubscription ShieldChanged;
	FVBAttributeSubscription MaxShieldChanged;
	// 체력은 PlayerState 의 ASC 가 관리한다. 폰의 ASC 캐시는 클라에서 OnRep_PlayerState 뒤에야 찬다.
	FVBPlayerStateBinding PlayerStateSet;

	void OnShieldChanged(const FOnAttributeChangeData& Data);
	void OnMaxShieldChanged(const FOnAttributeChangeData& Data);

	float CurrentShield = 0.0f;
	float CurrentMaxShield = 1.0f;

	void UpdateShieldBar();
};
