// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBGA_Interact.h"

#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionMoveToForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h" // GetMesh()->GetAnimInstance() - 공용 PCH 에 기대지 않는다(PCH 없는 -SingleFile 확인에서 C2027)
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "UI/VBUIManagerSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Engine/World.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPushableComponent.h"
#include "Interaction/VBPushableConfig.h"
#include "Interaction/VBToggleProp.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "VB/VB.h"

UVBGA_Interact::UVBGA_Interact()
{
	InputTag = VBGameplayTags::Input_Interact;

	// 이름표. 무엇이 상호작용을 막고 끊는지, 상호작용이 무엇을 막는지는 전투 규칙 표(DA_CombatRules)에 있다.
	//   락온 중에도 켜진다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Interact));

	ActivationOwnedTags.AddTag(VBGameplayTags::State_Interacting);
}

void UVBGA_Interact::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	bHoldInteraction = false;
	bMotionCommitted = false;
	bFastRequested = false;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (!Interactor)
	{
		// 배선 사고다. 막을 수 없으니 말한다 - 조용히 끝나면 「E 를 눌러도 아무 일 없다」로만 보인다.
		VB_LOG(Warning, "%s: %s 에 UVBInteractorComponent 가 없다", *GetName(), *GetNameSafe(Avatar));
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}

		return;
	}

	// 로컬 표시. 원격 클라는 여기까지만 한다 - 판정은 서버 몫이고, 여기서 EndAbility 를 부르면
	//   그것이 서버로 올라가 서버에서 도는 홀드를 끊는다(UVBGA_Execute 의 같은 주석 참조).
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		const UVBInteractableComponent* Focus = Interactor->GetFocusedInteractable();
		bHoldInteraction = Focus && Focus->GetConfig()->Mode == EVBInteractMode::Hold;
		if (bHoldInteraction)
		{
			Interactor->BeginHoldDisplay(Focus->GetConfig()->HoldDuration);
		}
	}

	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	UVBInteractableComponent* Target = Interactor->FindBestInteractable();
	if (!Target)
	{
		// 흔한 경로다(아무것도 없는 곳에서 E). 로그는 Verbose 로 둔다.
		VB_LOG(Verbose, "%s: 닿는 상호작용 대상 없음", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	// 사용 조건은 밀기 예약, 수납, 캐릭터 동작 전에 본다 - 캐릭터가 움직인 뒤 거절하면 "했는데 안 됐다" 로 보인다.
	//   완료 때 NotifyInteracted 가 한 번 더 본다(동작 도중에 바뀐 경우).
	if (!Target->PassesUseConditions(Avatar))
	{
		Refuse(Target, Avatar);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const UVBInteractableConfig* Config = Target->GetConfig();
	bHoldInteraction = Config->Mode == EVBInteractMode::Hold;
	if (Config->Mode == EVBInteractMode::Push)
	{
		BeginPush(Target);
	}
	else if (Config->Mode == EVBInteractMode::Tap)
	{
		BeginTap(Target, Avatar);
	}
	else
	{
		BeginHold(Target);
	}
}

void UVBGA_Interact::BeginPush(UVBInteractableComponent* Target)
{
	UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
	if (!Pushable || !Pushable->Reserve(GetAvatarCharacter()))
	{
		FinishInteraction(true);
		return;
	}

	const UVBInteractableConfig* Config = Target->GetConfig();
	ActiveTarget = Target;
	SheatheIfWanted(Config);
	StartMotion(Target, Config->ActivateMotion);
}

void UVBGA_Interact::BeginTap(UVBInteractableComponent* Target, AActor* Avatar)
{
	const UVBInteractableConfig* Config = Target->GetConfig();
	// 이번에 켜는가 끄는가는 지금 상태의 반대다. 소품도 같은 규칙으로 뒤집는다(AVBToggleProp).
	const bool bActivating = !Target->IsActivated();
	const AActor* Prop = Target->GetOwner();
	const ACharacter* Character = Cast<ACharacter>(Avatar);
	const bool bRunning = Character && UVBLocomotionIntentComponent::IsSprintingFor(Character)
		&& Character->GetCharacterMovement()->IsMovingOnGround();
	// 싸우는 중이면 캐릭터 동작 없이 바로 처리한다. "싸우는 중" 판정은 코디네이터 하나가 관리한다(대화, 빠른 이동도 같은 질의).
	//   서버 전용 질의지만 여기는 이미 권위 쪽이다. 서브시스템이 없으면 평소대로 캐릭터 동작을 한다.
	const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(Avatar);
	const bool bSkipMotion = Config->ShouldSkipMotion(Coordinator && Coordinator->IsEngaged(Avatar));
	if (bSkipMotion)
	{
		VB_LOG(Log, "%s: 싸우는 중 - 몸 동작 없이 바로 한다 대상=%s", *GetName(), *GetNameSafe(Prop));
	}

	if (const FVBInteractionMotion* Motion =
			(!bSkipMotion && Prop) ? Config->SelectMotion(bActivating, Prop->GetActorTransform(), Avatar->GetActorLocation(),
				bRunning, Avatar->GetVelocity()) : nullptr)
	{
		ActiveTarget = Target;
		SheatheIfWanted(Config);
		StartMotion(Target, *Motion);
		return;
	}
	// 방향별 동작을 저작한 대상에서 반대편 모션이 없다고 즉시 토글로 우회하지 않는다.
	if (Config->bUseSidePlane && !bSkipMotion)
	{
		FinishInteraction(true);
		return;
	}

	const bool bCompleted = Target->NotifyInteracted(Avatar);
	if (!bCompleted)
	{
		Refuse(Target, Avatar);
	}

	FinishInteraction(!bCompleted);
}

void UVBGA_Interact::BeginHold(UVBInteractableComponent* Target)
{
	const UVBInteractableConfig* Config = Target->GetConfig();
	ActiveTarget = Target;
	SheatheIfWanted(Config);
	if (Config->bCancelOnMove)
	{
		WatchMoveCancel();
	}

	GetWorld()->GetTimerManager().SetTimer(HoldTimerHandle, this, &UVBGA_Interact::HandleHoldCompleted,
		Config->HoldDuration, false);
	VB_LOG(Log, "%s: 홀드 시작 %s.%s (%.2f초)", *GetName(), *GetNameSafe(Target->GetOwner()), *Target->GetName(),
		Config->HoldDuration);
}

void UVBGA_Interact::FinishInteraction(bool bWasCancelled)
{
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, bWasCancelled);
}
void UVBGA_Interact::SheatheIfWanted(const UVBInteractableConfig* Config)
{
	// 이미 수납돼 있으면(맨손, 트래버설 수납) 손대지 않는다 - 돌려놓는 것도 우리 몫이 아니게 된다.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Config || !Config->bSheatheWeapon || !Avatar)
	{
		return;
	}

	if (UVBWeaponStateComponent* Weapon = Avatar->FindComponentByClass<UVBWeaponStateComponent>();
		Weapon && Weapon->IsWeaponSummoned() && !Weapon->IsTempSheathed())
	{
		Weapon->TempSheatheForParkour();
		bSheathedWeapon = true;
	}
}

void UVBGA_Interact::StartMotion(UVBInteractableComponent* Target, const FVBInteractionMotion& Motion)
{
	ACharacter* Character = GetAvatarCharacter();
	UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (!Character || !Interactor)
	{
		FinishInteraction(true);
		return;
	}

	// 설 자리 = 소품 루트 기준 변환. 팩 애니의 원점은 발바닥이라 캡슐 중심은 반 높이만큼 위다.
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FTransform PropFrame = Target->GetOwner()->GetActorTransform();
	if (Motion.bStandFromApproach)
	{
		// 방향 없는 소품: 기준 = 소품 자리 + 다가온 방향(FVBInteractionMotion::bStandFromApproach 주석). 높이는 플레이어 발이다 -
		//   소품의 원점 높이(메시 중심)는 소품마다 달라 설 자리의 높이로 못 쓴다.
		const FVector PropLocation = PropFrame.GetLocation();
		FVector ToProp = PropLocation - Character->GetActorLocation();
		ToProp.Z = 0.0f;
		const float Yaw = ToProp.IsNearlyZero() ? Character->GetActorRotation().Yaw : ToProp.Rotation().Yaw;
		PropFrame = FTransform(FRotator(0.0f, Yaw, 0.0f),
			FVector(PropLocation.X, PropLocation.Y, Character->GetActorLocation().Z - HalfHeight));
	}

	const UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
	const FTransform Stand = Pushable ? Pushable->GetStandTransform() : Motion.StandTransform * PropFrame;
	const FVector Destination = Stand.GetLocation() + FVector(0.0f, 0.0f, HalfHeight);
	AlignDestination = Destination;

	// 방향은 이동 시작에 한 번 맞춘다. 이동 도중 PhysicsRotation 이 되돌리지 못하게 캡슐 방향 제어를 가져온다.
	const uint8 MotionIndex = Target->GetConfig()->GetMotionIndex(Motion);
	bTargetInitiallyActivated = Target->IsActivated();
	Interactor->BeginMotion(Target, MotionIndex);
	if (!Cast<UVBPushableComponent>(Target))
	{
		if (Target->GetConfig()->bCancelOnMove)
		{
			WatchMoveCancel();
		}
		else
		{
			LockMoveInput();
		}
	}

	VB_LOG(Log, "%s: 대상=%s 동작=%d 몽타주=%s", *GetName(), *GetNameSafe(Target->GetOwner()),
		MotionIndex, *GetNameSafe(Motion.CharacterMontage));
	Character->SetActorRotation(FRotator(0.0f, Stand.Rotator().Yaw, 0.0f), ETeleportType::None);
	PendingMontage = Motion.CharacterMontage;

	const float Duration = Target->GetConfig()->AlignDuration;
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		Character->SetActorLocation(Destination, /*bSweep=*/true);
		HandleAlignFinished();
		return;
	}

	UAbilityTask_ApplyRootMotionMoveToForce* MoveTask = UAbilityTask_ApplyRootMotionMoveToForce::ApplyRootMotionMoveToForce(
		this, NAME_None, Destination, Duration, /*bSetNewMovementMode=*/false, MOVE_Walking,
		/*bRestrictSpeedToExpected=*/false, /*PathOffsetCurve=*/nullptr,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.0f);
	MoveTask->OnTimedOut.AddDynamic(this, &UVBGA_Interact::HandleAlignFinished);
	MoveTask->OnTimedOutAndDestinationReached.AddDynamic(this, &UVBGA_Interact::HandleAlignFinished);
	MoveTask->ReadyForActivation();
	VB_LOG(Log, "%s: 설 자리로 이동 %s (%.0fcm, %.2f초)", *GetName(), *GetNameSafe(Target->GetOwner()),
		FVector::Dist(Character->GetActorLocation(), Destination), Duration);
}

