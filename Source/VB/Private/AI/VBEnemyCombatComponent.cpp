// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBEnemyCombatComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h" // UGameplayAbility::GetCooldownGameplayEffect 의 반환 타입
#include "GameFramework/Actor.h"
#include "Engine/World.h" // GetTimeSeconds - 슈퍼아머 반격 피격 창
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h" // PlanNextAttack - PeekAttackConfig
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h" // PlanNextAttack - MeasureMontageLungeCm
#include "Data/VBAttackConfig.h"
#include "Data/VBEnemyConfig.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Enemy/VBEnemyBase.h"
#include "AI/VBAIController.h" // PushStartDistanceToBlackboard
#include "GameFramework/Pawn.h"
#include "VB/VB.h" // VB_LOG

UVBEnemyCombatComponent::UVBEnemyCombatComponent()
{
	// 관측은 BTService, 조율은 코디네이터 타이머가 하므로 틱이 필요 없다.
	PrimaryComponentTick.bCanEverTick = false;

	// 복제하지 않는다. 역할/토큰/슬롯은 서버 전용 상태다.
}

void UVBEnemyCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 조율은 서버 결정이다. 클라에서 구독하면 없는 토큰을 해제하려 든다.
		return;
	}

	if (UAbilitySystemComponent* ASC = GetOwnerASC())
	{
		// 토큰 해제의 유일한 정상 경로. 공격 GA 가 어떻게 끝나든 이 태그가 빠진다.
		AttackingTagHandle = ASC->RegisterGameplayTagEvent(
			VBGameplayTags::State_Combat_Attacking,
			EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UVBEnemyCombatComponent::OnAttackingTagChanged);
		// 슈퍼아머 반격 근거(연속 피격)를 센다.
		HitReactingTagHandle = ASC->RegisterGameplayTagEvent(
			VBGameplayTags::State_Combat_HitReacting,
			EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &UVBEnemyCombatComponent::OnHitReactingTagChanged);
		// 같은 피격이 어느 타인지 채운다. 반격이 없는 아키타입은 구독하지 않는다.
		const UVBEnemyConfig* Config = GetCombatConfig();
		if (Config->ArmoredCounterAttackInputTag.IsValid() && Config->ArmoredCounterAfterHits > 0)
		{
			CombatHitEventHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit)
				.AddUObject(this, &UVBEnemyCombatComponent::OnCombatHitEvent);
		}
	}
}

void UVBEnemyCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AttackingTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->UnregisterGameplayTagEvent(AttackingTagHandle,
				VBGameplayTags::State_Combat_Attacking,
				EGameplayTagEventType::NewOrRemoved);
		}

		AttackingTagHandle.Reset();
	}

	if (HitReactingTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->UnregisterGameplayTagEvent(HitReactingTagHandle,
				VBGameplayTags::State_Combat_HitReacting,
				EGameplayTagEventType::NewOrRemoved);
		}

		HitReactingTagHandle.Reset();
	}

	if (CombatHitEventHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_Hit))
			{
				Delegate->Remove(CombatHitEventHandle);
			}
		}

		CombatHitEventHandle.Reset();
	}

	// 그룹에 파괴된 멤버가 남아 재계산 후보로 세이지 않게 바로 뺀다.
	LeaveCombat();

	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* UVBEnemyCombatComponent::GetOwnerASC() const
{
	// 구독/해제가 같은 ASC 를 보도록 획득 방법을 하나로 고정한다.
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

void UVBEnemyCombatComponent::SetCombatTarget(AActor* NewTarget)
{
	UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this);

	if (CombatTarget.Get() != NewTarget)
	{
		// 이전 그룹에서 먼저 빠진다. 해제가 CombatTarget 을 키로 쓰므로 갱신보다 먼저다.
		if (Coordinator)
		{
			Coordinator->UnregisterCombatant(this);
		}

		CombatTarget = NewTarget;
	}

	// 있음/없음은 따로 본다 - 약참조가 먼저 비면 위 비교가 놓친다.
	const bool bNowInCombat = NewTarget != nullptr;
	if (bNowInCombat != bInCombat)
	{
		bInCombat = bNowInCombat;
		if (bInCombat && GetWorld())
		{
			LastAttackStartTime = GetWorld()->GetTimeSeconds();
		}

		OnInCombatChanged.Broadcast(bInCombat);
	}

	if (!NewTarget)
	{
		// 전투가 끝나면 반격 근거도 비운다. 타겟만 바뀔 때는 남긴다(때린 상대로 다시 겨눌 수 있다).
		ArmoredCounter.Reset();
		return; // 이탈 완료. 위 등록 해제가 그룹에서 뺐다.
	}

	// 매 주기 등록한다. 멱등이라 기존 결정이 보존되고, 그룹이 해체된 뒤에도 다시 들어간다.
	if (Coordinator)
	{
		Coordinator->RegisterCombatant(this, NewTarget);
	}
}

