// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBSwingTargetBinding.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Actor.h"

void FVBSwingTargetBinding::Bind(AActor* InTarget, const FScriptDelegate& OnDestroyed,
	const FOnGameplayEffectTagCountChanged::FDelegate& OnDeadTag)
{
	Unbind();
	if (!InTarget)
	{
		return;
	}

	Target = InTarget;
	// AddDynamic 과 같은 일이다 - 그것도 (객체, 함수 이름)으로 묶은 FScriptDelegate 를 Add 한다. 들고 있어야 같은 것을 Remove 한다.
	DestroyedBinding = OnDestroyed;
	InTarget->OnDestroyed.Add(DestroyedBinding);
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InTarget))
	{
		TargetASC = ASC;
		DeadTagHandle = ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Add(OnDeadTag);
	}
}

void FVBSwingTargetBinding::Unbind()
{
	if (UAbilitySystemComponent* ASC = TargetASC.Get(); ASC && DeadTagHandle.IsValid())
	{
		ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadTagHandle);
	}

	if (AActor* Actor = Target.Get())
	{
		Actor->OnDestroyed.Remove(DestroyedBinding);
	}

	DeadTagHandle.Reset();
	TargetASC.Reset();
	Target.Reset();
	DestroyedBinding.Clear();
}