void UVBGA_Interact::HandleAlignFinished()
{
	// 두 신호(시간 초과 / 도착)가 한 틱에 둘 다 올 수 있다. 몽타주를 이미 걸었으면 무시한다.
	UAnimMontage* Montage = PendingMontage;
	PendingMontage = nullptr;
	UVBInteractableComponent* Target = ActiveTarget.Get();
	if (!Montage)
	{
		return;
	}

	ACharacter* Character = GetAvatarCharacter();
	const UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	if (!Target || !Character || !Motion || !Target->IsInteractionEnabled() || !IsValid(Target->GetOwner())
		|| Target->IsActivated() != bTargetInitiallyActivated)
	{
		// 정렬 뒤 몽타주 없이 끝나는 자리 - 다가가기만 하고 끝난 원인을 로그로 가르려고 남긴다.
		VB_LOG(Warning, "%s: 정렬 뒤 취소 대상=%s target=%d char=%d motion=%d enabled=%d owner=%d activated=%d(시작 %d)", *GetName(),
			*GetNameSafe(Target ? Target->GetOwner() : nullptr), Target != nullptr, Character != nullptr, Motion != nullptr,
			Target && Target->IsInteractionEnabled(), Target && IsValid(Target->GetOwner()), Target && Target->IsActivated(),
			bTargetInitiallyActivated);
		FinishInteraction(true);
		return;
	}

	UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
	// 설 자리는 StartMotion 이 정한 그 자리다(AlignDestination 주석).
	const FVector Destination = AlignDestination;
	if (FVector::Dist(Character->GetActorLocation(), Destination) > Target->GetConfig()->AlignTolerance)
	{
		VB_LOG(Warning, "%s: 정렬 실패로 취소 대상=%s 오차=%.1fcm", *GetName(), *GetNameSafe(Target->GetOwner()),
			FVector::Dist(Character->GetActorLocation(), Destination));
		FinishInteraction(true);
		return;
	}

	if (!FMath::IsFinite(Motion->CommitTime) || Motion->CommitTime < 0.0f
		|| Motion->CommitTime >= Montage->GetPlayLength())
	{
		VB_LOG(Warning, "%s: 확정 시각이 몽타주 밖이라 취소 대상=%s CommitTime=%.2f 몽타주=%s(%.2f초)", *GetName(),
			*GetNameSafe(Target->GetOwner()), Motion->CommitTime, *GetNameSafe(Montage), Montage->GetPlayLength());
		FinishInteraction(true);
		return;
	}

	Character->GetCharacterMovement()->StopMovementImmediately();
	if (Pushable && !Pushable->StartPushing(Character))
	{
		FinishInteraction(true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, bFastRequested ? Target->GetConfig()->ResolveFastRate(bTargetInitiallyActivated) : 1.0f,
		Pushable ? FName(TEXT("Grab")) : NAME_None, /*bStopWhenAbilityEnds=*/true);
	MontageTask->OnCompleted.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnCancelled.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->ReadyForActivation();
	// 재생 실패 콜백이 동기적으로 능력을 끝낼 수 있다.
	if (!IsActive())
	{
		return;
	}

	if (Pushable)
	{
		return;
	}

	if (Motion->CommitTime <= KINDA_SMALL_NUMBER)
	{
		CommitMotion();
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(MotionCommitTimerHandle, this, &UVBGA_Interact::CommitMotion,
		Motion->CommitTime / FMath::Max(0.01f, Character->GetMesh()->GetAnimInstance()->Montage_GetPlayRate(Montage)), false);
	}
}

void UVBGA_Interact::CommitMotion()
{
	if (!IsActive() || bMotionCommitted || !GetCurrentActorInfo()->IsNetAuthority())
	{
		return;
	}

	ACharacter* Character = GetAvatarCharacter();
	UVBInteractableComponent* Target = ActiveTarget.Get();
	const UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	UAnimInstance* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Target || !Motion || !Anim || !Anim->Montage_IsPlaying(Motion->CharacterMontage)
		|| !Target->IsInteractionEnabled() || Target->IsActivated() != bTargetInitiallyActivated
		|| !Interactor->CanKeep(Target))
	{
		// 확정 시각의 취소 - 어느 조건이 깨졌는지 남긴다.
		VB_LOG(Warning, "%s: 확정 시각에 취소 대상=%s target=%d motion=%d montagePlaying=%d enabled=%d activated=%d(시작 %d) reach=%d 거리=%.0fcm",
			*GetName(), *GetNameSafe(Target ? Target->GetOwner() : nullptr), Target != nullptr, Motion != nullptr,
			Anim && Motion && Anim->Montage_IsPlaying(Motion->CharacterMontage), Target && Target->IsInteractionEnabled(),
			Target && Target->IsActivated(), bTargetInitiallyActivated, Target && Interactor && Interactor->CanKeep(Target),
			Target && Character ? FVector::Dist(Character->GetActorLocation(), Target->GetComponentLocation()) : -1.0f);
		FinishInteraction(true);
		return;
	}

	const float Remaining = Motion->CommitTime - Anim->Montage_GetPosition(Motion->CharacterMontage);
	if (Remaining > KINDA_SMALL_NUMBER)
	{
		GetWorld()->GetTimerManager().SetTimer(MotionCommitTimerHandle, this, &UVBGA_Interact::CommitMotion,
			FMath::Max(Remaining / FMath::Max(0.01f, Anim->Montage_GetPlayRate(Motion->CharacterMontage)), 0.01f), false);
		return;
	}

	bMotionCommitted = true;
	// 확정됐다 - 이동을 돌려주고(또는 취소 감시를 멈추고), 이동 입력이 오면 남은 몽타주를 섞어 끝낸다(CheckMoveAfterCommit).
	GetWorld()->GetTimerManager().ClearTimer(PreCommitMoveTimerHandle);
	UnlockMoveInput();
	// 닿기 전 취소와 같은 "새로 움직였다" 규칙 - 접촉 순간 밀고 있던 스틱은 한 번 놓아야 센다.
	MoveCancelGate.Reset();
	GetWorld()->GetTimerManager().SetTimer(PostCommitMoveTimerHandle, this, &UVBGA_Interact::CheckMoveAfterCommit,
		FVBMoveCancelGate::Interval, true);
	if (!Target->NotifyInteracted(Character))
	{
		// 동작은 이미 접촉 시점이다. 몽타주는 끝까지 두고(끊으면 손이 튄다) 상태, 효과만 안 낸다.
		Refuse(Target, Character);
		return;
	}

	if (bFastRequested)
	{
		if (AVBToggleProp* Prop = Cast<AVBToggleProp>(Target->GetOwner()))
		{
			Prop->SetInteractionPlayRate(Target->GetConfig()->ResolveFastRate(bTargetInitiallyActivated));
		}
	}
}

