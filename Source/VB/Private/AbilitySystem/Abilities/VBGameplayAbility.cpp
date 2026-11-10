// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Types/VBReportOnce.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "VB/VB.h" // VB_LOG - BP ActivateAbility 본문이 있으면 소리내어 밝힌다

UVBGameplayAbility::UVBGameplayAbility()
{
	// 네트워크: ServerInitiated - 클라에도 인스턴스가 있어 몽타주를 로컬 재생한다.
	// 서버 전용 부작용(GE, Spawn, CMC)은 파생 GA 가 IsNetAuthority() 로 가드한다.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Instancing: 액터당 1개 인스턴스 (ServerInitiated 복제의 필수 조건)
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UVBGameplayAbility::DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::DoesAbilitySatisfyTagRequirements(AbilitySystemComponent, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 판정은 ASC(CanStartUnderCombatRules)에 있다. 여기는 그 판정에 닿을 수 없는 두 경우만 거절하고 경고한다.
	const UVBAbilitySystemComponent* VBASC = Cast<UVBAbilitySystemComponent>(&AbilitySystemComponent);
	if (!VBASC || GetAssetTags().IsEmpty())
	{
		if (VBFirstReportFor(GetClass()))
		{
			VB_LOG(Error, "%s 를 켜지 않는다 - %s", *GetClass()->GetName(), !VBASC
				? *FString::Printf(TEXT("ASC %s 가 UVBAbilitySystemComponent 가 아니라 전투 규칙 표를 들 수 없다."), *GetNameSafe(&AbilitySystemComponent))
				: TEXT("이름표(AssetTags)가 비어 전투 규칙 표의 어느 줄에도 안 걸린다. 생성자에서 SetAssetTags 로 단다."));
		}

		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(VBGameplayTags::Ability_Fail_Unknown);
		}

		return false;
	}

	return VBASC->CanStartUnderCombatRules(GetAssetTags(), OptionalRelevantTags);
}

UVBAbilitySystemComponent* UVBGameplayAbility::GetVBAbilitySystemComponent() const
{
	return Cast<UVBAbilitySystemComponent>(GetAbilitySystemComponentFromActorInfo());
}

ACharacter* UVBGameplayAbility::GetAvatarCharacter() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
}

UVBWeaponStateComponent* UVBGameplayAbility::GetWeaponStateFromActorInfo() const
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	return ActorInfo ? UVBWeaponStateComponent::FindFor(ActorInfo->AvatarActor.Get()) : nullptr;
}

void UVBGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	
	// OnSpawn 정책: 부여 즉시 활성화 (패시브용)
	if (ActivationPolicy == EVBAbilityActivationPolicy::OnSpawn)
	{
		if (ActorInfo && !Spec.IsActive())
		{
			ActorInfo->AbilitySystemComponent->TryActivateAbility(Spec.Handle);
		}
	}
}

void UVBGameplayAbility::PlayHitStop(float RealDuration, float TimeDilation)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}
	
	FGameplayCueParameters CueParams;
	CueParams.RawMagnitude = RealDuration;
	CueParams.NormalizedMagnitude = TimeDilation;
	ASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitStop, CueParams);
}

void UVBGameplayAbility::PlayHitStop(float RealDuration, float TimeDilation, const TArray<AActor*>& Victims, const UObject* AttackConfig)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return;
	}

	FGameplayCueParameters CueParams;
	CueParams.RawMagnitude = RealDuration;
	CueParams.NormalizedMagnitude = TimeDilation;
	CueParams.SourceObject = AttackConfig;
	CueParams.EffectContext = ASC->MakeEffectContext();
	CueParams.EffectContext.AddActors(TArray<TWeakObjectPtr<AActor>>(Victims));
	ASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_HitStop, CueParams);
}

void UVBGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Super 를 부르지 않는다 - 엔진 기본은 BP 본문(K2_ActivateAbility)만 돌리는데 이 프로젝트에서 그 경로는 금지다.
	// BP 본문이 있으면 조용히 안 돌므로 경고한다.
	if (bHasBlueprintActivate || bHasBlueprintActivateFromEvent)
	{
		// VB_LOG 가 TEXT() 를 씌우므로 맨 리터럴을 한 줄로 전달한다(이어 붙이면 넓은/좁은 문자열이 섞인다).
		VB_LOG(Error, "%s 의 블루프린트에 ActivateAbility 이벤트 본문이 있다. 이 프로젝트는 그 경로를 안 쓰므로 본문이 실행되지 않는다. 로직을 ExecuteAbility(C++) 로 옮기거나 그래프에서 그 이벤트 노드를 지워라.",
			*GetName());
	}

	// 1단계: CommitAbility (모든 GA에 강제). 자원 판정이 무엇보다 먼저다.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 2단계: 자식의 실제 로직
	ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}






