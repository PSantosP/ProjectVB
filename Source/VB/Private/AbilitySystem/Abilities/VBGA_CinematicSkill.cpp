// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_CinematicSkill.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Data/VBCinematicSkillChargeRules.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBCinematicSkillDefinition.h"
#include "Character/VBTargetable.h"
#include "Character/VBFinisherComponent.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/DataValidation.h"
#include "VB/VB.h"

UVBGA_CinematicSkill::UVBGA_CinematicSkill()
{
	// 저작된 연출은 한 번에 하나 - 무엇이 기다리는지는 전투 규칙 표(DA_CombatRules)의 Ability.Attack.CinematicSkill 행이 정한다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_CinematicSkill);
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_CinematicSkill));
}

bool UVBGA_CinematicSkill::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	const FString Problem = UVBCinematicSkillChargeRules::ValidateCost(GetCostGameplayEffect(), GetAbilityLevel(Handle, ActorInfo));
	if (!Problem.IsEmpty())
	{
		if (!bReportedCostProblem)
		{
			VB_LOG(Error, "%s: %s", *GetName(), *Problem);
		}

		bReportedCostProblem = true;
		return false;
	}

	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const bool bFull = ASC && UVBCinematicSkillChargeRules::IsFull(
		ASC->GetNumericAttribute(UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute()),
		ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute()));
	if (!bFull && OptionalRelevantTags)
	{
		OptionalRelevantTags->AddTag(UAbilitySystemGlobals::Get().ActivateFailCostTag);
	}

	return bFull;
}

bool UVBGA_CinematicSkill::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	if (!Definition)
	{
		VB_LOG(Error, "%s: no Definition assigned - the skill cannot pick a roster (data error, see IsDataValid)", *GetName());
		return false;
	}
	// 조회만 한다. 여기서 거절하면 base 의 commit 전이라 비용, 쿨다운이 들지 않는다.
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UVBWeaponStateComponent* WSC = UVBWeaponStateComponent::FindFor(Avatar);
	if (!WSC || WSC->GetCurrentWeaponType() != Definition->RequiredWeapon)
	{
		return false;
	}

	if (UVBFinisherComponent::IsActorInScene(Avatar))
	{
		return false;
	}

	const UVBSaveParticipantSubsystem* Save = Avatar->GetWorld()
		? Avatar->GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	if (Save && Save->IsApplyingSave())
	{
		return false;
	}

	return CanStageShow(Cast<ACharacter>(Avatar));
}

bool UVBGA_CinematicSkill::CanStageShow(const ACharacter* Avatar) const
{
	const UCharacterMovementComponent* Move = Avatar ? Avatar->GetCharacterMovement() : nullptr;
	if (!Move || !Move->IsMovingOnGround())
	{
		return false;
	}

	FString Why;
	if (!VBCinematicSkillRules::IsReady(AttackConfig, &Why))
	{
		if (!bReportedStagingProblem)
		{
			VB_LOG(Warning, "%s: not ready - %s (refused before commit)", *GetName(), *Why);
		}

		bReportedStagingProblem = true;
		return false;
	}

	return true;
}

void UVBGA_CinematicSkill::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Session = FSession();
	const UAnimMontage* Montage = AttackConfig ? AttackConfig->AttackMontage.Get() : nullptr;
	if (!Montage || !VBCinematicSkillRules::FindFinalWindow(*Montage, Session.FinalWindowStart, Session.FinalWindowEnd))
	{
		VB_LOG(Error, "%s: no montage with a single final window at execute - ends cancelled, no damage", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ActorInfo && ActorInfo->IsNetAuthority() && !BuildRoster())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

bool UVBGA_CinematicSkill::BuildRoster()
{
	AActor* Caster = GetAvatarActorFromActorInfo();
	if (!Caster || !GetWorld() || !Definition)
	{
		VB_LOG(Error, "%s: roster needs a caster (%d), a world (%d) and a definition (%d) - ends cancelled", *GetName(),
			Caster != nullptr, GetWorld() != nullptr, Definition != nullptr);
		return false;
	}

	const FVector Origin = Caster->GetActorLocation();
	const FVector Facing = Caster->GetActorForwardVector().GetSafeNormal2D();
	// 후보는 target lock 과 같은 방식(roster 반경에 겹치고 지금 노릴 수 있는 pawn)으로 찾는다.
	// overlap 순서는 안정적이지 않아 경로 이름 순으로 정렬해 같은 장면이 같은 roster 를 내게 한다.
	TArray<AActor*> Overlaps;
	const TArray<TEnumAsByte<EObjectTypeQuery>> Types{UEngineTypes::ConvertToObjectType(ECC_Pawn)};
	UKismetSystemLibrary::SphereOverlapActors(Caster, Origin, Definition->RosterRadiusCm, Types,
		ACharacter::StaticClass(), {Caster}, Overlaps);
	Overlaps.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });
	TArray<ACharacter*> Actors;
	TArray<FVBCinematicRosterCandidate> Facts;
	for (AActor* Actor : Overlaps)
	{
		ACharacter* Enemy = Cast<ACharacter>(Actor);
		if (!Enemy || !IVBTargetable::IsActorTargetable(Actor))
		{
			continue;
		}
		// 노릴 수 있으면 인터페이스가 있다 - 정체 태그만 읽는다.
		const IVBTargetable* Targetable = CastChecked<IVBTargetable>(Actor);
		const FVector To = (Enemy->GetActorLocation() - Origin).GetSafeNormal2D();
		FVBCinematicRosterCandidate& C = Facts.AddDefaulted_GetRef();
		Actors.Add(Enemy);
		C.DistanceCm = FVector::Dist2D(Enemy->GetActorLocation(), Origin);
		C.AngleFromFacingDeg = To.IsNearlyZero() ? 0.0f
			: FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Facing, To), -1.0, 1.0)));
		C.bAlive = !UVBAbilitySystemStatics::IsActorDead(Enemy);
		C.bGrounded = Enemy->GetCharacterMovement() && Enemy->GetCharacterMovement()->IsMovingOnGround();
		C.bExcludedByTag = Targetable->GetTargetIdentityTags().HasAny(Definition->ExcludedTargetTags);
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy);
		const USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const UAnimMontage* Playing = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		const bool bOwnAttack = ASC && UVBAbilitySystemStatics::IsAttacking(ASC);
		C.bBodyBusy = (ASC && (ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting)
				|| UVBAbilitySystemStatics::IsStaggered(ASC)
				|| UVBAbilitySystemStatics::IsDowned(ASC)
				|| ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing)))
			|| (Mesh && Mesh->IsSimulatingPhysics())
			|| (Playing && Playing->HasRootMotion() && !bOwnAttack);
	}

	for (const int32 Index : VBCinematicSkillRules::SelectRoster(Facts, Definition->RosterRadiusCm, Definition->RosterHalfAngleDeg,
		Definition->MaxTargets))
	{
		Session.Roster.Add(Actors[Index]);
	}

	VB_LOG(Log, "%s: roster %d of %d targetable pawns (radius %.0f cm, half angle %.0f, max %d)", *GetName(), Session.Roster.Num(),
		Facts.Num(), Definition->RosterRadiusCm, Definition->RosterHalfAngleDeg, Definition->MaxTargets);
	return true;
}