void UVBGA_Interact::InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputPressed(Handle, ActorInfo, ActivationInfo);
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr)
	{
		Interactor->RequestRepeatInteraction();
	}
}

void UVBGA_Interact::HandleRepeatPress()
{
	if (!IsActive() || !GetCurrentActorInfo()->IsNetAuthority())
	{
		return;
	}

	UVBInteractableComponent* Target = ActiveTarget.Get();
	if (!Target)
	{
		return;
	}

	if (UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target))
	{
		if (PendingMontage)
		{
			FinishInteraction(true);
		}
		else
		{
			Pushable->RequestRelease(GetAvatarCharacter());
		}

		return;
	}
	// 열기면 FastOpenRate, 닫기면 FastCloseRate(시작 때 상태로 가른다 - 도중에 방향을 뒤집지 않는다). 1 이면 그 방향은 가속 없음.
	//   캐릭터 동작이 있는 탭에만 쓴다 - 홀드는 몽타주가 없어 CommitMotion 이 재생 중 아님으로 능력을 끝낸다.
	const float Rate = Target->GetConfig()->ResolveFastRate(bTargetInitiallyActivated);
	if (bHoldInteraction || bFastRequested || Rate <= 1.0f)
	{
		return;
	}

	AVBToggleProp* Prop = Cast<AVBToggleProp>(Target->GetOwner());
	if (!Prop)
	{
		return;
	}

	bFastRequested = true;
	if (PendingMontage)
	{
		return;
	}

	GetAbilitySystemComponentFromActorInfo()->CurrentMontageSetPlayRate(Rate);
	if (bMotionCommitted)
	{
		Prop->SetInteractionPlayRate(Rate);
	}
	else
	{
		CommitMotion(); // Recalculate the contact timer against the accelerated montage clock.
	}
}

