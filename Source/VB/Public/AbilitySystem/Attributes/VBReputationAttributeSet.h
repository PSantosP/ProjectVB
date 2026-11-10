// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAttributeSetBase.h"
#include "VBReputationAttributeSet.generated.h"

/**
 * 명성 AttributeSet. 치료 -> 증가, 처치 -> 감소. 축은 -100(학살자) ~ +100(치료사).
 */
UCLASS()
class VB_API UVBReputationAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()

public:
	UVBReputationAttributeSet();

	// 명성 수치 (-100 ~ +100)
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Reputation, Category="VB|Reputation")
	FGameplayAttributeData Reputation;
	ATTRIBUTE_ACCESSORS(UVBReputationAttributeSet, Reputation)

	// 메타 속성: 명성 변동값(비복제)
	UPROPERTY(BlueprintReadOnly, Category="VB|Reputation")
	FGameplayAttributeData IncomingReputationChange;
	ATTRIBUTE_ACCESSORS(UVBReputationAttributeSet, IncomingReputationChange)

protected:
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Reputation(const FGameplayAttributeData& OldReputation);

public:
	// 명성 축의 끝점(제품 상수). 속성으로 올리지 않는다 - 초기화가 비면 Clamp(x, 0, 0) 으로 조용히 0 에 묶인다.
	// 명성 바 위젯도 이 값을 읽는다.
	static constexpr float ReputationMin = -100.0f;
	static constexpr float ReputationMax = 100.0f;
};