void UVBEnemyCombatComponent::LeaveCombat()
{
	// 래치도 지운다. 남으면 새 그룹에서 하강 에지가 보유하지 않은 토큰을 해제한다.
	bWasAttacking = false;
	SetCombatTarget(nullptr);
}

EVBCombatRole UVBEnemyCombatComponent::GetRole() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetAssignedRole(this);
	}

	return EVBCombatRole::Idle;
}

bool UVBEnemyCombatComponent::HasAttackToken() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetHeldTokenCost(this) > 0;
	}

	return false;
}

FVector UVBEnemyCombatComponent::GetSlotLocation() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetAssignedSlotLocation(this);
	}

	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

bool UVBEnemyCombatComponent::IsReadyToAttack() const
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag AttackInputTag = GetAttackInputTag();
	if (!AttackInputTag.IsValid())
	{
		// 태그 미저작. 매 재계산마다 불리므로 경고는 부팅 시 ValidateAttackAbilityContract 가 1회 한다.
		return false;
	}

	return CanActivateAbilityByInputTag(AttackInputTag);
}

FGameplayTag UVBEnemyCombatComponent::GetAttackInputTag() const
{
	// 태그는 아키타입 자산이 갖는다(트리 하나가 여러 아키타입을 태우므로). 반격 조건이 맞으면 반격 태그.
	const UVBEnemyConfig* Config = GetCombatConfig(); // 절대 null 아님
	if (WantsArmoredCounter())
	{
		return Config->ArmoredCounterAttackInputTag;
	}
	// 보스 페이즈를 위해 액터에게 지금 태그를 묻는다. 잡몹은 AttackAbilityInputTag 그대로다.
	if (const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		return Enemy->GetCurrentAttackInputTag();
	}

	return Config->AttackAbilityInputTag;
}

bool UVBEnemyCombatComponent::CanActivateAbilityByInputTag(const FGameplayTag& InputTag) const
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC || !InputTag.IsValid())
	{
		return false;
	}

	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, InputTag);
	if (!Spec || !Spec->Ability)
	{
		return false;
	}
	// 인스턴스가 있으면 그것을, 없으면 CDO 를 묻는다(엔진 InternalTryActivateAbility 와 같은 선택).
	// 지역변수로 받는 이유: 생 포인터와 TObjectPtr 를 삼항에 바로 섞으면 C2445.
	const UGameplayAbility* PrimaryInstance = Spec->GetPrimaryInstance();
	const UGameplayAbility* AbilityToQuery = PrimaryInstance ? PrimaryInstance : Spec->Ability.Get();
	return AbilityToQuery->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get());
}

bool UVBEnemyCombatComponent::WantsArmoredCounter() const
{
	const UVBEnemyConfig* Config = GetCombatConfig();
	if (!Config->ArmoredCounterAttackInputTag.IsValid() || Config->ArmoredCounterAfterHits <= 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!ArmoredCounter.WantsCounter(Config->ArmoredCounterAfterHits, World->GetTimeSeconds(), Config->ArmoredCounterWindowSeconds))
	{
		return false;
	}
	// 반격 GA 가 쿨다운이면 보통 공격으로 떨어진다(참이면 아예 안 때리게 된다).
	return CanActivateAbilityByInputTag(Config->ArmoredCounterAttackInputTag);
}