void UVBGA_Interact::HandleMotionMontageEnded()
{
	if (IsActive())
	{
		// 확정 전에 몽타주가 끝났으면(다른 몽타주에 끊김, 섞여 나감) 대상은 그대로다 - 조용히 끝나지 않게 남긴다. 밀기는 몽타주가 잡기 자세라 제외.
		VB_CLOG(!bMotionCommitted && !Cast<UVBPushableComponent>(ActiveTarget.Get()), Warning,
			"%s: 확정 전에 몽타주가 끝났다 - 대상=%s 는 바뀌지 않는다", *GetName(), *GetNameSafe(ActiveTarget.IsValid() ? ActiveTarget->GetOwner() : nullptr));
		FinishInteraction(false);
	}
}

void UVBGA_Interact::LockMoveInput()
{
	const ACharacter* Character = GetAvatarCharacter();
	AController* Controller = Character ? Character->GetController() : nullptr;
	if (Controller && !MoveLockedController.IsValid())
	{
		// 컨트롤러의 막기는 세는 방식이다 - 스마트 오브젝트 등 다른 막기와 겹쳐도 각자 하나씩 풀면 된다.
		Controller->SetIgnoreMoveInput(true);
		MoveLockedController = Controller;
		// 준비 판정, 무기 바퀴가 입력 막힘을 사건으로만 받는다 - 바꾼 쪽이 알린다(UVBUIManagerSubsystem::NotifyInputPolicyChanged).
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Controller))
		{
			UI->NotifyInputPolicyChanged();
		}
	}
}

