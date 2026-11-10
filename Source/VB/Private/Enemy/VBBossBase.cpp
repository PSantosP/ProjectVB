// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Enemy/VBBossBase.h"
#include "Enemy/VBBossAttackRules.h"
#include "AI/VBEnemyCombatComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectTypes.h" // FOnAttributeChangeData
#include "Net/UnrealNetwork.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Data/VBBossConfig.h"
#include "Enemy/VBBossEncounterSubsystem.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "VB/VB.h"

AVBBossBase::AVBBossBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AVBBossBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 조건 없음 + 값이 바뀔 때만 OnRep(DOREPLIFETIME 과 같은 동작을 드러내 적음).
	DOREPLIFETIME_CONDITION_NOTIFY(AVBBossBase, CurrentPhaseIndex, COND_None, REPNOTIFY_OnChanged);
	// 인덱스와 같은 번치로 간다 - OnRep_CurrentPhaseIndex 가 읽는다.
	DOREPLIFETIME(AVBBossBase, bPhaseTransitionSilent);
	DOREPLIFETIME_CONDITION_NOTIFY(AVBBossBase, bEngaged, COND_None, REPNOTIFY_OnChanged);
}

const UVBBossConfig* AVBBossBase::GetBossConfig() const
{
	return Cast<UVBBossConfig>(GetEnemyConfig());
}

void AVBBossBase::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && AbilitySystemComponent)
	{
		HealthChangedHandle = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UVBHealthAttributeSet::GetHealthAttribute()).AddUObject(this, &AVBBossBase::OnHealthChanged);
	}

	// 보스 바의 싸우는 중 상태. 아래 설정 검사가 일찍 끝나도 걸리게 먼저 건다.
	if (HasAuthority())
	{
		if (UVBEnemyCombatComponent* Combat = GetCombatComponent())
		{
			InCombatChangedHandle = Combat->OnInCombatChanged.AddUObject(this, &AVBBossBase::HandleInCombatChanged);
			HandleInCombatChanged(Combat->IsInCombat());
		}
	}

	// 부팅 시 1회 계약 검사. 못 걸리면 문턱을 지나도 아무 일 없이 조용하다.
	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig)
	{
		VB_LOG(Warning, "보스 %s 의 EnemyConfig 가 UVBBossConfig 가 아니다(%s). 페이즈 없이 잡몹처럼 행동한다.",
			*GetName(), *GetNameSafe(GetEnemyConfig()));
		return;
	}

	// 추가 페이즈 태그마다 부여된 GA 가 있는지(첫 페이즈는 ValidateAttackAbilityContract 가 본다).
	if (HasAuthority() && AbilitySystemComponent)
	{
		for (int32 Index = 0; Index < BossConfig->AdditionalPhases.Num(); ++Index)
		{
			const FGameplayTag& Tag = BossConfig->AdditionalPhases[Index].AttackInputTag;
			if (!UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(AbilitySystemComponent, Tag))
			{
				VB_LOG(Warning, "보스 %s: AdditionalPhases[%d] 의 공격 태그 %s 에 맞는 GA 가 AbilitySet 에 없다. "
					"그 페이즈에서 공격하지 않는다.",
					*GetName(), Index, *Tag.ToString());
			}
		}
		// 상황 행도 같다 - 부여된 GA 가 없는 행은 조용히 빠지므로 여기서 한 번 알린다.
		for (int32 Phase = 0; Phase <= BossConfig->AdditionalPhases.Num(); ++Phase)
		{
			const TConstArrayView<FVBBossAttackRow> Rows = BossConfig->GetSituationRows(Phase);
			for (int32 Index = 0; Index < Rows.Num(); ++Index)
			{
				VB_CLOG(!UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(AbilitySystemComponent,
					Rows[Index].InputTag), Warning,
					"보스 %s: 페이즈 %d 상황 행 %d 의 태그 %s 에 맞는 GA 가 없다 - 그 행은 고르지 않는다", *GetName(), Phase, Index,
					*Rows[Index].InputTag.ToString());
			}
		}
	}
}

