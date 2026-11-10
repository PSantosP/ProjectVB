// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"                // FGameplayEffectSpec(IsFinisherDamage)
#include "GameplayAbilitySpec.h"          // FScopedAbilityListLock / FGameplayAbilitySpec
#include "GenericTeamAgentInterface.h"    // FGenericTeamId::GetAttitude
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h" // InputTag 의 홈
#include "AbilitySystem/Abilities/VBGA_MedicalAbilityBase.h" // FindExecuteTarget - 처형 키 능력의 사거리, 도달 규칙
#include "GameFramework/Character.h"                   // FindFinisherTarget 의 공격자
#include "Character/VBTargetLockComponent.h"           // 락온 대상 / 전방 원뿔
#include "Engine/World.h"                          // 시야 게이트의 캡슐 스윕
#include "CollisionQueryParams.h"                   // SCENE_QUERY_STAT
#include "Enemy/VBEnemyBase.h"                         // bAllowFinisher 를 읽는 경로
#include "Data/VBEnemyConfig.h"
#include "UObject/ObjectKey.h"
#include "VB/VB.h"                         // VB_LOG

bool UVBAbilitySystemStatics::IsDead(const UAbilitySystemComponent* ASC)
{
	// State.Dead 를 읽는 곳은 여기 하나다(커밋 검사 state_tag_reads_check 가 지킨다). 사망 의미가 바뀌면 여기만 고친다. 붙이는 쪽은 사망 처리다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead);
}

bool UVBAbilitySystemStatics::IsAttacking(const UAbilitySystemComponent* ASC)
{
	// State.Combat.Attacking 질의가 코드에 등장하는 유일한 지점이다.
	// 태그 자체는 GA 들이 ActivationOwnedTags 로 부여하고, 읽는 쪽은 전부 여기를 거친다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
}

bool UVBAbilitySystemStatics::IsTelegraphing(const UAbilitySystemComponent* ASC)
{
	// 태그를 붙이고 떼는 곳은 UVBGA_EnemyMeleeAttack 하나다. 예고 시작에 붙고, 떼는 지점은 둘이다 -
	//   정상 진행의 BeginSwing 과, 취소, 경직으로 예고 도중 끝날 때의 EndAbility.
	// 읽는 쪽은 여기를 거친다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Telegraphing);
}

bool UVBAbilitySystemStatics::IsInFinisherWindow(const UAbilitySystemComponent* ASC)
{
	// 태그를 붙이고 떼는 곳은 UVBGA_FinisherWindow 하나다 - 반응이 끝나 창이 열릴 때(OpenWindow) 붙이고 EndAbility 에서 뗀다.
	// 창이 열려 있다는 것과 체력이 낮다는 것은 다른 말이다. 체력이 문턱 아래여도 창이 닫혀 있으면 false 다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindow);
}

bool UVBAbilitySystemStatics::IsFinisherWindowArmed(const UAbilitySystemComponent* ASC)
{
	// 생산자는 UVBGA_FinisherWindow 의 ActivationOwnedTags 하나다 - 켜지는 순간 붙고 엔진이 EndAbility 에서 뗀다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
}

bool UVBAbilitySystemStatics::ShouldIgnoreHit(const UAbilitySystemComponent* Victim, const FGameplayEffectSpec& Spec,
	const TCHAR** OutReason)
{
	const TCHAR* Reason = nullptr;
	if (Victim)
	{
		if (Victim->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Dodging))
		{
			Reason = TEXT("회피 무적");
		}
		else if (Victim->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing))
		{
			Reason = TEXT("처형 중 무적");
		}
		// 체력 문턱으로 켜진 창만 처형 아닌 피해를 무른다 - 게이지로 연 창은 일반 타격을 받는다(태그 생산자 UVBGA_FinisherWindow).
		else if (Victim->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindowImmune) && !IsFinisherDamage(Spec))
		{
			Reason = TEXT("체력 문턱 처형 창 - 처형 아닌 피해");
		}
	}

	if (OutReason)
	{
		*OutReason = Reason;
	}

	return Reason != nullptr;
}

bool UVBAbilitySystemStatics::ShouldIgnorePoiseHit(const UAbilitySystemComponent* Victim, const FGameplayEffectSpec& Spec,
	const TCHAR** OutReason)
{
	if (ShouldIgnoreHit(Victim, Spec, OutReason))
	{
		return true;
	}
	// 창이 켜진 적의 게이지는 다 찬 상태다 - 처형 피해도 게이지는 건드리지 않는다.
	if (IsFinisherWindowArmed(Victim))
	{
		if (OutReason)
		{
			*OutReason = TEXT("처형 창 - 게이지는 다 차 있다");
		}

		return true;
	}

	return false;
}