void UVBGA_Interact::UnlockMoveInput()
{
	AController* Controller = MoveLockedController.Get();
	MoveLockedController.Reset();
	if (Controller)
	{
		Controller->SetIgnoreMoveInput(false);
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Controller))
		{
			UI->NotifyInputPolicyChanged();
		}
	}
}

bool UVBGA_Interact::HasMoveInput(bool bIncludeRawStick) const
{
	const ACharacter* Character = GetAvatarCharacter();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	// 가속 = 이동 입력에서 온 값이다. 설 자리로 옮기는 루트모션은 여기 안 들어온다.
	// 원래 스틱 값은 영웅의 이동 입력이 이동 컴포넌트에 닿지 않는 캐릭터 동작 구간을 메운다.
	const FVector2D RawStick = bIncludeRawStick ? UVBHeroComponent::GetRawMoveInputFor(Character) : FVector2D::ZeroVector;
	return Movement && FVBMoveCancelGate::HasMoveInput(Movement->GetCurrentAcceleration(), RawStick);
}

void UVBGA_Interact::WatchMoveCancel()
{
	MoveCancelGate.Reset();
	GetWorld()->GetTimerManager().SetTimer(PreCommitMoveTimerHandle, this, &UVBGA_Interact::CheckMoveBeforeCommit,
		FVBMoveCancelGate::Interval, true);
}