void UVBEnemyCombatComponent::OnHitReactingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount <= 0)
	{
		bWasHitReacting = false;
		return;
	}

	if (bWasHitReacting)
	{
		return; // 1 -> 2 는 같은 피격 구간이다
	}

	bWasHitReacting = true;

	const UWorld* World = GetWorld();
	const UVBEnemyConfig* Config = GetCombatConfig();
	if (!World || !Config->ArmoredCounterAttackInputTag.IsValid())
	{
		return; // 반격이 없는 아키타입은 세지 않는다
	}

	ArmoredCounter.RecordHit(World->GetTimeSeconds(), Config->ArmoredCounterWindowSeconds);
}

void UVBEnemyCombatComponent::OnCombatHitEvent(const FGameplayEventData* Payload)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 능력 인스턴스는 서버에서만 채워진다. 이 구독은 서버 전용이다.
	const UGameplayAbility* Ability = Payload ? Payload->ContextHandle.GetAbilityInstance_NotReplicated() : nullptr;
	const UVBGA_MeleeAttackBase* Melee = Cast<UVBGA_MeleeAttackBase>(Ability);
	const double Now = World->GetTimeSeconds();
	// 에지가 기록하지 않은 피격은 근거가 아니다.
	if (!ArmoredCounter.AttachSwing(Now, Melee, Melee ? Melee->GetSwingSerial() : INDEX_NONE))
	{
		return;
	}

	const UVBEnemyConfig* Config = GetCombatConfig();
	const int32 Swings = ArmoredCounter.CountRecentSwings(Now, Config->ArmoredCounterWindowSeconds);
	if (Config->ArmoredCounterAfterHits > 0 && Swings >= Config->ArmoredCounterAfterHits)
	{
		// 다음 공격까지 잡아 둔다. 쿨다운 여부는 쓰는 순간 다시 묻는다.
		ArmoredCounter.HoldCounterUntil(Now + Config->ArmoredCounterPendingSeconds);
		VB_LOG(Log, "[Counter] %s 창 %.1f초 안 %d타(피격 %d) - 슈퍼아머 반격을 다음 공격까지 잡아 둠(최대 %.1f초, 지금 %s)", *GetNameSafe(GetOwner()),
			Config->ArmoredCounterWindowSeconds, Swings, ArmoredCounter.GetHitCount(), Config->ArmoredCounterPendingSeconds,
			CanActivateAbilityByInputTag(Config->ArmoredCounterAttackInputTag) ? TEXT("켤 수 있음") : TEXT("쿨다운 - 그동안은 보통 공격"));
	}
}

bool UVBEnemyCombatComponent::IsAttacking() const
{
	return UVBAbilitySystemStatics::IsAttacking(GetOwnerASC());
}

bool UVBEnemyCombatComponent::IsTelegraphing() const
{
	return UVBAbilitySystemStatics::IsTelegraphing(GetOwnerASC());
}