bool UVBAbilitySystemStatics::IsFinisherDamage(const FGameplayEffectSpec& Spec)
{
	FGameplayTagContainer AssetTags;
	Spec.GetAllAssetTags(AssetTags);
	for (const FGameplayTag& Tag : AssetTags)
	{
		if (IsFinisherActionTag(Tag))
		{
			return true;
		}
	}

	return false;
}

bool UVBAbilitySystemStatics::IsFinisherActionTag(const FGameplayTag& Tag)
{
	return Tag == VBGameplayTags::Effect_Execute || Tag == VBGameplayTags::Effect_Purify;
}

bool UVBAbilitySystemStatics::IsStaggered(const UAbilitySystemComponent* ASC)
{
	// 태그의 생산자는 UVBGA_Stagger(와 그 파생 UVBGA_Knockdown)의 ActivationOwnedTags 하나다 - 활성화에 붙고 엔진이 EndAbility 에서 뗀다.
	// 히트리액트(State.Combat.HitReacting)는 다른 태그다. 맞은 순간과 기절 게이지가 다 깎인 상태를 여기서 섞지 않는다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Staggered);
}

bool UVBAbilitySystemStatics::IsDowned(const UAbilitySystemComponent* ASC)
{
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Downed);
}

bool UVBAbilitySystemStatics::IsActorInFinisherWindow(const AActor* Actor)
{
	// 형제 IsActorDead 와 같은 경로를 쓴다. 엔진 시그니처가 const AActor* 라 const_cast 도 필요 없다 -
	// 이 클래스가 「판정은 한 벌뿐」이라고 적어 둔 자리에서 획득 경로를 둘로 늘리지 않는다.
	return IsInFinisherWindow(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor));
}

namespace
{
	// 처형 키 설정 오류는 캐릭터(ASC)마다 원인마다 한 번 로그를 남긴다 - 매 틱 묻는 표시가 로그를 덮지 않고, 다른 캐릭터, 다른 판에서는 다시 출력된다.
	enum class EExecuteKeyProblem : uint8 { TwoBound, NoSpec, WrongType, NoConfig };
	bool FirstReportFor(const UAbilitySystemComponent& ASC, EExecuteKeyProblem Problem)
	{
		static TSet<TPair<FObjectKey, EExecuteKeyProblem>> Reported;
		bool bAlreadyReported = false;
		Reported.Add(TPair<FObjectKey, EExecuteKeyProblem>(FObjectKey(&ASC), Problem), &bAlreadyReported);
		return !bAlreadyReported;
	}

	// 대상이 처형을 허용하는가. 적이 아니거나 설정이 없으면 허용한다.
	//
	// 왜 허용 쪽으로 떨어지나: 지금까지의 동작이 그것이고, 설정을 안 만든 적이 조용히 처형 불가가 되면
	//   「왜 이 적만 안 되지」를 추적하게 된다. 막을 수 있는 자리가 아니므로 지금 동작을 유지한다(규칙 27).
	bool AllowsFinisher(const AActor* Candidate)
	{
		const AVBEnemyBase* Enemy = Cast<const AVBEnemyBase>(Candidate);
		if (!Enemy)
		{
			return true;
		}

		// GetEnemyConfig() 는 미할당 시 CDO 를 돌려주므로 null 이 아니다(VBEnemyBase.h:39).
		// 그래도 검사하는 이유: 그 계약이 바뀌면 여기는 조용히 「전부 처형 불가」가 되고,
		// 그 증상은 「왜 처형이 안 되지」로만 보여 추적이 길다. 한 줄로 그 경로를 막아 둔다.
		const UVBEnemyConfig* Config = Enemy->GetEnemyConfig();
		return !Config || Config->bAllowFinisher;
	}
}