void UVBGA_Interact::CheckMoveBeforeCommit()
{
	if (!IsActive() || bMotionCommitted)
	{
		GetWorld()->GetTimerManager().ClearTimer(PreCommitMoveTimerHandle);
		return;
	}

	if (MoveCancelGate.Step(HasMoveInput( /*bIncludeRawStick=*/true)))
	{
		// 손이 닿기 전이라 대상은 그대로다. 몽타주는 능력이 끝날 때 멈춘다(bStopWhenAbilityEnds).
		VB_LOG(Log, "%s: 닿기 전에 새로 움직였다 - 취소 대상=%s", *GetName(),
			*GetNameSafe(ActiveTarget.IsValid() ? ActiveTarget->GetOwner() : nullptr));
		FinishInteraction(true);
	}
}

void UVBGA_Interact::CheckMoveAfterCommit()
{
	const ACharacter* Character = GetAvatarCharacter();
	UAnimInstance* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
	const UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	// 이 동작의 몽타주만 끝낸다 - 그 사이 다른 몽타주가 들어왔으면 그것은 건드리지 않는다.
	UAnimMontage* Montage = Motion ? Motion->CharacterMontage.Get() : nullptr;
	if (!IsActive() || !Anim || !Montage || !Anim->Montage_IsPlaying(Montage))
	{
		return;
	}
	// 원래 스틱까지 본다: 동작 중에는 캡슐 방향을 상호작용이 점유해 영웅의 이동 처리가 이동 컴포넌트에 입력을 전달하지 않으므로 가속만 보면 이 판정이
	//   참이 되지 않는다. 닿은 뒤 새로 밀면 남은 동작을 끊는다.
	if (!MoveCancelGate.Step(HasMoveInput( /*bIncludeRawStick=*/true)))
	{
		return;
	}
	// 손은 이미 닿았다 - 남은 자세 때문에 미끄러지며 달리지 않게 섞어 끝낸다. 몽타주 끝 콜백이 능력을 닫고 캡슐 방향 제어를 되돌린다.
	const UVBInteractableConfig* Config = ActiveTarget.IsValid() ? ActiveTarget->GetConfig() : nullptr;
	const float BlendOut = Config ? Config->PostContactBlendOutSeconds : GetDefault<UVBInteractableConfig>()->PostContactBlendOutSeconds;
	VB_LOG(Log, "%s: 닿은 뒤 새로 움직였다 - 남은 몽타주 %s 를 %.2f초에 섞어 끝낸다", *GetName(), *GetNameSafe(Montage), BlendOut);
	GetWorld()->GetTimerManager().ClearTimer(PostCommitMoveTimerHandle);
	Anim->Montage_Stop(BlendOut, Montage);
}

