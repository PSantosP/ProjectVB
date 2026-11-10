// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_Stagger.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h" // ShouldAbilityRespondToEvent - 창 켜짐 질의
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBHitReactConfig.h"
#include "VB/VB.h"

UVBGA_Stagger::UVBGA_Stagger()
{
	// 입력 GA 가 아니라 InputTag 없음. ActivationPolicy 를 OnSpawn 으로 두면 부여 즉시 켜진다.

	// Event.Combat.PoiseBroken 트리거. GiveAbility 만 하면 ASC 가 이벤트 맵에 등록한다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_PoiseBroken;
	AbilityTriggers.Add(Trigger);

	// 이름표. 처형 창이 열릴 때 부모 Ability.Reaction 으로 끊긴다. 막고 끊는 규칙은 전투 규칙 표에 있다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Reaction_Stagger));

	// SuperArmor 는 일부러 경직 막기에 넣지 않는다 - 끊김 정도는 MaxPoise 하나로 표현한다.

	// Staggered 의 단독 생산자(엔진이 EndAbility 에서 뗀다). 히트리액트에 같은 태그를 주지 않는다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Staggered);

	// 연속 경직마다 다시 틀려면 true 여야 한다(UVBGA_HitReact 와 같은 이유).
	bRetriggerInstancedAbility = true;
}

void UVBGA_Stagger::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	RunReaction(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void UVBGA_Stagger::RunReaction(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// HitReactConfig 미할당은 설정 오류다 - 무시하면 경직이 흔적 없이 사라진다(BP 클래스 디폴트 누락).
	if (!HitReactConfig)
	{
		VB_LOG(Warning, "%s: HitReactConfig 미할당 -> 경직 스킵. BP_GA_Stagger 에 DA_HitReact_Default 를 할당하라",
			*GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 피격자 = 이 GA 의 아바타. 방향 슬롯이 비면 무방향 몽타주로 떨어진다.
	const EVBHitDirection StaggerDirection = VBComputeHitDirection(
		GetAvatarActorFromActorInfo(), TriggerEventData ? TriggerEventData->Instigator.Get() : nullptr);
	UAnimMontage* StaggerMontage = HitReactConfig->ResolveStaggerMontage(StaggerDirection);
	if (!StaggerMontage)
	{
		VB_LOG(Warning, "%s: 경직 몽타주 해석 실패 -> 즉시 종료. DA_HitReact_Default 의 StaggerSet 을 저작하라",
			*GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 서버와 소유 클라가 각자 로컬 재생한다. bStopAllMontages=true 로 진행 중인 공격 몽타주를 끊는다.
	// bAllowInterruptAfterBlendOut 은 false 유지(UVBGA_HitReact 와 같은 이유).
	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				NAME_None,
				StaggerMontage,
				HitReactConfig->MontagePlayRate,
				NAME_None,
				true,
				1.0f,
				0.0f,
				false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->ReadyForActivation();
}

bool UVBGA_Stagger::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	// 기절 게이지 소진만 거른다(헤더 주석). 판정은 피해자의 창 설정이 한다 - 같은 사건의 트리거 순서와 무관하다.
	if (!Payload || Payload->EventTag != VBGameplayTags::Event_Combat_PoiseBroken || !ActorInfo)
	{
		return true;
	}

	UAbilitySystemComponent* OwnerASC = ActorInfo->AbilitySystemComponent.Get();
	if (UVBAbilitySystemStatics::IsFinisherWindowArmed(OwnerASC))
	{
		VB_LOG(Log, "%s: 기절 게이지 소진을 받지 않는다 - 처형 창이 켜져 있다", *GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}
	// 기절 게이지로 창을 여는 적의 파탄은 창의 몫이다. 태그의 단독 생산자는 창 능력의 부여(UVBGA_FinisherWindow::OnGiveAbility)다.
	if (OwnerASC && OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PostureExecutable))
	{
		VB_LOG(Log, "%s: 기절 게이지 소진은 처형 창이 받는다 - 경직 없음", *GetNameSafe(ActorInfo->AvatarActor.Get()));
		return false;
	}

	return true;
}

void UVBGA_Stagger::OnStaggerEnded()
{
	// 몽타주 4개 델리게이트 공통 종료. 정리할 상태는 엔진이 EndAbility 에서 푼다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
