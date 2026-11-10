// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MedicalAbilityBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "VB/VB.h"


bool UVBGA_MedicalAbilityBase::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	const bool bHasTarget = FindMedicalTarget(ActorInfo) != nullptr;
	// 질의는 판정 이유만 남긴다. 실제 활성화 실패는 ASC 가 알린다.
	if (!bHasTarget && OptionalRelevantTags)
	{
		OptionalRelevantTags->AddTag(VBGameplayTags::Ability_Fail_NoMedicalTarget);
	}

	return bHasTarget;
}

void UVBGA_MedicalAbilityBase::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 의료 행위는 플레이어 캐릭터만 받는다. 무기 상태가 없으면 캐릭터 조립이 깨진 것이다.
	UVBWeaponStateComponent* Weapons = GetWeaponStateFromActorInfo();
	if (!Weapons)
	{
		VB_LOG(Error, "%s: 아바타 %s 에 무기 상태 컴포넌트가 없다 - 의료 행위를 끝낸다", *GetName(), *GetNameSafe(GetAvatarActorFromActorInfo()));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Weapons->EnterCombat(); // 의료 행위도 발도 없는 전투 자극이다.

	// 대상 탐색(락온 우선 + 전방 원뿔) + GE 적용
	PerformMedicalTrace(ActorInfo);

	// 어빌리티 종료.
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

AActor* UVBGA_MedicalAbilityBase::FindMedicalTarget(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// GetAvatarCharacter() 를 쓰지 않는다 - 활성화 전(CanActivateAbility)에는 CurrentActorInfo 가 없다.
	ACharacter* AvatarCharacter = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!AvatarCharacter || !MedicalConfig)
	{
		return nullptr;
	}

	// 이 능력의 대상 규칙. 판정은 FindFinisherTarget 에 위임하고 사거리 값은 이 설정에 남긴다.
	return UVBAbilitySystemStatics::FindFinisherTarget(AvatarCharacter, MedicalConfig->MedicalRange,
		MedicalConfig->FinisherTargetRules);
}

void UVBGA_MedicalAbilityBase::PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo)
{
	// 찾기 + 적용(즉시 갈래). 연출 갈래는 시작 때 잡은 대상으로 ApplyMedicalEffects 를 직접 부른다.
	ApplyMedicalEffects(ActorInfo, FindMedicalTarget(ActorInfo));
}

void UVBGA_MedicalAbilityBase::ApplyMedicalEffects(const FGameplayAbilityActorInfo* ActorInfo, AActor* HitActor)
{
	// [서버 권위] GE 적용은 서버에서만. ServerInitiated 라 클라에서도 여기 온다.
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (!HitActor)
	{
		VB_LOG(Log, "%s: 타겟 없음", *GetName());
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || !MedicalConfig || !MedicalConfig->MedicalDamageEffectClass)
	{
		return;
	}

	// 힐링 GE 적용 (SetByCaller로 힐량 전달)
	FGameplayEffectSpecHandle DmgSpec =
		MakeOutgoingGameplayEffectSpec(MedicalConfig->MedicalDamageEffectClass, GetAbilityLevel());
	
	if (DmgSpec.IsValid())
	{
		// MedicalActionTag 자식에서 설정
		DmgSpec.Data.Get()->AddDynamicAssetTag(MedicalConfig->MedicalActionTag);

		DmgSpec.Data.Get()->SetSetByCallerMagnitude(
			VBGameplayTags::Data_MedicalAmount, MedicalConfig->BaseMedicalAmount);
		
		TargetASC->ApplyGameplayEffectSpecToSelf(*DmgSpec.Data.Get());
		VB_LOG(Log, "%s: %s 의료 행위 적용!", *GetName(), *HitActor->GetName());
	}
	
	// 명성 증가 GE 적용 (자기 자신에게)
	if (MedicalConfig->ReputationEffectClass)
	{
		FGameplayEffectSpecHandle RepSpec =
			MakeOutgoingGameplayEffectSpec(MedicalConfig->ReputationEffectClass, GetAbilityLevel());
		
		if (RepSpec.IsValid())
		{
			RepSpec.Data.Get()->SetSetByCallerMagnitude(
				VBGameplayTags::Data_ReputationChange, MedicalConfig->ReputationChangeOnMedical);
			
			// 명성은 자기 자신의 AttributeSet
			if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
			{
				OwnerASC->ApplyGameplayEffectSpecToSelf(*RepSpec.Data.Get());
				VB_LOG(Log, "%s : 명성 + %.0f", *GetName(), MedicalConfig->ReputationChangeOnMedical);
			}
		}
	}
}