void UVBGA_Interact::HandleHoldCompleted()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	UVBInteractableComponent* Target = ActiveTarget.Get();

	// 누르는 동안 밀려나 벗어났거나 대상이 꺼졌을 수 있다. 그때는 완료가 아니라 취소다. 카메라 방향은 안 본다(CanKeep).
	if (!Interactor || !Interactor->CanKeep(Target))
	{
		VB_LOG(Log, "%s: 홀드 시간은 찼으나 대상에 더는 닿지 않는다 - 취소 (%s)", *GetName(), *GetNameSafe(Target));
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	const bool bCompleted = Target->NotifyInteracted(Avatar);
	if (!bCompleted)
	{
		Refuse(Target, Avatar);
	}

	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, !bCompleted);
}

void UVBGA_Interact::Refuse(UVBInteractableComponent* Target, AActor* Avatar)
{
	if (!Target)
	{
		return;
	}

	Target->ApplyRefused(Avatar);
	if (UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr)
	{
		Interactor->ClientInteractionRefused(Target->GetRefusedText());
	}
}

void UVBGA_Interact::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// 종료 경로가 넷이다 - 탭 완료 / 홀드 완료 / 뗌, 피격 취소 / 대상 없음. 정리는 여기 한 곳에서 한다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimerHandle);
		World->GetTimerManager().ClearTimer(MotionCommitTimerHandle);
		World->GetTimerManager().ClearTimer(PostCommitMoveTimerHandle);
		World->GetTimerManager().ClearTimer(PreCommitMoveTimerHandle);
	}
	// 어느 길로 끝나든(피격 취소, 사망, 대상 없음) 막은 이동을 돌려준다.
	UnlockMoveInput();
	if (UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(ActiveTarget.Get()))
	{
		Pushable->StopPushing(GetAvatarCharacter());
	}

	ActiveTarget.Reset();
	bHoldInteraction = false;

	PendingMontage = nullptr;

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr)
	{
		Interactor->EndHoldDisplay();
		// 캡슐 방향 제어를 되돌린다. 캐릭터 동작이 아니었으면 이미 false 라 무동작이다.
		Interactor->EndMotion();
	}

	if (bSheathedWeapon)
	{
		bSheathedWeapon = false;
		// 죽어서 끝났으면 돌려놓지 않는다 - 시체 손에 칼이 돌아온다.
		if (!UVBAbilitySystemStatics::IsActorDead(Avatar))
		{
			if (UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
			{
				Weapon->RestoreFromTempSheathe();
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