bool UVBAbilitySystemStatics::PassesReachRules(const AActor* From, float FacingYaw, const FVector& TargetLocation,
	const AActor* IgnoredActor, const FVBReachRules& Rules, TConstArrayView<TObjectPtr<AActor>> TargetParts)
{
	if (!From)
	{
		return false;
	}

	const FVector Origin = From->GetActorLocation();

	// 높이. GASP 는 NearlyEqual(ErrorTolerance 50) 로 같은 판정을 한다 -
	//   계단이나 턱 위의 적을 끌어내리면 클립이 공중에서 논다.
	if (FMath::Abs(TargetLocation.Z - Origin.Z) > Rules.MaxHeightDifference)
	{
		return false;
	}

	// 야우. 평면 각만 본다 - 높이는 바로 위가 따로 보므로 두 조건이 서로를 가리지 않는다.
	//   우리 원뿔 판정(FindConeTarget)은 3D 내적이라 높이가 각도에 섞여 드는데, 그것과 달리
	//   이 게이트는 락온으로 들어온 대상에도 선다(그쪽은 원뿔을 건너뛴다).
	FVector ToTarget = TargetLocation - Origin;
	ToTarget.Z = 0.0f;
	if (!ToTarget.IsNearlyZero())
	{
		const float DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(FacingYaw, ToTarget.Rotation().Yaw));
		if (DeltaYaw > Rules.MaxYawAngle)
		{
			return false;
		}
	}

	// 시야. 둘 사이를 캡슐로 쓸어 막히면 제외한다 - 벽 너머 처형, 상호작용을 막는다.
	//   World 가 없으면 검사를 건너뛴다. 못 재는 것으로 기능을 없애지는 않는다(규칙 27) -
	//   다만 액터가 월드에 있는 한 이 경로는 안 나온다.
	const UWorld* World = From->GetWorld();
	if (Rules.bRequireLineOfSight && World)
	{
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VBReachLineOfSight), /*bTraceComplex*/ false, From);
		TraceParams.AddIgnoredActor(IgnoredActor);
		for (const TObjectPtr<AActor>& Part : TargetParts)
		{
			if (!ensureMsgf(IsValid(Part.Get()) && Part->GetWorld() == World && Part.Get() != IgnoredActor,
				TEXT("상호작용 TargetParts 에 유효한 같은 월드의 다른 물리 조각이 필요하다")))
			{
				return false;
			}
			TraceParams.AddIgnoredActor(Part.Get());
		}

		FHitResult Blocker;
		const FCollisionShape Probe = FCollisionShape::MakeCapsule(Rules.LineOfSightRadius, Rules.LineOfSightHalfHeight);
		if (World->SweepSingleByChannel(Blocker, Origin, TargetLocation, FQuat::Identity,
			Rules.LineOfSightChannel, Probe, TraceParams))
		{
			return false;
		}
	}

	return true;
}

AActor* UVBAbilitySystemStatics::FindFinisherTarget(const AActor* Attacker, float MaxRange,
	const FVBReachRules& Rules)
{
	// ACharacter 로 받는 이유: 락온 컴포넌트가 캐릭터에만 붙는다. 그 밖의 액터는 처형을 걸지 않는다.
	const ACharacter* AttackerCharacter = Cast<const ACharacter>(Attacker);
	if (!AttackerCharacter)
	{
		return nullptr;
	}

	const UVBTargetLockComponent* LockComp = AttackerCharacter->FindComponentByClass<UVBTargetLockComponent>();
	if (!LockComp)
	{
		// 처형을 걸 수 있는 액터에는 락온 컴포넌트가 있어야 한다. 설정 사고지만 막을 수는 없으므로
		// 말만 한다(규칙 27). 조용히 nullptr 를 돌려주면 「입력이 먹지 않는다」로만 보인다.
		VB_LOG(Warning, "FindFinisherTarget: %s 에 UVBTargetLockComponent 가 없어 처형 대상을 찾을 수 없다",
			*AttackerCharacter->GetName());
		return nullptr;
	}

	const FVector Origin = AttackerCharacter->GetActorLocation();
	const float MaxRangeSq = FMath::Square(MaxRange);
	const float AttackerYaw = AttackerCharacter->GetActorRotation().Yaw;

	// 거리는 두 갈래(락온/원뿔)에 다 쓰이므로 람다 하나에 모은다.
	auto IsEligible = [Origin, MaxRangeSq, AttackerYaw, &Rules,
		AttackerCharacter](const AActor* Candidate) -> bool
	{
		if (!Candidate)
		{
			return false;
		}

		const FVector CandidateLocation = Candidate->GetActorLocation();
		if (FVector::DistSquared(Origin, CandidateLocation) > MaxRangeSq)
		{
			return false;
		}

		// 높이, 야우, 시야는 GASP 의 CheckForInteractableCharacters 를 옮긴 것이다. 짝 애니는 두 캐릭터가 같은 바닥에
		//   마주 서 있다고 전제하므로, 전제가 깨진 채 모션워핑하면 지형을 뚫거나 한쪽만 끌려간다.
		//   값은 UVBMedicalConfig::FinisherTargetRules 가 가지며, 기준 방향은 공격자 캐릭터 방향이다.
		if (!PassesReachRules(AttackerCharacter, AttackerYaw, CandidateLocation, Candidate, Rules))
		{
			return false;
		}

		if (!AllowsFinisher(Candidate))
		{
			return false;
		}
		// 체력을 다시 계산하지 않는다. 창의 판정은 UVBGA_FinisherWindow 하나가 하고 여기는 읽기만 한다.
		return IsActorInFinisherWindow(Candidate);
	};

	// 1순위: 플레이어가 이미 지목한 대상.
	if (AActor* Locked = LockComp->GetLockedTarget())
	{
		if (IsEligible(Locked))
		{
			return Locked;
		}
	}

	// 2순위: 전방 원뿔.
	return LockComp->FindConeTarget(IsEligible);
}

