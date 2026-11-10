#include "Character/VBSlideMovementComponent.h"

#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "Curves/CurveFloat.h"
#include "Data/VBMovementConfig.h"
#include "VB/VB.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

class FSavedMove_VBSlide final : public FSavedMove_Character
{
public:
	bool bSavedWantsToSlide = false;
	bool bSavedSlideActive = false;
	bool bSavedCrouchAfterSlide = false;
	float SavedSlideElapsed = 0.f;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		bSavedWantsToSlide = bSavedSlideActive = false;
		bSavedCrouchAfterSlide = false;
		SavedSlideElapsed = 0.f;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		return FSavedMove_Character::GetCompressedFlags() | (bSavedWantsToSlide ? FLAG_Custom_0 : 0)
			| (bSavedCrouchAfterSlide ? FLAG_Custom_1 : 0);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAcceleration,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAcceleration, ClientData);
		if (const auto* Movement = Cast<UVBSlideMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedWantsToSlide = Movement->bWantsToSlide;
			bSavedSlideActive = Movement->bSlideActive;
			bSavedCrouchAfterSlide = Movement->bCrouchAfterSlide;
			SavedSlideElapsed = Movement->SlideElapsed;
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		if (auto* Movement = Cast<UVBSlideMovementComponent>(Character->GetCharacterMovement()))
		{
			Movement->bWantsToSlide = bSavedWantsToSlide;
			Movement->bSlideActive = bSavedSlideActive;
			Movement->bCrouchAfterSlide = bSavedCrouchAfterSlide;
			Movement->SlideElapsed = SavedSlideElapsed;
			if (AVBCharacter* Player = Cast<AVBCharacter>(Character))
			{
				Player->OnSlideMovementStateChanged(bSavedSlideActive);
			}
		}
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const auto* Other = static_cast<const FSavedMove_VBSlide*>(NewMove.Get());
		return !bSavedSlideActive && !Other->bSavedSlideActive &&
			bSavedWantsToSlide == Other->bSavedWantsToSlide &&
			bSavedCrouchAfterSlide == Other->bSavedCrouchAfterSlide &&
			FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}
};

class FNetworkPredictionData_Client_VBSlide final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_VBSlide(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return MakeShared<FSavedMove_VBSlide>(); }
};

void UVBSlideMovementComponent::RequestSlide()
{
	if (CanRequestSlide())
	{
		bWantsToSlide = true;
		bCrouchAfterSlide = false;
	}
}

bool UVBSlideMovementComponent::CanRequestSlide() const
{
	const AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner);
	const UVBAbilitySystemComponent* ASC = Player ? Cast<UVBAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
	const UAnimInstance* Anim = Player && Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr;
	// 끝 속도 >= 시작 속도면 시작하지 않는다(설정 오류 - 시작하자마자 끝나 포즈만 깜빡인다).
	return !bSlideActive && SlideEndSpeed < SlideStartSpeed && IsMovingOnGround() && Velocity.Size2D() > SlideStartSpeed && Player
		&& (!ASC || !ASC->IsBlockedByHeldState(VBGameplayTags::Ability_Locomotion_Crouch))
		&& (!Anim || !Anim->IsAnyMontagePlaying());
}

void UVBSlideMovementComponent::SetSlideActive(bool bNewActive)
{
	if (bSlideActive == bNewActive)
	{
		return;
	}

	bSlideActive = bNewActive;
	if (bNewActive)
	{
		SlideElapsed = 0.f;
	}

	if (AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner))
	{
		Player->OnSlideMovementStateChanged(bNewActive);
	}

	if (!bNewActive && CharacterOwner)
	{
		if (bCrouchAfterSlide)
		{
			CharacterOwner->Crouch();
		}
		else
		{
			CharacterOwner->UnCrouch();
		}
	}
}

void UVBSlideMovementComponent::CancelSlide(bool bRemainCrouched)
{
	bWantsToSlide = false;
	bCrouchAfterSlide = bRemainCrouched;
	SetSlideActive(false);
}

void UVBSlideMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSlide = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bCrouchAfterSlide = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UVBSlideMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UVBSlideMovementComponent* MutableThis = const_cast<UVBSlideMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_VBSlide(*this);
	}

	return ClientPredictionData;
}