FGameplayTag AVBBossBase::GetCurrentAttackInputTag() const
{
	const UVBBossConfig* BossConfig = GetBossConfig();
	const UVBEnemyCombatComponent* Combat = GetCombatComponent();
	if (BossConfig && Combat && Combat->HasAttackToken())
	{
		const FVBBossAttackRow* Row = RowState.FindHeldRow(BossConfig->GetSituationRows(CurrentPhaseIndex), CurrentPhaseIndex);
		if (Row)
		{
			return Row->InputTag;
		}
	}

	return GetPhaseDefaultAttackInputTag();
}

void AVBBossBase::ForgetChosenAttack()
{
	RowState.Reset();
}

void AVBBossBase::NotifyAttackStarted()
{
	const UVBBossConfig* BossConfig = GetBossConfig();
	const UVBEnemyCombatComponent* Combat = GetCombatComponent();
	if (BossConfig && Combat && GetWorld() && Combat->HasAttackToken())
	{
		RowState.StartHeldRowCooldown(BossConfig->GetSituationRows(CurrentPhaseIndex), CurrentPhaseIndex, GetWorld()->GetTimeSeconds());
	}
}

void AVBBossBase::ChooseNextAttack()
{
	ForgetChosenAttack();
	const UVBBossConfig* BossConfig = GetBossConfig();
	UVBEnemyCombatComponent* Combat = GetCombatComponent();
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	const UWorld* World = GetWorld();
	// 반격 요청이 있으면 그 계획은 반격이라 행을 고르지 않는다.
	if (!HasAuthority() || !BossConfig || !Target || !World || Combat->WantsArmoredCounter())
	{
		return;
	}

	const TConstArrayView<FVBBossAttackRow> Rows = BossConfig->GetSituationRows(CurrentPhaseIndex);
	if (Rows.IsEmpty())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const FVBBossSituationTuning& Tuning = BossConfig->Situations;
	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	FVBBossSituationFacts Facts;
	Facts.bTargetBehind = VBBossAttackRules::IsTargetBehind(GetActorForwardVector(), ToTarget, Tuning.BehindAngleDegrees);
	Facts.bTargetFar = ToTarget.Size2D() > Tuning.FarDistance && Now - Combat->GetLastAttackStartTime() > Tuning.FarSeconds;
	Facts.bAfterHitBurst = RowState.IsAfterHitBurst(Now, Tuning.BurstSeconds, Tuning.BurstHits);
	const int32 Chosen = RowState.Choose(Rows, CurrentPhaseIndex, Facts, Now,
		[Combat](const FVBBossAttackRow& Row) { return Combat->CanActivateAbilityByInputTag(Row.InputTag); }, FMath::FRand());
	if (Chosen == INDEX_NONE)
	{
		return;
	}

	VB_LOG(Log, "보스 %s: 상황 행 %d(%s) - 기본 공격 대신 %s (등 뒤 %d · 멀다 %d · 몰아 맞음 %d)", *GetName(), Chosen,
		*UEnum::GetValueAsString(Rows[Chosen].Situation), *Rows[Chosen].InputTag.ToString(),
		Facts.bTargetBehind, Facts.bTargetFar, Facts.bAfterHitBurst);
}

FGameplayTag AVBBossBase::GetPhaseDefaultAttackInputTag() const
{
	if (CurrentPhaseIndex > 0)
	{
		if (const UVBBossConfig* BossConfig = GetBossConfig())
		{
			if (BossConfig->AdditionalPhases.IsValidIndex(CurrentPhaseIndex - 1))
			{
				return BossConfig->AdditionalPhases[CurrentPhaseIndex - 1].AttackInputTag;
			}
		}
	}

	return Super::GetCurrentAttackInputTag();
}