AActor* UVBAbilitySystemStatics::FindExecuteTarget(const UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return nullptr;
	}
	// 읽기만 하고 대상 질의는 순회가 끝난 뒤에 한다 - 순회 중에 능력 목록을 바꿀 코드를 부르지 않는다.
	const UVBGameplayAbility* Bound = nullptr;
	int32 BoundCount = 0;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UVBGameplayAbility* VBAbility = Cast<UVBGameplayAbility>(Spec.Ability);
		if (VBAbility && VBAbility->InputTag.MatchesTagExact(VBGameplayTags::Input_Execute))
		{
			Bound = Bound ? Bound : VBAbility;
			++BoundCount;
		}
	}
	// 둘이 묶이면 키 한 번에 둘 다 켜지고 표시는 첫째만 따른다 - 설정 사고라 한 번 말하고 첫째를 쓴다.
	if (BoundCount > 1 && FirstReportFor(*ASC, EExecuteKeyProblem::TwoBound))
	{
		VB_LOG(Warning, "FindExecuteTarget: %s - Input.Execute 에 능력 %d개가 묶였다, 첫째(%s)의 대상만 고른다",
			*GetNameSafe(ASC->GetAvatarActor()), BoundCount, *GetNameSafe(Bound));
	}
	// 처형 키가 대상을 못 찾는 설정 사고는 눌러도 아무 일 없음으로만 보인다 - 원인마다 한 번 말한다.
	const UVBGA_MedicalAbilityBase* Medical = Cast<UVBGA_MedicalAbilityBase>(Bound);
	if (!Medical || !Medical->HasMedicalConfig())
	{
		const EExecuteKeyProblem Problem = !Bound ? EExecuteKeyProblem::NoSpec
			: (!Medical ? EExecuteKeyProblem::WrongType : EExecuteKeyProblem::NoConfig);
		if (FirstReportFor(*ASC, Problem))
		{
			VB_LOG(Warning, "FindExecuteTarget: %s - %s", *GetNameSafe(ASC->GetAvatarActor()),
				!Bound ? TEXT("Input.Execute 에 묶인 능력이 없다")
				: !Medical ? TEXT("Input.Execute 에 묶인 능력이 의료 행위(UVBGA_MedicalAbilityBase)가 아니다")
				: TEXT("처형 능력의 MedicalConfig 가 비었다"));
		}

		return nullptr;
	}
	// 대상 규칙은 그 능력의 것 하나다 - 능력의 활성화 판정, 실행과 표시가 같은 답을 낸다.
	return Medical->FindMedicalTarget(ASC->AbilityActorInfo.Get());
}

bool UVBAbilitySystemStatics::IsActorFinishing(const AActor* Actor)
{
	const UAbilitySystemComponent* ASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing);
}

bool UVBAbilitySystemStatics::IsActorDead(const AActor* Actor)
{
	// 획득과 판정을 한 줄로 잇는다. 획득만 여기서 하고 판정은 위 함수에 위임 - 판정은 한 벌뿐이어야 한다.
	return IsDead(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor));
}

bool UVBAbilitySystemStatics::IsFriendlyFire(const AActor* Attacker, const AActor* Target)
{
	if (!Attacker || !Target || Attacker == Target)
	{
		// 자기 자신은 트레이스가 이미 제외한다(QueryParams.AddIgnoredActor) - 여기의 답이 아니다.
		return false;
	}

	return FGenericTeamId::GetAttitude(Attacker, Target) == ETeamAttitude::Friendly;
}

const FGameplayAbilitySpec* UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(
		UAbilitySystemComponent* ASC, FGameplayTag InputTag)
{
	if (!ASC || !InputTag.IsValid())
	{
		return nullptr;
	}

	// 순회 중 어빌리티가 부여/제거되면 배열이 재할당되어 이터레이터가 무효가 된다.
	// 잠금을 호출부가 아니라 여기 한 곳에만 거는 것이 이 함수를 공용 홈으로 둔 이유의 절반이다.
	FScopedAbilityListLock ActiveScopeLock(*ASC);
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UVBGameplayAbility* VBAbility = Cast<UVBGameplayAbility>(Spec.Ability);
		if (VBAbility && VBAbility->InputTag.MatchesTagExact(InputTag))
		{
			return &Spec;
		}
	}

	return nullptr;
}