bool UVBGA_CinematicSkill::IsInFinalWindow() const
{
	const UAnimMontage* Montage = GetActiveMontage();
	const ACharacter* Avatar = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	const UAnimInstance* Anim = Avatar && Avatar->GetMesh() ? Avatar->GetMesh()->GetAnimInstance() : nullptr;
	if (!Montage || !Anim || !Anim->Montage_IsPlaying(Montage) || Session.FinalWindowEnd <= Session.FinalWindowStart)
	{
		return false;
	}

	return VBCinematicSkillRules::IsFinalPhase(Anim->Montage_GetPosition(Montage), Session.FinalWindowStart);
}

void UVBGA_CinematicSkill::FilterWindowHits(int32 WindowKey, TArray<FHitResult>& InOutHits)
{
	// 여기서 버린 타격은 이 창에서 소모된다 - trace 컴포넌트가 이미 대상을 셌다(RehitInterval <= 0 이면 다시 안 알린다).
	// 그래서 roster, 단계, 소모 규칙만 타격을 버릴 수 있다.
	TArray<const AActor*> HitActors;
	for (const FHitResult& Hit : InOutHits)
	{
		HitActors.Add(Hit.GetActor());
	}

	const bool bFinal = IsInFinalWindow();
	const TArray<int32> Allowed = VBCinematicSkillRules::AllowFinalHits(HitActors, bFinal, Session.Roster, Session.Consumed);
	if (Allowed.Num() != InOutHits.Num())
	{
		VB_LOG(Log, "%s: window %d - %d of %d hits allowed (final window %s)", *GetName(), WindowKey, Allowed.Num(), InOutHits.Num(),
			bFinal ? TEXT("open") : TEXT("NOT open"));
	}

	TArray<FHitResult> Kept;
	for (const int32 Index : Allowed)
	{
		Kept.Add(InOutHits[Index]);
	}

	InOutHits = MoveTemp(Kept);
}

#if WITH_EDITOR
EDataValidationResult UVBGA_CinematicSkill::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const FString CostProblem = UVBCinematicSkillChargeRules::ValidateCost(GetCostGameplayEffect());
	if (!CostProblem.IsEmpty())
	{
		Context.AddError(FText::FromString(CostProblem));
		Result = EDataValidationResult::Invalid;
	}

	if (!Definition)
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: Definition is not set - the skill refuses every activation."),
			*GetName())));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

void UVBGA_CinematicSkill::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 모든 끝 경로(정상, 취소, 중단, 죽음)가 여기로 모인다.
	Session = FSession();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

#if !UE_BUILD_SHIPPING
// 입력 행이 없는 동안의 개발용 입구: 로컬 플레이어 ASC 에 스킬 클래스를 없으면 주고 ASC 로 실제 활성화한다(Montage 재생만이 아니다).
// play bot 은 ExecCmds=vb.Combat.CastCinematicSkill <class path> 로 부른다.
static FAutoConsoleCommandWithWorldAndArgs GVBCastCinematicSkill(
	TEXT("vb.Combat.CastCinematicSkill"),
	TEXT("Dev: grant (if missing) and activate a cinematic skill class on the local player. Arg: class path of a UVBGA_CinematicSkill child."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		UAbilitySystemComponent* ASC = PC ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC->GetPawn()) : nullptr;
		UClass* Class = Args.Num() > 0 ? FSoftClassPath(Args[0]).TryLoadClass<UVBGA_CinematicSkill>() : nullptr;
		if (!ASC || !Class || !ASC->IsOwnerActorAuthoritative())
		{
			VB_LOG(Warning, "vb.Combat.CastCinematicSkill: needs an authoritative local player ASC and a UVBGA_CinematicSkill class path");
			return;
		}

		FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(Class);
		const FGameplayAbilitySpecHandle SpecHandle = Spec ? Spec->Handle : ASC->GiveAbility(FGameplayAbilitySpec(Class, 1,
			INDEX_NONE, PC));
		const bool bActivated = ASC->TryActivateAbility(SpecHandle);
		VB_LOG(Log, "vb.Combat.CastCinematicSkill %s -> %s", *Class->GetName(), bActivated ? TEXT("activated") : TEXT("refused"));
	}));
#endif