void AVBBossBase::OnHealthChanged(const struct FOnAttributeChangeData& Data)
{
	// 죽음 래치(HasStartedDeath)는 State.Dead 보다 먼저 서므로 그것을 묻는다.
	if (!HasAuthority() || IsDead() || HealthComponent->HasStartedDeath())
	{
		return;
	}

	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig || !HealthAttributeSet)
	{
		return;
	}

	const float MaxHealth = HealthAttributeSet->GetMaxHealth();
	if (MaxHealth <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float HealthRatio = Data.NewValue / MaxHealth;

	if (Data.NewValue < Data.OldValue && GetWorld())
	{
		RowState.RecordDamage(GetWorld()->GetTimeSeconds(), BossConfig->Situations.BurstSeconds);
	}

	// 지금 페이즈 뒤의 문턱을 차례로 본다. 한 대에 둘을 지나면 마지막 것으로 바로 간다.
	int32 TargetPhaseIndex = CurrentPhaseIndex;
	for (int32 Index = CurrentPhaseIndex; Index < BossConfig->AdditionalPhases.Num(); ++Index)
	{
		if (HealthRatio > BossConfig->AdditionalPhases[Index].HealthRatioThreshold)
		{
			break;
		}

		TargetPhaseIndex = Index + 1;
	}

	if (TargetPhaseIndex != CurrentPhaseIndex)
	{
		EnterPhase(TargetPhaseIndex);
	}
}

void AVBBossBase::EnterPhase(int32 NewPhaseIndex)
{
	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig || !BossConfig->AdditionalPhases.IsValidIndex(NewPhaseIndex - 1))
	{
		return;
	}

	// 엎드린 채 문턱을 지나면 연출 없이 페이즈만 바뀐다. 판단은 여기 한 번이고 서버와 OnRep 이 같은 값을 읽는다.
	bPhaseTransitionSilent = UVBAbilitySystemStatics::IsDowned(AbilitySystemComponent);

	const int32 PreviousPhaseIndex = CurrentPhaseIndex;
	CurrentPhaseIndex = NewPhaseIndex;
	const FVBBossPhase& Phase = BossConfig->AdditionalPhases[NewPhaseIndex - 1];
	VB_LOG(Log, "보스 %s 페이즈 %d -> %d (공격 태그 %s, 전환 몽타주 %s)",
		*GetName(), PreviousPhaseIndex, CurrentPhaseIndex, *Phase.AttackInputTag.ToString(),
		*GetNameSafe(Phase.TransitionMontage));

	// 스윙 중이든 예고 중이든 지금 공격을 부모 태그(Ability.Attack)로 끊는다.
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		AbilitySystemComponent->CancelAbilities(&AttackTags);
	}

	BeginPhaseTransition(Phase);
}

void AVBBossBase::BeginPhaseTransition(const FVBBossPhase& Phase)
{
	// 도는 전환 위에 또 들어오면 앞 것을 걷고 새로 건다. 앞 몽타주의 늦은 종료 알림은 세대 번호로 무시한다.
	EndPhaseTransition();
	++PhaseTransitionSerial;

	if (!Phase.TransitionMontage)
	{
		return;
	}

	// 엎드린 채면 연출을 틀지 않는다 - 서 있는 클립이 다운 자세를 덮으면 메시가 튄다. 판단은 bPhaseTransitionSilent 하나다.
	if (bPhaseTransitionSilent)
	{
		VB_LOG(Log, "%s 페이즈 전환 - 엎드린 채라 연출 생략(공격 풀만 바뀐다)", *GetName());
		return;
	}

	if (AbilitySystemComponent)
	{
		// 이 태그가 BT 의 이동, 공격 분기를 닫고 공격 GA 활성화도 막는다.
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Combat_PhaseTransition, 1,
			EGameplayTagReplicationState::CountToOwner);
		bPhaseTransitionActive = true;

		if (Phase.bSuperArmorDuringTransition)
		{
			AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor, 1,
				EGameplayTagReplicationState::CountToOwner);
			bGrantedTransitionSuperArmor = true;
		}
	}

	// 데코레이터는 한 틱 늦으므로 속도를 여기서 지워 미끄러지며 시작하지 않게 한다.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	PhaseTransitionStartLocation = GetActorLocation();
	PhaseTransitionStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	const float PlayedLength = PlayTransitionMontage(Phase);
	if (PlayedLength <= 0.0f)
	{
		// 재생이 안 됐는데 태그를 걸어 두면 보스가 영영 멈춘다. 즉시 걷는다.
		VB_LOG(Warning, "보스 %s: 전환 몽타주 %s 재생 실패. 전환 없이 페이즈만 바꾼다.",
			*GetName(), *GetNameSafe(Phase.TransitionMontage));
		EndPhaseTransition();
		return;
	}

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AVBBossBase::OnTransitionMontageEnded, PhaseTransitionSerial);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, Phase.TransitionMontage);
	}
}

