// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBAttributeSubscription.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBStaminaBarWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UVBAbilitySystemComponent;
struct FOnAttributeChangeData;
struct FVBAbilityFailureReport;

/**
 * 플레이어 기력 바(UI.Slot.HUD.Stamina). 소유 PlayerState ASC 의 Stamina 변화에 스스로 바인딩한다(틱 없음).
 * 가득이면 숨고 줄면 보인다. 기력 부족 거절(Ability.Fail.Cost)에 번쩍인다.
 */
UCLASS(Abstract)
class VB_API UVBStaminaBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 이 ASC 에 묶는다. nullptr 이면 풀고 숨는다. 마지막에 준 쪽이 이긴다.
	void InitializeWithASC(UAbilitySystemComponent* ASC);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> StaminaBar;

	// 이 비율 아래면 LowFillColor.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0", ClampMax="1"))
	float LowStaminaThreshold = 0.25f;

	// 다시 가득 찬 뒤 숨기까지(초). 바로 숨으면 회복이 끝난 순간이 안 보인다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float FullHideDelay = 1.0f;

	// 기력 부족 거절 때 번쩍이는 길이(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float RefusedFlashSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FLinearColor NormalFillColor = FLinearColor(0.85f, 0.75f, 0.25f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FLinearColor LowFillColor = FLinearColor(0.9f, 0.35f, 0.1f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FLinearColor RefusedFillColor = FLinearColor(1.0f, 0.1f, 0.1f, 1.0f);

private:
	friend class FVBStaminaBarTestAccess;

	void Unbind();
	void OnStaminaChanged(const FOnAttributeChangeData& Data);
	void OnMaxStaminaChanged(const FOnAttributeChangeData& Data);
	void HandleAbilityFailure(const FVBAbilityFailureReport& Report);
	void Refresh();
	void HideWhenStillFull();
	void EndRefusedFlash();
	bool IsFull() const;
	// 최대가 0 인데 거절이 오면 묶을 때마다 한 번 경고한다.
	void WarnIfNoMaxStamina(const TCHAR* Event);

	FVBPlayerStateBinding PlayerStateSet;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FVBAttributeSubscription StaminaChanged;
	FVBAttributeSubscription MaxStaminaChanged;
	// 실패 사건은 우리 ASC 에만 있다(다른 ASC 면 번쩍임만 없다).
	TWeakObjectPtr<UVBAbilitySystemComponent> FailureSource;
	FDelegateHandle FailureHandle;
	FTimerHandle HideTimer;
	FTimerHandle FlashTimer;

	float CurrentStamina = 0.0f;
	float CurrentMaxStamina = 0.0f;
	bool bFlashing = false;
	bool bWarnedNoMaxStamina = false;
};