float UVBSlideMovementComponent::GetMaxSpeed() const
{
	return bSlideActive && IsMovingOnGround()
		? FMath::Min(SlideMaxSpeed, SlideSettings.GetTargetSpeed(GetSlideSlopeAngle(), SlideElapsed))
		: Super::GetMaxSpeed();
}

float UVBSlideMovementComponent::GetSlideSlopeAngle() const
{
	const FVector Direction = Acceleration.GetSafeNormal2D();
	if (!CurrentFloor.IsWalkableFloor() || Direction.IsNearlyZero())
	{
		return 0.f;
	}

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CurrentFloor.HitResult.Normal, Direction), -1.f, 1.f))) - 90.f;
}

void UVBSlideMovementComponent::PhysicsRotation(float DeltaTime)
{
	// Owner Tick은 CMC 뒤다. 예측/서버 submove의 현재 가속으로 회전율을 정한 뒤 엔진 회전을 실행한다.
	if (AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner))
	{
		Player->UpdateRotationRateForMovementMode();
	}

	Super::PhysicsRotation(DeltaTime);
}

void UVBSlideMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); // Process predicted crouch first.
	// 서버의 원격 이동은 서버 이동 틱보다 먼저 네트워크 처리에서 돈다 - 같은 패킷의 상태 변화가 이 이동에 바로 반영되게.
	RefreshRotationMode();
	const AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner);
	const UVBAbilitySystemComponent* ASC = Player ? Cast<UVBAbilitySystemComponent>(Player->GetAbilitySystemComponent()) : nullptr;
	if (bSlideActive && (!IsMovingOnGround() || !IsCrouching() || !bWantsToSlide
		|| (ASC && ASC->IsBlockedByHeldState(VBGameplayTags::Ability_Locomotion_Crouch))))
	{
		CancelSlide(IsMovingOnGround() && !bWantsToSlide && bCrouchAfterSlide);
	}

	if (bWantsToSlide && !bSlideActive)
	{
		if (CanRequestSlide() && IsCrouching())
		{
			SetSlideActive(true);
		}
		else if (!CanRequestSlide())
		{
			bWantsToSlide = false;
		}
	}
}

void UVBSlideMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!bSlideActive || !IsMovingOnGround())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	const FVector SavedAcceleration = Acceleration;
	const float SlopeAngle = GetSlideSlopeAngle();
	const float SlideAcceleration = SlideElapsed < SlideSettings.InitialBoostTime
		? SlideSettings.InitialBoostAcceleration : SlideSettings.AfterBoostAcceleration;
	Acceleration = SavedAcceleration.GetSafeNormal2D() * SlideAcceleration;
	Super::CalcVelocity(DeltaTime, 0.f, bFluid, SlideSettings.GetDeceleration(SlopeAngle));
	Acceleration = SavedAcceleration;
	Velocity.Z = 0.f; // PhysWalking projects horizontal velocity onto the floor ramp itself.
}

void UVBSlideMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	if (!bSlideActive)
	{
		return;
	}

	SlideElapsed += DeltaSeconds;
	if (!IsMovingOnGround() || (SlideMaxDuration > 0.f && SlideElapsed >= SlideMaxDuration) ||
		Velocity.Size2D() <= SlideEndSpeed)
	{
		CancelSlide(bCrouchAfterSlide);
	}
}

void UVBSlideMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	UpdateWalkSpeeds();
	RefreshRotationMode();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 이동 뒤에 둔다 - 풀기 판정이 이번 프레임의 가속, 바닥을 읽는다(이동 컴포넌트는 캐릭터보다 먼저 틱한다).
	TickResidualMotion();
	TickRunHandover();
}

void UVBSlideMovementComponent::ScheduleRunHandover(const UAnimMontage* FromMontage, UAnimMontage* ToRunMontage, float ExitTime,
	const FVector& DodgeDirection, float MaxAngleDeg)
{
	PendingRunHandover = FRunHandover();
	if (FromMontage && ToRunMontage)
	{
		PendingRunHandover = {FromMontage, ToRunMontage, ExitTime, DodgeDirection, MaxAngleDeg};
	}
}

