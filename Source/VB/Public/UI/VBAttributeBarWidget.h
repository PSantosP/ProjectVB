// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBAttributeSubscription.h"
#include "VBAttributeBarWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UTextBlock;
struct FOnAttributeChangeData;

// 바 위 숫자의 모양
UENUM(BlueprintType)
enum class EVBBarNumberFormat : uint8
{
	Hidden,
	Current, // 85
	CurrentAndMax, // 85 / 120
};

/**
 * 체력 바의 공통 기반 클래스 - ASC 의 Health/MaxHealth 를 구독해 바와 잔상 바를 움직이며, ASC 에 바인딩하고 해제하는 곳은 여기 하나다
 * 플레이어 바(UVBHealthBarWidget)와 적 바(UVBEnemyHealthBarWidget)가 상속하고, 누구에게 묶을지는 파생 쪽이 정한다
 * NativeDestruct 에서 놓고, 다시 구성되면 묶어 준 쪽이 다시 준다
 */
UCLASS(Abstract)
class VB_API UVBAttributeBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 다른 ASC 에 묶여 있었으면 풀고 묶고, 같은 ASC 면 아무것도 안 한다. nullptr 이면 풀기만 한다(마지막 값이 그대로 보인다).
	void InitializeWithASC(UAbilitySystemComponent* ASC);
	UAbilitySystemComponent* GetBoundASC() const { return BoundASC.Get(); }

	// 바를 만들어 붙이는 쪽(근처 적 바 등)이 자기 설정으로 덮을 때. 바로 다시 그린다.
	void SetHealthNumberFormat(EVBBarNumberFormat Format);

	// 반올림 규칙: 현재는 올림(살아 있으면 0.4 도 1 - 0 인데 살아 있는 일이 없게, 0 이하는 0), 최대는 반올림.
	static FText FormatHealthNumber(float Health, float MaxHealth, EVBBarNumberFormat Format);

protected:
	virtual void NativeDestruct() override;

	// 파생 갈래가 끼어드는 자리 - OnASCBound 는 구독 뒤 첫 갱신 전(구독은 FVBAttributeSubscription), OnUnbound 는 풀기 뒤 늘
	// OnHealthRefreshed 는 비율이 다시 정해질 때마다, OnHealthDropped 는 체력이 줄었을 때만
	virtual void OnASCBound(UAbilitySystemComponent& ASC) {}
	virtual void OnUnbound() {}
	virtual void OnHealthRefreshed(float Percent) {}
	virtual void OnHealthDropped() {}

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> HealthBarDelay;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	// HealthText 에 쓰는 모양. 위젯 클래스 기본값이 데이터다(WBP 에서 바꾼다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	EVBBarNumberFormat HealthNumberFormat = EVBBarNumberFormat::CurrentAndMax;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float DelayBarUpdateInterval = 0.016f;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float DelayBarInterpSpeed = 5.0f;

	// 딜레이 바 틱 시작 지연(초) - 피격 직후 이 시간 뒤부터 잔상 바가 따라붙기 시작.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	float DelayBarStartDelay = 0.5f;

private:
	friend class FVBHUDRowTestAccess;

	void UnbindASC();
	void RefreshHealth();
	void StartDelayBar();
	void TickDelayBar();
	void OnHealthChanged(const FOnAttributeChangeData& Data);
	void OnMaxHealthChanged(const FOnAttributeChangeData& Data);

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FVBAttributeSubscription HealthChanged;
	FVBAttributeSubscription MaxHealthChanged;
	FTimerHandle DelayBarTimerHandle;

	float CurrentHealth = 0.0f;
	float CurrentMaxHealth = 1.0f;
};