void AVBBossBase::EndPhaseTransition()
{
	if (!bPhaseTransitionActive)
	{
		return;
	}

	bPhaseTransitionActive = false;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_PhaseTransition);
		if (bGrantedTransitionSuperArmor)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor);
		}
	}

	bGrantedTransitionSuperArmor = false;
}

void AVBBossBase::OnTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Serial)
{
	if (Serial != PhaseTransitionSerial)
	{
		// 앞 세대의 몽타주가 늦게 끝난 것. 지금 도는 전환의 것이 아니다.
		return;
	}
	// 끊겼어도 전환은 끝이다 - 태그가 남으면 보스가 영영 멈춘다.
	// 이동 거리는 전환 중 BT 가 움직였는지의 증거다(도발 클립 루트모션은 원점으로 돌아온다).
	const double Elapsed = GetWorld() ? GetWorld()->GetTimeSeconds() - PhaseTransitionStartTime : 0.0;
	const double Moved = FVector::Dist2D(GetActorLocation(), PhaseTransitionStartLocation);
	VB_LOG(Log, "보스 %s 전환 종료: %.2f초, 끊김=%d, 순이동 %.0fcm",
		*GetName(), Elapsed, bInterrupted ? 1 : 0, Moved);
	EndPhaseTransition();
}

void AVBBossBase::BeforeDeathTag(const FVBDeathFacts& Facts)
{
	EndPhaseTransition();
	Super::BeforeDeathTag(Facts);
}

void AVBBossBase::PresentDeath(const FVBDeathFacts& Facts)
{
	Super::PresentDeath(Facts);
	SetEngaged(false);
}

void AVBBossBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
			UVBHealthAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	}

	HealthChangedHandle.Reset();
	if (UVBEnemyCombatComponent* Combat = GetCombatComponent())
	{
		Combat->OnInCombatChanged.Remove(InCombatChangedHandle);
	}

	InCombatChangedHandle.Reset();
	// 사라지는 보스는 이 기계의 목록에서 뺀다(서버, 클라 각자). 복제 값은 바꾸지 않는다.
	if (UVBBossEncounterSubsystem* Encounters = GetWorld() ? GetWorld()->GetSubsystem<UVBBossEncounterSubsystem>() : nullptr)
	{
		Encounters->SetEngaged(this, false);
	}

	Super::EndPlay(EndPlayReason);
}

void AVBBossBase::HandleInCombatChanged(bool bInCombat)
{
	SetEngaged(bInCombat && !UVBAbilitySystemStatics::IsActorDead(this));
}

void AVBBossBase::SetEngaged(bool bNewEngaged)
{
	if (!HasAuthority() || bEngaged == bNewEngaged)
	{
		return;
	}

	bEngaged = bNewEngaged;
	PublishEngaged();
}

void AVBBossBase::OnRep_Engaged()
{
	PublishEngaged();
}

void AVBBossBase::PublishEngaged()
{
	if (UVBBossEncounterSubsystem* Encounters = GetWorld() ? GetWorld()->GetSubsystem<UVBBossEncounterSubsystem>() : nullptr)
	{
		Encounters->SetEngaged(this, bEngaged);
	}
}

float AVBBossBase::PlayTransitionMontage(const FVBBossPhase& Phase)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Phase.TransitionMontage)
	{
		return 0.0f;
	}

	return AnimInstance->Montage_Play(Phase.TransitionMontage, 1.0f);
}

void AVBBossBase::OnRep_CurrentPhaseIndex()
{
	// 원격 클라의 연출만. 초기 복제(BeginPlay 전)에는 이미 지난 전환이라 틀지 않는다.
	if (!HasActorBegunPlay())
	{
		return;
	}
	// 서버가 연출 없음으로 판단한 전환은 클라도 틀지 않는다(같은 번치로 온 값이라 안전).
	if (bPhaseTransitionSilent)
	{
		return;
	}

	const UVBBossConfig* BossConfig = GetBossConfig();
	if (BossConfig && BossConfig->AdditionalPhases.IsValidIndex(CurrentPhaseIndex - 1))
	{
		PlayTransitionMontage(BossConfig->AdditionalPhases[CurrentPhaseIndex - 1]);
	}
}
