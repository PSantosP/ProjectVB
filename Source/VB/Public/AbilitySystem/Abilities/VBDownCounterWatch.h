// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h" // FGameplayAbilityEndedDelegate
#include "Engine/TimerHandle.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayTagContainer.h"

class UAbilitySystemComponent;
class UAnimInstance;
struct FVBKnockdownSet;

// 엎드린 채 휘두르는 반격의 감시 상태. 태그, 간격, 반경, 시도 시계와 반격 종료 구독을 맡는다.
// 표적과 토큰 판정, 능력 켜기는 UVBGA_Knockdown 이 맡는다. 구독은 건 대상을 기억해 그 대상에서만 뗀다.
struct VB_API FVBDownCounterWatch
{
	// 세트의 반격 값을 잡는다. 반격이 없는 세트면 false.
	bool Arm(const FVBKnockdownSet& Set);

	// 마지막 시도에서 간격이 지났으면 true.
	bool IsAttemptDue(double Now) const { return Now - LastAttemptTime >= IntervalSeconds; }

	// 시도 시각을 찍는다. 실패한 시도에도 간격을 둬 같은 경고가 감시 주기마다 찍히지 않게 한다.
	void MarkAttempt(double Now) { LastAttemptTime = Now; }

	// 몸 배율을 반영한 반경. 자산 값은 애니 고유 단위다.
	float GetTriggerRadius(float StrideScale) const { return TriggerRadius * StrideScale; }

	const FGameplayTag& GetInputTag() const { return InputTag; }

	// 이 스펙을 이번 반격으로 잡고 ASC 의 능력 종료 알림을 받는다. 이미 받고 있으면 스펙만 바꾼다.
	void WatchAbilityEnd(UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& SpecHandle,
		FGameplayAbilityEndedDelegate::FDelegate OnEnded);

	// 끝난 능력이 이번 반격이면 true.
	bool IsCounterAbility(const FGameplayAbilitySpecHandle& SpecHandle) const { return SpecHandle == CounterSpec; }

	// 몽타주 알림을 건 애님 인스턴스를 기억한다. 동적 바인딩은 UFUNCTION 주인인 능력이 한다.
	void NoteAnimSubscription(UAnimInstance& Anim);

	// 기억한 애님 인스턴스를 내주고 비운다. 건 적이 없으면 null.
	UAnimInstance* TakeAnimSubscription();

	// 능력 종료 알림을 건 ASC 에서 뗀다. 시도 시계는 활성화 사이에 남는다.
	void ReleaseAbilityEnd();

	FTimerHandle WatchTimer;

private:
	FGameplayTag InputTag;
	float IntervalSeconds = 0.0f;
	float TriggerRadius = 0.0f;
	double LastAttemptTime = -1.0e9;
	FGameplayAbilitySpecHandle CounterSpec;
	FDelegateHandle AbilityEndedHandle;
	TWeakObjectPtr<UAbilitySystemComponent> AbilityEndedSource;
	TWeakObjectPtr<UAnimInstance> SubscribedAnim;
};