bool UVBSlideMovementComponent::ShouldHandOverToRun(const FVector& Input, const FVector& DodgeDirection, float MaxAngleDeg)
{
	const FVector In2D = FVector(Input.X, Input.Y, 0.0f).GetSafeNormal();
	const FVector Dir2D = FVector(DodgeDirection.X, DodgeDirection.Y, 0.0f).GetSafeNormal();
	return !In2D.IsNearlyZero() && !Dir2D.IsNearlyZero() && FVector::DotProduct(In2D,
		Dir2D) >= FMath::Cos(FMath::DegreesToRadians(MaxAngleDeg));
}

void UVBSlideMovementComponent::TickRunHandover()
{
	const UAnimMontage* From = PendingRunHandover.From.Get();
	UAnimInstance* Anim = CharacterOwner && CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	if (!From || !Anim)
	{
		return;
	}
	// 회피가 끊겼으면(피격, 다른 몽타주) 달리기로 전환하지 않는다.
	if (!Anim->Montage_IsPlaying(From))
	{
		PendingRunHandover = FRunHandover();
		return;
	}

	if (Anim->Montage_GetPosition(From) < PendingRunHandover.ExitTime)
	{
		return;
	}

	UAnimMontage* To = PendingRunHandover.To.Get();
	const bool bHandOver = To && ShouldHandOverToRun(GetCurrentAcceleration(), PendingRunHandover.Direction,
		PendingRunHandover.MaxAngleDeg);
	PendingRunHandover = FRunHandover();
	if (bHandOver)
	{
		Anim->Montage_Play(To);
	}
}