void UVBEnemyCombatComponent::ValidateAttackAbilityContract() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 어빌리티 부여는 서버에서만 일어난다. 클라에서는 거짓 경고가 된다.
		return;
	}

	const FGameplayTag AttackInputTag = GetAttackInputTag();
	if (!AttackInputTag.IsValid())
	{
		VB_LOG(Warning, "%s: EnemyConfig 의 AttackAbilityInputTag 가 비었다 - 이 적은 영원히 공격 후보가 "
			"되지 않는다(역할이 Surrounder 로 고정되어 BT 공격 분기에 도달조차 못 하므로 "
			"다른 로그는 한 줄도 남지 않는다)", *Owner->GetName());
		return;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		VB_LOG(Warning, "%s: ASC 가 없어 공격 어빌리티 계약을 검증할 수 없다", *Owner->GetName());
		return;
	}

	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, AttackInputTag);
	if (!Spec)
	{
		VB_LOG(Warning, "%s: AttackAbilityInputTag '%s' 에 맞는 어빌리티가 부여되지 않았다 - 이 적은 "
			"영원히 공격하지 않는다. AbilitySet 에 든 GA 의 InputTag 와 맞춰라",
			*Owner->GetName(), *AttackInputTag.ToString());
		return;
	}

	// 반격 태그도 같은 계약이다. GA 가 없으면 반격이 조용히 안 나온다.
	const FGameplayTag ArmoredTag = GetCombatConfig()->ArmoredCounterAttackInputTag;
	if (ArmoredTag.IsValid() && !UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, ArmoredTag))
	{
		VB_LOG(Warning, "%s: ArmoredCounterAttackInputTag '%s' 에 맞는 어빌리티가 부여되지 않았다 - 슈퍼아머 반격이 영영 나오지 않는다. "
			"AbilitySet 에 그 InputTag 의 GA 를 넣어라", *Owner->GetName(), *ArmoredTag.ToString());
	}

	// 쿨다운 GE 가 없으면 개별 적의 연타를 막는 것이 토큰 회전뿐임을 알린다.
	const UGameplayAbility* AbilityCDO = Spec->Ability.Get();
	const UGameplayEffect* CooldownGE = AbilityCDO ? AbilityCDO->GetCooldownGameplayEffect() : nullptr;
	if (!CooldownGE)
	{
		VB_LOG(Log, "%s: 공격 어빌리티에 쿨다운 GE 가 없다 - 연타를 막는 것은 어택 토큰 회전뿐이다",
			*Owner->GetName());
	}
}

const UVBEnemyConfig* UVBEnemyCombatComponent::GetCombatConfig() const
{
	if (const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		return Enemy->GetEnemyConfig(); // 미할당이면 CDO - 절대 null 아님
	}
	// 적이 아닌 액터에 붙은 경우 CDO 로 답한다.
	return GetDefault<UVBEnemyConfig>();
}

void UVBEnemyCombatComponent::OnAttackingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0)
	{
		// 상승 에지에서만 통지한다. 재트리거(1->2)에서 시계를 다시 찍으면 안전망이 늘어난다.
		if (!bWasAttacking)
		{
			bWasAttacking = true;
			LastAttackStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
			{
				Enemy->NotifyAttackStarted();
			}
			// 공격이 시작됐으면 반격 근거를 비운다.
			ArmoredCounter.Reset();
			// 임대 시계는 부여가 아닌 공격 시작부터 잰다(접근 시간을 재면 스윙 중에 만료된다).
			if (UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
			{
				Coordinator->NotifyAttackStarted(this);
			}
		}

		return;
	}

	if (!bWasAttacking)
	{
		// 상승 없이 온 콜백은 무시한다. 해제로 처리하면 보유하지 않은 토큰을 해제한다.
		return;
	}

	bWasAttacking = false;
	if (UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		Coordinator->ReleaseAttackToken(this);
	}
}

