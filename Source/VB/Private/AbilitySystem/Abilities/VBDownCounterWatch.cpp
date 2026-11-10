// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBDownCounterWatch.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Data/VBHitReactConfig.h"

bool FVBDownCounterWatch::Arm(const FVBKnockdownSet& Set)
{
	if (!Set.HasDownAttack())
	{
		return false;
	}

	InputTag = Set.DownAttackInputTag;
	IntervalSeconds = Set.DownAttackIntervalSeconds;
	TriggerRadius = Set.DownAttackTriggerRadius;
	return true;
}

void FVBDownCounterWatch::WatchAbilityEnd(UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& SpecHandle,
	FGameplayAbilityEndedDelegate::FDelegate OnEnded)
{
	CounterSpec = SpecHandle;
	if (!AbilityEndedHandle.IsValid())
	{
		AbilityEndedHandle = ASC.OnAbilityEnded.Add(MoveTemp(OnEnded));
		AbilityEndedSource = &ASC;
	}
}

void FVBDownCounterWatch::NoteAnimSubscription(UAnimInstance& Anim)
{
	SubscribedAnim = &Anim;
}

UAnimInstance* FVBDownCounterWatch::TakeAnimSubscription()
{
	UAnimInstance* Anim = SubscribedAnim.Get();
	SubscribedAnim.Reset();
	return Anim;
}

void FVBDownCounterWatch::ReleaseAbilityEnd()
{
	if (UAbilitySystemComponent* ASC = AbilityEndedSource.Get())
	{
		ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
	}

	AbilityEndedSource.Reset();
	AbilityEndedHandle.Reset();
}