void UVBSlideMovementComponent::StopResidualAttackMotion(float RestoreAfterSeconds, const UAnimMontage* SourceMontage)
{
	if (!CharacterOwner || !IsMovingOnGround() || HasRootMotionSources() || GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FAnimMontageInstance* RootMotionOwner = CharacterOwner->GetRootMotionAnimMontageInstance();
	if (RootMotionOwner && RootMotionOwner->Montage != SourceMontage)
	{
		return;
	}

	const FAnimMontageInstance* RecoveryInstance = RootMotionOwner;
	const UAnimInstance* Anim = CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	if (!RecoveryInstance && Anim)
	{
		for (int32 Index = Anim->MontageInstances.Num() - 1; Index >= 0; --Index)
		{
			const FAnimMontageInstance* Candidate = Anim->MontageInstances[Index];
			if (Candidate && Candidate->Montage == SourceMontage && Candidate->GetWeight() > ZERO_ANIMWEIGHT_THRESH)
			{
				RecoveryInstance = Candidate;
				break;
			}
		}
	}

	// 노티 처리 중에 불려 같은 프레임 루트모션이 속도를 도로 채우므로, 마감 시각까지 매 틱 누른다.
	StopMovementImmediately();
	// 이미 끝난 몽타주는 속도만 버린다 - 회피, 피격, 새 공격까지 누르지 않는다.
	if (!RecoveryInstance || !GetWorld())
	{
		return;
	}

	ResidualMotionMontageInstanceId = RecoveryInstance->GetInstanceID();
	ResidualMotionSuppressUntil = GetWorld()->GetTimeSeconds() + FMath::Max(0.01f,
		RestoreAfterSeconds + RecoveryInstance->GetBlend().GetBlendTimeRemaining());
	CharacterOwner->SetAnimRootMotionTranslationScale(0.0f);
}

void UVBSlideMovementComponent::TickResidualMotion()
{
	if (ResidualMotionSuppressUntil <= 0.0)
	{
		return;
	}

	const FAnimMontageInstance* RootMotionOwner = CharacterOwner ? CharacterOwner->GetRootMotionAnimMontageInstance() : nullptr;
	UAnimInstance* Anim = CharacterOwner && CharacterOwner->GetMesh() ? CharacterOwner->GetMesh()->GetAnimInstance() : nullptr;
	const FAnimMontageInstance* RecoveryInstance = Anim ? Anim->GetMontageInstanceForID(ResidualMotionMontageInstanceId) : nullptr;
	const bool bOwnerChanged = RootMotionOwner && RootMotionOwner->GetInstanceID() != ResidualMotionMontageInstanceId;
	const bool bDone = !CharacterOwner || !GetWorld() || !RecoveryInstance || bOwnerChanged || !IsMovingOnGround() || HasRootMotionSources()
		|| GetWorld()->GetTimeSeconds() >= ResidualMotionSuppressUntil
		|| GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER;
	if (bDone)
	{
		ResidualMotionSuppressUntil = 0.0;
		ResidualMotionMontageInstanceId = INDEX_NONE;
		// 새 몽타주의 Task 가 건 배율은 이전 공격이 정리할 것이 아니다.
		if (CharacterOwner && !bOwnerChanged && CharacterOwner->GetAnimRootMotionTranslationScale() == 0.0f)
		{
			CharacterOwner->SetAnimRootMotionTranslationScale(1.0f);
		}

		return;
	}

	CharacterOwner->SetAnimRootMotionTranslationScale(0.0f);
	Velocity.X = 0.0f;
	Velocity.Y = 0.0f;
}

void UVBSlideMovementComponent::SetMovementConfig(const UVBMovementConfig* InConfig)
{
	MovementConfig = InConfig;
}

bool UVBSlideMovementComponent::ShouldClampGuardToWalk(bool bGuardingTag, const FVBGuardSet* HeldGuard)
{
	return bGuardingTag && HeldGuard && HeldGuard->bGuardLocomotionCoversMovement;
}

FVector UVBSlideMovementComponent::PickGaitSpeeds(const FVBMovementProfile& Profile, bool bGuarding, bool bSprinting,
	bool bSprintRampDone, EVBGait Gait)
{
	if (bGuarding)
	{
		return Profile.WalkSpeeds;
	}

	if (bSprinting)
	{
		// 2단 램프: 누른 지 SprintRampDuration 전에는 1단(SprintRunSpeeds), 뒤에는 2단(SprintSpeeds). 램프가 없는 프로필은 1단 값으로 고정.
		return Profile.bSprintRampEnabled && bSprintRampDone ? Profile.SprintSpeeds : Profile.SprintRunSpeeds;
	}

	return Gait == EVBGait::Walk ? Profile.WalkSpeeds : Profile.RunSpeeds;
}

void UVBSlideMovementComponent::UpdateWalkSpeeds()
{
	if (!CharacterOwner)
	{
		return;
	}

	if (!LocomotionIntent.IsValid())
	{
		LocomotionIntent = UVBLocomotionIntentComponent::FindFor(CharacterOwner);
	}

	if (!WeaponState.IsValid())
	{
		WeaponState = UVBWeaponStateComponent::FindFor(CharacterOwner);
	}

	const UVBLocomotionIntentComponent* Intent = LocomotionIntent.Get();
	if (!Intent)
	{
		return;
	}

	const UVBMovementConfig* Cfg = MovementConfig ? MovementConfig.Get() : GetDefault<UVBMovementConfig>();

	// 이동 방향(0~180도, 캐릭터 앞 기준) -> 곡선으로 구역 0(앞), 1(옆), 2(뒤). 멈춰 있으면 앞.
	float DirectionDeg = 0.f;
	const FVector Vel2D(Velocity.X, Velocity.Y, 0.f);
	if (!Vel2D.IsNearlyZero())
	{
		const float Dot = FVector::DotProduct(CharacterOwner->GetActorForwardVector(), Vel2D.GetSafeNormal());
		DirectionDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
	}

	if (!Cfg->StrafeSpeedMapCurve && !bWarnedNoSpeedCurve)
	{
		bWarnedNoSpeedCurve = true;
		VB_LOG(Warning, "%s: 이동 설정 %s 에 StrafeSpeedMapCurve 가 없다 - 방향별 걷기 속도 구분 없이 늘 앞쪽 속도를 쓴다",
			*GetNameSafe(CharacterOwner), *GetNameSafe(Cfg));
	}

	const float Zone = Cfg->StrafeSpeedMapCurve ? Cfg->StrafeSpeedMapCurve->GetFloatValue(DirectionDeg) : 0.f;

	// 속도 프로필은 MM 풀이 정한다 - 풀을 정하는 값이 속도도 정하면 보폭과 클립의 출처가 갈릴 수 없다. 풀 None = 비무장.
	const EVBWeaponType MMPool = WeaponState.IsValid() ? WeaponState->GetMMPool() : EVBWeaponType::None;
	const FVBMovementProfile& P = Cfg->ResolveProfile(MMPool, MMPool != EVBWeaponType::None);
	const bool bSprinting = Intent->GetIsSprinting();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (bSprinting && !bWasSprinting)
	{
		SprintHoldStartTime = Now;
	}

	bWasSprinting = bSprinting;

	const ACharacter* Body = CharacterOwner;
	const UAbilitySystemComponent* OwnerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Body);
	// 가드 걷기를 가진 무기(가드 세트가 걷기까지 덮는)만 가드 중 걷기 속도로 묶는다 - 나머지는 종전처럼 가드한 채 뛴다.
	const UVBWeaponDefinition* Held = WeaponState.IsValid() ? WeaponState->GetSelectedDefinition() : nullptr;
	const bool bGuarding = ShouldClampGuardToWalk(OwnerASC && OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Guarding),
		Held ? &Held->GetGuardSet() : nullptr);
	const FVector GaitSpeeds = PickGaitSpeeds(P, bGuarding, bSprinting, Now - SprintHoldStartTime >= Cfg->SprintRampDuration,
		Intent->GetGait());
	float NewMaxSpeed = Zone < 1.f ? FMath::Lerp(GaitSpeeds.X, GaitSpeeds.Y, Zone) : FMath::Lerp(GaitSpeeds.Y, GaitSpeeds.Z, Zone - 1.f);
	// 무장 조깅의 45/135도는 따로 저작된 클립이다 - 축 곡선만 쓰면 대각선 클립의 루트 속도보다 빨라 발이 밀린다. 대각선 정점에서만 그 값으로 모은다.
	if (MMPool != EVBWeaponType::None && !bSprinting && Intent->GetGait() == EVBGait::Run && P.RunDiagonalSpeed > 0.f)
	{
		const float NearestDiagonal = DirectionDeg <= 90.f ? 45.f : 135.f;
		const float DiagonalWeight = 1.f - FMath::Clamp(FMath::Abs(DirectionDeg - NearestDiagonal) / 45.f, 0.f, 1.f);
		NewMaxSpeed = FMath::Lerp(NewMaxSpeed, P.RunDiagonalSpeed, DiagonalWeight);
	}

	MaxWalkSpeed = NewMaxSpeed;
	MaxWalkSpeedCrouched = P.CrouchSpeed;
}