void UVBEnemyCombatComponent::PlanNextAttack(const FVector& TargetLocation)
{
	ClearPlannedAttack();
	if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		Enemy->ChooseNextAttack();
	}

	const AActor* Owner = GetOwner();
	UAbilitySystemComponent* ASC = GetOwnerASC();
	const FGameplayTag Tag = GetAttackInputTag();
	if (!Owner || !ASC || !Tag.IsValid())
	{
		return;
	}

	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, Tag);
	if (!Spec)
	{
		return;
	}
	// 인스턴스가 있으면 그것을(무브셋 해석이 인스턴스 상태를 볼 수 있다), 없으면 CDO.
	const UGameplayAbility* PrimaryInstance = Spec->GetPrimaryInstance();
	const UVBGA_MeleeAttackBase* AttackGA = Cast<UVBGA_MeleeAttackBase>(PrimaryInstance ? PrimaryInstance : Spec->Ability.Get());
	const UVBAttackConfig* Config = AttackGA ? AttackGA->PeekAttackConfig() : nullptr;
	if (!Config)
	{
		return;
	}

	const int32 VariantCount = Config->GetMontageVariantCount();
	if (VariantCount <= 0)
	{
		return;
	}

	// 시작 거리 = 클립 전진(메시 배율 반영) + 그 변형의 멈추는 거리.
	const float Scale = VBStrideScale::Resolve(Owner);
	const float Ratio = FMath::Max(1.0f, GetCombatConfig()->WarpLungeToleranceRatio);
	const float Distance = FVector::Dist2D(Owner->GetActorLocation(), TargetLocation);

	TArray<float> Lunges;
	TArray<float> Standoffs;
	TArray<FString> Names;
	Lunges.Reserve(VariantCount);
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		const FVBAttackMontageVariant Variant = Config->ResolveMontageVariant(Index);
		Lunges.Add(VBMeleeAttackRules::MeasureMontageLungeCm(Variant.Montage, Scale));
		Standoffs.Add(Config->GetVariantApproachOffset(Index, Scale));
		Names.Add(GetNameSafe(Variant.Montage));
	}

	// 후보 = 거리 >= 멈춤 + 전진/허용비 인 변형(더 가까우면 클립이 눌려 발이 민다). 순환 커서로 고른다.
	// 후보가 없으면 전진이 가장 짧은 것.
	int32 Chosen = INDEX_NONE;
	for (int32 Step = 0; Step < VariantCount && Chosen == INDEX_NONE; ++Step)
	{
		const int32 Index = (VariantPlanCursor + Step) % VariantCount;
		if (Distance >= Standoffs[Index] + Lunges[Index] / Ratio)
		{
			Chosen = Index;
		}
	}

	if (Chosen == INDEX_NONE)
	{
		Chosen = 0;
		for (int32 Index = 1; Index < VariantCount; ++Index)
		{
			if (Lunges[Index] < Lunges[Chosen])
			{
				Chosen = Index;
			}
		}
	}

	VariantPlanCursor = Chosen + 1;

	PlannedVariantIndex = Chosen;
	PlannedStartDistance = Lunges[Chosen] + Standoffs[Chosen];
	PushStartDistanceToBlackboard();

	FString Candidates;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		Candidates += FString::Printf(TEXT("%s%s %.0f+%.0f"), Index ? TEXT(" / ") : TEXT(""), *Names[Index], Lunges[Index],
			Standoffs[Index]);
	}

	VB_LOG(Log, "[공격 계획] %s 거리 %.0f -> %d번 %s (전진 %.0f + 멈춤 %.0f = 시작 %.0f) 후보 [%s]",
		*GetNameSafe(Owner), Distance, Chosen, *Names[Chosen], Lunges[Chosen], Standoffs[Chosen], PlannedStartDistance, *Candidates);
}

void UVBEnemyCombatComponent::ClearPlannedAttack()
{
	PlannedVariantIndex = INDEX_NONE;
	PlannedStartDistance = -1.0f;
	PushStartDistanceToBlackboard();
	if (AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		Enemy->ForgetChosenAttack();
	}
}

void UVBEnemyCombatComponent::PushStartDistanceToBlackboard() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AVBAIController* AI = Pawn ? Cast<AVBAIController>(Pawn->GetController()) : nullptr)
	{
		AI->PushAttackStartDistance(GetAttackStartDistance());
	}
}

int32 UVBEnemyCombatComponent::ConsumePlannedVariantIndex()
{
	if (!HasAttackToken())
	{
		PlannedVariantIndex = INDEX_NONE;
		return INDEX_NONE;
	}
	// 시작 거리는 남긴다 - 스윙 중에도 역할, 걸음이 같은 문턱을 본다.
	const int32 Index = PlannedVariantIndex;
	PlannedVariantIndex = INDEX_NONE;
	return Index;
}

float UVBEnemyCombatComponent::GetAttackStartDistance() const
{
	// 계획은 토큰이 있는 동안만 유효하다. 토큰을 회수하는 경로가 여럿이라 저장값 대신 현재 토큰 보유 여부를 확인한다.
	return (PlannedStartDistance > 0.0f && HasAttackToken()) ? PlannedStartDistance : GetCombatConfig()->AttackCommitDistance;
}

float UVBEnemyCombatComponent::GetAttackReleaseDistance() const
{
	// 히스테리시스 폭은 자산의 Release-Commit 그대로.
	const UVBEnemyConfig* Config = GetCombatConfig();
	return GetAttackStartDistance() + FMath::Max(0.0f, Config->AttackReleaseDistance - Config->AttackCommitDistance);
}
