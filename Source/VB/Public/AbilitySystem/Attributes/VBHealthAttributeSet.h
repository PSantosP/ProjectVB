// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAttributeSetBase.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "VBHealthAttributeSet.generated.h"

struct FGameplayEffectModCallbackData;

/**
 * 체력/실드 AttributeSet. 메타 속성 IncomingDamage/IncomingHealing 을 PostGameplayEffectExecute 에서 처리한다.
 * Health 가 0 이하로 넘는 쓰기에서 OnOutOfHealth 를 낸다 - 죽음 단계는 UVBHealthComponent 몫.
 */
UCLASS()
class VB_API UVBHealthAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()
	
public:
	UVBHealthAttributeSet();
	
	// 실제 Attribute (복제됨)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="VB|Health")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, Health)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="VB|Health")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, MaxHealth)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Shield, Category="VB|Health")
	FGameplayAttributeData Shield;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, Shield)
	
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxShield, Category="VB|Health")
	FGameplayAttributeData MaxShield;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, MaxShield)
	
	// Meta Attribute(복제안함, 계산용)
	UPROPERTY(BlueprintReadOnly, Category="VB|Health")
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, IncomingDamage)
	
	UPROPERTY(BlueprintReadOnly, Category="VB|Health")
	FGameplayAttributeData IncomingHealing;
	ATTRIBUTE_ACCESSORS(UVBHealthAttributeSet, IncomingHealing)
	
	// Health 가 0 이하로 넘는 쓰기마다 한 번(권위 쪽). mutable 인 이유: 듣는 쪽이 const GetSet<> 으로 묶는다.
	DECLARE_MULTICAST_DELEGATE_OneParam(FVBOutOfHealth, const FVBDeathFacts&);
	mutable FVBOutOfHealth OnOutOfHealth;

protected:
	// Attribute 변경 직전 -> 클램핑
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	// 현재값이 바뀐 직후(순간, 무한 GE 모두) -> 상한이 바뀌면 체력, 보호막 비율을 지킨다(서버).
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	
	// GE 적용 직후 -> 데미지/힐 파이프라인
	// 받지 않는 타격(ShouldIgnoreHit)은 여기서 버린다 - 적용도 Post 도 일어나지 않는다.
	virtual bool PreGameplayEffectExecute(struct FGameplayEffectModCallbackData& Data) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;
	
	// 복제
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// OnRep 함수 (GAMEPLAYATTRIBUTE_REPNOTIFY 매크로 사용)
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldHealth);
	
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
	
	UFUNCTION()
	void OnRep_Shield(const FGameplayAttributeData& OldShield);
	
	UFUNCTION()
	void OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield);

private:
	// IncomingDamage 하나를 소비한다: 가드, 실드, 체력, 반응, 충전 공개 순서.
	void ProcessIncomingDamage(const FGameplayEffectModCallbackData& Data);

	// 받는 쪽 피해 로그 한 줄을 남긴다.
	void LogIncomingDamage(const FGameplayEffectModCallbackData& Data, float DamageValue) const;

	// 실드, 체력 순서로 깎고 실드 뒤에 남았던 피해를 돌려준다.
	float TakeShieldThenHealth(float DamageValue, float& AppliedChargeDamage);

	// 이 캐릭터의 체력 하한. 죽지 않는 플레이어 규칙이면 1, 아니면 0 이다.
	float ResolveHealthFloor() const;

	// IncomingHealing 하나를 소비한다.
	void ProcessIncomingHealing();

	// 실행 중인 효과의 죽음 사실 스택. Pre 에서 쌓고 Post 끝에서 꺼낸다(한 효과 실행 동안만 산다).
	TArray<FVBDeathFacts, TInlineAllocator<2>> ExecutingDeathFacts;
};