void UVBSlideMovementComponent::RefreshRotationMode()
{
	const AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner);
	if (!Player)
	{
		return;
	}

	if (!LocomotionIntent.IsValid())
	{
		LocomotionIntent = UVBLocomotionIntentComponent::FindFor(Player);
	}

	if (!WeaponState.IsValid())
	{
		WeaponState = UVBWeaponStateComponent::FindFor(Player);
	}

	if (!TargetLock.IsValid())
	{
		TargetLock = UVBTargetLockComponent::FindFor(Player);
	}

	const UVBLocomotionIntentComponent* Intent = LocomotionIntent.Get();
	if (!Intent)
	{
		// 모든 AVBCharacter 가 생성자에서 의도 컴포넌트를 만든다 - 없으면 회전 방식이 지난 값으로 남는다.
		if (!bWarnedNoIntent)
		{
			bWarnedNoIntent = true;
			VB_LOG(Warning, "%s: 이동 의도 컴포넌트가 없다 - 회전 방식을 정하지 못해 지난 값이 남는다", *GetNameSafe(Player));
		}

		return;
	}

	VBBodyControlPolicy::FRotationModeInputs Inputs;
	Inputs.bSprinting = Intent->GetIsSprinting();
	Inputs.bWantsToStrafe = Intent->WantsToStrafe();
	Inputs.bWantsToAim = Intent->WantsToAim();
	Inputs.bLockedOn = TargetLock.IsValid() && TargetLock->IsLockedOn();
	Inputs.bInCombat = WeaponState.IsValid() && WeaponState->IsInCombat();
	Inputs.bCapsuleRotationOwned = Player->IsCapsuleRotationOwnedByFeature();
	const VBBodyControlPolicy::FRotationMode Mode = VBBodyControlPolicy::ResolveRotationMode(Inputs);
	bOrientRotationToMovement = Mode.bOrientRotationToMovement;
	bUseControllerDesiredRotation = Mode.bUseControllerDesiredRotation;
	bAllowPhysicsRotationDuringAnimRootMotion = Mode.bAllowPhysicsRotationDuringAnimRootMotion;
}

void UVBSlideMovementComponent::StopMovementTick()
{
	RefreshRotationMode();
	SetComponentTickEnabled(false);
}
