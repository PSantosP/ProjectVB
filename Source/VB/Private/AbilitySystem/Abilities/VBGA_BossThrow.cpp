// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_BossThrow.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitSpec.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Combat/VBProjectile.h"
#include "Combat/VBProjectileConfig.h"
#include "Combat/VBProjectileRules.h"
#include "VB/VB.h"

UVBGA_BossThrow::UVBGA_BossThrow()
{
	// 피해는 서버에서만 나므로 클라가 이 능력을 켜지 못하게 한다(근접 공격과 같다).
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;

	// 적 공격의 식별자 - 경직이 막는 대상이고 근접 공격과 같은 줄에 선다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Enemy));

	// 이 태그의 추가와 제거가 공격 토큰의 시작과 해제다. 빠지면 토큰이 돌아오지 않는다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
}

void UVBGA_BossThrow::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bReleased = false;
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/false, /*bWasCancelled=*/true);
		return;
	}

	ACharacter* const Avatar = GetAvatarCharacter();
	if (!Avatar || !ProjectileConfig || !AnimMontage)
	{
		VB_LOG(Error, "%s: 던지기 설정 누락(아바타 %s, ProjectileConfig %s, AnimMontage %s) - 던지지 않는다", *GetName(),
			*GetNameSafe(Avatar), *GetNameSafe(ProjectileConfig), *GetNameSafe(AnimMontage));
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	// 예고: 근접 적 공격과 같은 태그, 큐. 길이는 몽타주 끝까지로 실어 보내고, 실제로는 놓는 순간 뗀다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		const float Seconds = AnimMontage->GetPlayLength() / FMath::Max(MontagePlayRate, UE_KINDA_SMALL_NUMBER);
		Telegraph.Grant(*OwnerASC, Avatar, ProjectileConfig.Get(), Seconds, ProjectileConfig->AttackAnswer);
	}

	// 노티가 첫 프레임에 올 수 있어 몽타주보다 먼저 듣는다.
	UAbilityTask_WaitGameplayEvent* ReleaseWait = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Montage_ProjectileRelease, /*OptionalExternalTarget=*/nullptr, /*OnlyTriggerOnce=*/true);
	ReleaseWait->EventReceived.AddDynamic(this, &UVBGA_BossThrow::OnRelease);
	ReleaseWait->ReadyForActivation();

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, AnimMontage, MontagePlayRate);
	MontageTask->OnCompleted.AddDynamic(this, &UVBGA_BossThrow::OnMontageFinished);
	MontageTask->OnBlendOut.AddDynamic(this, &UVBGA_BossThrow::OnMontageFinished);
	MontageTask->OnInterrupted.AddDynamic(this, &UVBGA_BossThrow::OnMontageCut);
	MontageTask->OnCancelled.AddDynamic(this, &UVBGA_BossThrow::OnMontageCut);
	MontageTask->ReadyForActivation();
}

void UVBGA_BossThrow::OnRelease(FGameplayEventData Payload)
{
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority() || bReleased)
	{
		return;
	}

	bReleased = true;
	Telegraph.Release();

	ACharacter* const Avatar = GetAvatarCharacter();
	UAbilitySystemComponent* const OwnerASC = GetAbilitySystemComponentFromActorInfo();
	UWorld* const World = GetWorld();
	if (!Avatar || !OwnerASC || !World || !ProjectileConfig)
	{
		return;
	}

	const USkeletalMeshComponent* const HandMesh = Avatar->GetMesh();
	if (!HandMesh || !HandMesh->DoesSocketExist(ReleaseSocketName))
	{
		VB_LOG(Error, "%s: %s 의 메시에 소켓 '%s' 가 없다 - 던지지 않는다", *GetName(), *Avatar->GetName(), *ReleaseSocketName.ToString());
		return;
	}

	const UVBEnemyCombatComponent* const Combat = Avatar->FindComponentByClass<UVBEnemyCombatComponent>();
	const AActor* const Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (!Target)
	{
		VB_LOG(Log, "%s: 놓는 순간 전투 표적이 없다 - 던지지 않는다", *GetName());
		return;
	}

	const UVBProjectileConfig& Config = *ProjectileConfig;
	const FVector Origin = HandMesh->GetSocketLocation(ReleaseSocketName);
	const FVector Aim = VBProjectileRules::AimPoint(Target->GetActorLocation(), Target->GetVelocity(), Origin,
		Config.Speed, Config.MaxLeadDistance);
	const FVector Velocity = VBProjectileRules::LaunchVelocity(Origin, Aim, Config.Speed, World->GetGravityZ() * Config.GravityScale);
	if (Velocity.IsNearlyZero())
	{
		VB_LOG(Warning, "%s: 겨눈 점이 손과 같다(속력 %.0f) - 던지지 않는다", *GetName(), Config.Speed);
		return;
	}

	const FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(Config.DamageEffectClass, GetAbilityLevel());
	if (!SpecHandle.IsValid())
	{
		VB_LOG(Error, "%s: %s 의 피해 효과로 스펙을 만들지 못했다 - 던지지 않는다", *GetName(), *Config.GetName());
		return;
	}

	const float AttackPower = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetAttackPowerAttribute());
	const FVBHitSpecValues Values = Config.MakeHitValues(AttackPower, Velocity.Rotation().Yaw);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = Avatar;
	SpawnParams.Instigator = Avatar;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBProjectile* const Projectile = World->SpawnActor<AVBProjectile>(AVBProjectile::StaticClass(), Origin, Velocity.Rotation(),
		SpawnParams);
	if (!Projectile)
	{
		VB_LOG(Error, "%s: 투사체를 스폰하지 못했다", *GetName());
		return;
	}

	Projectile->Launch(Config, *Avatar, SpecHandle, Values, Velocity);
}

void UVBGA_BossThrow::OnMontageFinished()
{
	VB_CLOG(!bReleased, Warning, "%s: 몽타주 %s 가 놓는 노티 없이 끝났다 - 노티(Event.Montage.ProjectileRelease)를 확인하라",
		*GetName(), *GetNameSafe(AnimMontage));
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}

void UVBGA_BossThrow::OnMontageCut()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
}

void UVBGA_BossThrow::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	Telegraph.Release();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
