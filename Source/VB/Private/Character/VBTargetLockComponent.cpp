// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Character/VBTargetLockComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBTargetable.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Input/VBInputDeviceManager.h"
#include "Data/VBTargetLockConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/InputSettings.h"   // bEnableLegacyInputScales - LookInputToControlDegrees
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

namespace
{
	// 플레이어가 푼 해제(락 키, 뒤로 질주). 명중 재락 억제와 서버 해제 허용이 둘에 같다.
	bool IsPlayerRelease(EVBLockReason Reason)
	{
		return Reason == EVBLockReason::Manual || Reason == EVBLockReason::RetreatSprint;
	}
}

UVBTargetLockComponent::UVBTargetLockComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	SetIsReplicatedByDefault(true);
}

UVBTargetLockComponent* UVBTargetLockComponent::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBTargetLockComponent>() : nullptr;
}

const UVBTargetLockConfig* UVBTargetLockComponent::GetTargetLockConfig() const
{
	return LockConfig ? LockConfig : GetDefault<UVBTargetLockConfig>();
}

void UVBTargetLockComponent::RefreshOwner()
{
	OwnerCharacter = Cast<AVBCharacter>(GetOwner());
	if (!OwnerWeaponState.IsValid())
	{
		OwnerWeaponState = UVBWeaponStateComponent::FindFor(GetOwner());
	}

	APlayerController* Controller = OwnerCharacter.IsValid() ? OwnerCharacter->GetController<APlayerController>() : nullptr;
	if (OwnerController.Get() != Controller)
	{
		OwnerController = Controller;
		CancelHitRecenter();
		HitRecenterWindow = {};
		LastReceivedHitTime = -DBL_MAX;
		ResetCameraInput();
		ResetSwitchInput();
		NextRequestId = PendingRequestId = LastServerRequestId = 0;
		bSuppressCameraUntilResponse = false;
		bDeathReacquirePending = false;
		SuppressHitUntil = -1.0;
		if (GetOwner()->HasAuthority() && bServerLockActive)
		{
			SetLockOn(nullptr, EVBLockReason::OwnerChanged);
		}
	}
}

bool UVBTargetLockComponent::CanOwnerLock() const
{
	return OwnerCharacter.IsValid() && OwnerController.IsValid() && !UVBAbilitySystemStatics::IsActorDead(OwnerCharacter.Get());
}

AActor* UVBTargetLockComponent::GetLockedTarget() const { return ConfirmedState.Target.Get(); }
EVBLockReason UVBTargetLockComponent::GetLockReason() const { return ConfirmedState.Reason; }
bool UVBTargetLockComponent::IsLockedOn() const { return IsValid(GetLockedTarget()); }

void UVBTargetLockComponent::ToggleLockOn()
{
	RefreshOwner();
	CancelHitRecenter(true); // 수동 락 버튼도 진행 중인 명중 보정보다 우선한다.
	if (!CanOwnerLock())
	{
		return;
	}

	if (IsLockedOn() || PendingRequestId != 0)
	{
		RequestLock(nullptr, EVBLockReason::Manual);
	}
	else if (AActor* Target = FindBestTarget())
	{
		RequestLock(Target, EVBLockReason::Manual);
	}
}

void UVBTargetLockComponent::SwitchTarget(float AxisValue)
{
	RefreshOwner();
	if (!IsLockedOn() || PendingRequestId || !FMath::IsFinite(AxisValue) || FMath::IsNearlyZero(AxisValue))
	{
		return;
	}

	if (AActor* Target = FindSideTarget(AxisValue))
	{
		RequestLock(Target, EVBLockReason::Switch);
	}
}

void UVBTargetLockComponent::HandleSwitchInput(float AxisValue, bool bGamepad)
{
	if (bGamepad)
	{
		const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
		const int32 Direction = SwitchLatch.Update(AxisValue, Cfg->StickSwitchPressThreshold, Cfg->StickSwitchReleaseThreshold);
		if (Direction != 0 && CanTrackCamera())
		{
			SwitchTarget(float(Direction));
		}
	}
	else if (CanTrackCamera())
	{
		SwitchTarget(AxisValue);
	}
}

void UVBTargetLockComponent::ResetSwitchInput() { SwitchLatch.Reset(); }

void UVBTargetLockComponent::OnInputDeviceChanged(ECommonInputType /*NewDeviceType*/)
{
	CancelHitRecenter(true);
	ResetSwitchInput();
	ResetCameraInput();
}

void UVBTargetLockComponent::RequestLock(AActor* Target, EVBLockReason Reason)
{
	if (!OwnerCharacter.IsValid() || !OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	PendingRequestId = ++NextRequestId;
	if (!Target)
	{
		bSuppressCameraUntilResponse = true;
		bDeathReacquirePending = false;
		if (IsPlayerRelease(Reason))
		{
			SuppressHitUntil = GetWorld()->GetTimeSeconds() + GetTargetLockConfig()->ManualUnlockHitDelay;
		}
	}

	SetComponentTickEnabled(true);
	ServerRequestLock(Target, Reason, PendingRequestId, ConfirmedState.Revision);
}

void UVBTargetLockComponent::ServerRequestLock_Implementation(AActor* Target, EVBLockReason Reason, uint32 RequestId,
	uint32 ExpectedRevision)
{
	RefreshOwner();
	if (RequestId > LastServerRequestId)
	{
		LastServerRequestId = RequestId;
		if (!Target)
		{
			// 입력 해제만 재획득 억제한다. 질주/거리/가림은 별도 해제 사유다.
			if (IsPlayerRelease(Reason))
			{
				SuppressHitUntil = GetWorld()->GetTimeSeconds() + GetTargetLockConfig()->ManualUnlockHitDelay;
			}

			if (IsPlayerRelease(Reason) || Reason == EVBLockReason::Occluded || Reason == EVBLockReason::Gameplay)
			{
				// 수동 해제는 최신 선택도 해제할 수 있다. 옛 가림 보고는 최신 대상을 해제하지 않는다.
				if (Reason != EVBLockReason::Occluded || ExpectedRevision == LockState.Revision)
				{
					SetLockOn(nullptr, Reason);
				}
			}
		}
		else
		{
			const bool bAllowedReason = Reason == EVBLockReason::Manual || Reason == EVBLockReason::Switch ||
				Reason == EVBLockReason::Guard || Reason == EVBLockReason::Reacquire;
			const bool bReasonStateValid = (Reason != EVBLockReason::Switch || IsValid(LockState.Target)) &&
				(Reason != EVBLockReason::Guard || !IsValid(LockState.Target)) &&
				(Reason != EVBLockReason::Reacquire || (!IsValid(LockState.Target) &&
					LockState.Reason == EVBLockReason::TargetDied && GetTargetLockConfig()->bAutoRetargetOnDeath));
			if (bAllowedReason && bReasonStateValid
				&& (Reason == EVBLockReason::Manual || Reason == EVBLockReason::Switch || !IsAutoLockSuppressed()) &&
				CanOwnerLock() && ExpectedRevision == LockState.Revision &&
				IsTargetValid(Target) && VBTargetLockPolicy::IsWithinDistance(
					FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()),
					VBTargetLockPolicy::AcquisitionRange(GetTargetLockConfig()->MaxLockDistance,
						GetTargetLockConfig()->TargetSearchRadius)) &&
				HasLineOfSight(Target, OwnerCharacter->GetPawnViewLocation()))
			{
				SetLockOn(Target, Reason);
			}
		}
	}
	// 거절도 응답하여 클라 대기 상태가 영구히 남지 않게 한다.
	ClientLockResponse(RequestId, LockState);
}

void UVBTargetLockComponent::ClientLockResponse_Implementation(uint32 RequestId, FVBTargetLockState State)
{
	if (RequestId == PendingRequestId)
	{
		PendingRequestId = 0;
		bSuppressCameraUntilResponse = false;
	}

	AcceptState(State);
}

void UVBTargetLockComponent::SetLockOn(AActor* Target, EVBLockReason Reason)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (bServerLockActive)
	{
		EndServerLock();
		bServerLockActive = false;
	}

	LockState.Target = Target;
	LockState.Reason = Reason;
	++LockState.Revision;
	if (IsValid(Target))
	{
		BeginServerLock();
		bServerLockActive = true;
	}

	VB_LOG(Log, "[LockOn] revision=%u reason=%s(%d) target=%s", LockState.Revision,
		*StaticEnum<EVBLockReason>()->GetNameStringByValue(int64(Reason)), int32(Reason), *GetNameSafe(Target));
	AcceptState(LockState);
	GetOwner()->ForceNetUpdate();
}

void UVBTargetLockComponent::AcceptState(const FVBTargetLockState& State)
{
	if (State.Revision < ConfirmedState.Revision)
	{
		return;
	}
	// 같은 revision의 NetGUID가 나중에 해석될 수 있다. 이미 유효한 상태를 null로 되돌리지는 않는다.
	if (State.Revision == ConfirmedState.Revision &&
		(State.Target == ConfirmedState.Target || !IsValid(State.Target) || IsValid(ConfirmedState.Target)))
	{
		return;
	}

	const bool bHadTarget = IsLockedOn();
	// 죽어서 풀렸으면 그 자리를 기억한다 - 다음 대상은 방금 죽은 적 근처에서 고른다.
	bHasDeathRetargetAnchor = State.Reason == EVBLockReason::TargetDied && IsValid(ConfirmedState.Target);
	if (bHasDeathRetargetAnchor)
	{
		DeathRetargetAnchor = ConfirmedState.Target->GetActorLocation();
	}
	// 죽은 대상이 이미 사라졌으면(복제 순서) 자리를 모른다 - 화면 가운데 기준으로 고른다.
	VB_CLOG(State.Reason == EVBLockReason::TargetDied && !bHasDeathRetargetAnchor, Log,
		"[LockOn] 처치 뒤 다음 대상: 죽은 대상이 이미 없어 화면 가운데 기준으로 고른다");
	const bool bRetainDefeatView = State.Reason == EVBLockReason::TargetDied
		&& CameraFramingId != 0 && CameraFramingTarget.IsValid()
		&& CameraFramingTarget.Get() == ConfirmedState.Target && CameraFraming.DefeatFollowSeconds > 0.0f;
	if (bRetainDefeatView)
	{
		DefeatFramingUntil = GetWorld()->GetTimeSeconds() + CameraFraming.DefeatFollowSeconds;
	}

	const bool bKeepFraming = bRetainDefeatView || (State.Reason == EVBLockReason::Reacquire && HasDefeatFraming());
	ConfirmedState = State;
	if (!IsLockedOn() && IsPlayerRelease(State.Reason))
	{
		SuppressHitUntil = GetWorld()->GetTimeSeconds() + GetTargetLockConfig()->ManualUnlockHitDelay;
	}

	CancelHitRecenter(true); // 가드/패링/해제 전환 뒤 옛 명중 보정이 재개되지 않게 한다.
	ResetCameraInput(bKeepFraming);
	RetreatSprintSeconds = 0.0f;
	OccludedSeconds = 0.0f;
	VisibilityCheckElapsed = GetTargetLockConfig()->VisibilityCheckInterval;
	bTargetVisible = true;
	bHasVisibleAimPoint = false;
	if (IsLockedOn())
	{
		OnTargetLocked.Broadcast(GetLockedTarget());
	}
	else
	{
		ResetSwitchInput();
		OnTargetUnlocked.Broadcast(nullptr);
	}
	// 서버가 확인한 사망만 재선택한다. 파괴/거리/수동 해제는 제외한다.
	// 자산(bAutoRetargetOnDeath)은 기능의 켜짐, 플레이어 설정(게임플레이 탭)은 그 사람의 취향 - 둘 다 켜져 있어야 넘어간다.
	bDeathReacquirePending = State.Reason == EVBLockReason::TargetDied &&
		GetTargetLockConfig()->bAutoRetargetOnDeath && !IsAutoLockSuppressed() &&
		OwnerCharacter.IsValid() && OwnerCharacter->IsLocallyControlled() &&
		UVBSaveGameSubsystem::GetSettingsData(this).bAutoTargetNextOnKill;
	SetComponentTickEnabled(IsLockedOn() || bHadTarget || bDeathReacquirePending || PendingRequestId != 0);
}

void UVBTargetLockComponent::OnRep_LockState() { RefreshOwner(); AcceptState(LockState); }

void UVBTargetLockComponent::ClearLockOn()
{
	RefreshOwner();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (bServerLockActive || LockState.Target)
		{
			SetLockOn(nullptr, EVBLockReason::Gameplay);
		}
	}
	else
	{
		RequestLock(nullptr, EVBLockReason::Gameplay);
	}
}

void UVBTargetLockComponent::TryAutoLockOnTarget(AActor* Target)
{
	RefreshOwner();
	if (!VBServerWrite::HasOwnerAuthority(*this) || !CanOwnerLock() || IsLockedOn())
	{
		return;
	}

	if (!GetTargetLockConfig()->bAutoLockOnHit || IsAutoLockSuppressed())
	{
		return;
	}

	if (IsTargetValid(Target))
	{
		SetLockOn(Target, EVBLockReason::Hit);
	}
}

void UVBTargetLockComponent::NotifyMeleeHitForCamera(AActor* Target)
{
	RefreshOwner();
	if (!VBServerWrite::HasOwnerAuthority(*this) || !CanOwnerLock() || IsLockedOn() ||
		!GetTargetLockConfig()->bRecenterCameraOnHit || IsAutoLockSuppressed() || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target))
	{
		return;
	}

	if (const AGameStateBase* GameState = GetWorld()->GetGameState())
	{
		ClientRecenterOnHit(Target, OwnerController.Get(), GameState->GetServerWorldTimeSeconds());
	}
}

void UVBTargetLockComponent::CancelHitRecenter(bool bSuppressNew)
{
	HitRecenterWindow.Cancel();
	if (bSuppressNew && GetWorld())
	{
		HitRecenterWindow.RecordInput(GetWorld()->GetTimeSeconds());
	}

	HitRecenterTarget.Reset();
}

void UVBTargetLockComponent::NotifyManualCameraInput(float Yaw, float Pitch)
{
	if (!GetWorld() || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch) ||
		(FMath::IsNearlyZero(Yaw) && FMath::IsNearlyZero(Pitch)))
	{
		return;
	}

	CancelHitRecenter(true);
}

void UVBTargetLockComponent::ClientRecenterOnHit_Implementation(AActor* Target, APlayerController* HitController, double ServerHitTime)
{
	RefreshOwner();
	if (!OwnerController.IsValid() || HitController != OwnerController.Get() || !OwnerController->IsLocalController())
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	if (!GameState || ServerHitTime <= LastReceivedHitTime ||
		!VBTargetLockPolicy::IsHitNotificationFresh(GameState->GetServerWorldTimeSeconds(), ServerHitTime,
			Cfg->HitRecenterMaxNotificationAge))
	{
		return;
	}

	LastReceivedHitTime = ServerHitTime;
	// 거절된 명중도 연타 간격에 포함한다. 화면 중앙에서 시작한 콤보가 중간부터 시점을 빼앗지 않는다.
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bCanStart = HitRecenterWindow.RegisterHit(Now, Cfg->HitRecenterQuietSeconds, Cfg->HitRecenterInputGraceSeconds);
	if (!bCanStart || !Cfg->bRecenterCameraOnHit || !CanUseCombatCamera() || IsLockedOn() || PendingRequestId ||
		IsAutoLockSuppressed() || OwnerController->IsLookInputIgnored() || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target) || !OwnerController->PlayerCameraManager)
	{
		return;
	}

	FVector2D Screen;
	// 카메라 뒤의 적으로 급회전하지 않는다. 전방 화면 밖 적은 짧은 각도 상한 안에서만 보정한다.
	if (!ProjectTarget(Target, Screen, false) || VBTargetLockPolicy::IsInRecenterSafeFrame(Screen, Cfg->HitRecenterSafeHalfExtent))
	{
		return;
	}

	const FVector CameraLocation = OwnerController->PlayerCameraManager->GetCameraLocation();
	if (!HasLineOfSight(Target, CameraLocation))
	{
		return;
	}

	const FVector AimDirection = GetAimPoint(Target) - CameraLocation;
	if (AimDirection.IsNearlyZero())
	{
		return;
	}

	HitRecenterStartRotation = OwnerController->GetControlRotation();
	const FRotator Error = (AimDirection.Rotation() - OwnerController->PlayerCameraManager->GetCameraRotation()).GetNormalized();
	HitRecenterDelta = FRotator(
		FMath::Clamp(Error.Pitch, -double(FMath::Max(0.0f, Cfg->HitRecenterMaxPitch)), double(FMath::Max(0.0f, Cfg->HitRecenterMaxPitch))),
		FMath::Clamp(Error.Yaw, -double(FMath::Max(0.0f, Cfg->HitRecenterMaxYaw)), double(FMath::Max(0.0f, Cfg->HitRecenterMaxYaw))), 0.0);
	const double EndPitch = FMath::ClampAngle(HitRecenterStartRotation.Pitch + HitRecenterDelta.Pitch, Cfg->MinCameraPitch,
		Cfg->MaxCameraPitch);
	// 이미 수동 시점이 pitch 범위 밖이면 범위 안으로 강제로 끌어오지 않는다.
	const double ClampedPitchDelta = FMath::FindDeltaAngleDegrees(HitRecenterStartRotation.Pitch, EndPitch);
	if (ClampedPitchDelta * HitRecenterDelta.Pitch <= 0.0 || FMath::Abs(ClampedPitchDelta) > FMath::Abs(HitRecenterDelta.Pitch))
	{
		HitRecenterDelta.Pitch = 0.0;
	}
	else
	{
		HitRecenterDelta.Pitch = ClampedPitchDelta;
	}

	HitRecenterTarget = Target;
	HitRecenterWindow.Start(Now, Cfg->HitRecenterDuration);
}

void UVBTargetLockComponent::UpdateHitRecenter()
{
	RefreshOwner();
	if (!HitRecenterWindow.bActive)
	{
		return;
	}

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	AActor* Target = HitRecenterTarget.Get();
	if (!Cfg->bRecenterCameraOnHit || !CanUseCombatCamera() || IsLockedOn() || PendingRequestId ||
		IsAutoLockSuppressed() || OwnerController->IsLookInputIgnored() || !IsTargetValid(Target) ||
		UVBAbilitySystemStatics::IsActorDead(Target) || !OwnerController->PlayerCameraManager)
	{
		CancelHitRecenter();
		return;
	}

	FVector2D Screen;
	if (!ProjectTarget(Target, Screen, false) || VBTargetLockPolicy::IsInRecenterSafeFrame(Screen, Cfg->HitRecenterSafeHalfExtent) ||
		!HasLineOfSight(Target, OwnerController->PlayerCameraManager->GetCameraLocation()))
	{
		CancelHitRecenter();
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	// 목표 회전은 명중 때 한 번만 확정한다. 움직이는 적을 지속 추적하지 않는다.
	// 긴 프레임 지연 뒤에도 끝 각도로 순간 이동하지 않는다.
	if (Now >= HitRecenterWindow.EndTime)
	{
		CancelHitRecenter();
		return;
	}

	const double Alpha = HitRecenterWindow.GetAlpha(Now);
	OwnerController->SetControlRotation((HitRecenterStartRotation + HitRecenterDelta * Alpha).GetNormalized());
}

void UVBTargetLockComponent::FaceAndLockParryTarget(AActor* Target, bool bLock, bool bFace)
{
	RefreshOwner();
	if (!VBServerWrite::HasOwnerAuthority(*this) || !OwnerCharacter.IsValid() || !IsValid(Target))
	{
		return;
	}
	// 패링은 이 공격자를 향한 명시적 행동이라 기존 락과 수동 해제 억제를 넘는다(TryAutoLockOnTarget 와 다름). 락은 범위 안 대상이 필요하고 바라보기는 아니다.
	if (bLock && CanOwnerLock() && IsTargetValid(Target) && GetLockedTarget() != Target)
	{
		SetLockOn(Target, EVBLockReason::Parry);
	}
	// 캡슐 방향을 가진 기능(사다리, 매달림, 등반: VBBodyControlPolicy)은 그대로 두고, 카메라만 락으로 돈다.
	if (bFace && !VBBodyControlPolicy::BlocksLockFacing(OwnerCharacter->GatherCapsuleRotationFacts()))
	{
		const FRotator Current = OwnerCharacter->GetActorRotation();
		const float Yaw = VBTargetLockPolicy::FacingYaw(OwnerCharacter->GetActorLocation(), Target->GetActorLocation(), Current.Yaw);
		// 한 번만 돌린다 - 가드 중엔 strafe 라 캡슐이 controller yaw 를 따르고, ClientFaceYaw 가 그 yaw 를 같은 공격자로 돌린다.
		OwnerCharacter->SetActorRotation(FRotator(Current.Pitch, Yaw, Current.Roll));
		ClientFaceYaw(Yaw);
	}
}

void UVBTargetLockComponent::ClientFaceYaw_Implementation(float Yaw)
{
	RefreshOwner();
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	// 남은 피격 재중심이 카메라를 이전 대상 쪽으로 끌지 않게 끊는다.
	CancelHitRecenter(true);
	if (OwnerController.IsValid())
	{
		FRotator Control = OwnerController->GetControlRotation();
		Control.Yaw = Yaw;
		OwnerController->SetControlRotation(Control);
	}

	const FRotator Current = OwnerCharacter->GetActorRotation();
	OwnerCharacter->SetActorRotation(FRotator(Current.Pitch, Yaw, Current.Roll));
}

void UVBTargetLockComponent::TryLockOnFromLocalView()
{
	RefreshOwner();
	if (!CanOwnerLock() || !OwnerCharacter->IsLocallyControlled() || IsLockedOn() || PendingRequestId || IsAutoLockSuppressed())
	{
		return;
	}

	if (AActor* Target = FindBestTarget())
	{
		RequestLock(Target, EVBLockReason::Guard);
	}
}

void UVBTargetLockComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshOwner();
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	if (GetOwner()->HasAuthority() && bServerLockActive)
	{
		TickServerLock(DeltaTime);
	}

	if (bDeathReacquirePending && PendingRequestId == 0)
	{
		bDeathReacquirePending = false;
		if (CanOwnerLock())
		{
			if (AActor* Target = FindScreenTarget(0.0f, bHasDeathRetargetAnchor ? &DeathRetargetAnchor : nullptr))
			{
				RequestLock(Target, EVBLockReason::Reacquire);
			}
		}
	}

	if (DefeatFramingUntil >= 0.0)
	{
		if (HasDefeatFraming() && CanUseCombatCamera())
		{
			UpdateCameraRotation(DeltaTime);
			return;
		}

		EndCameraFraming(CameraFramingId);
	}

	if (IsLockedOn() && OwnerCharacter->IsLocallyControlled())
	{
		TickLocalCameraTracking(DeltaTime);
	}

	if (!IsLockedOn() && !bServerLockActive && !PendingRequestId && !bDeathReacquirePending && !HasActiveCameraFraming())
	{
		SetComponentTickEnabled(false);
	}
}

void UVBTargetLockComponent::TickServerLock(float DeltaTime)
{
	AActor* Target = LockState.Target.Get();
	if (!CanOwnerLock())
	{
		SetLockOn(nullptr, EVBLockReason::Gameplay);
	}
	else if (IsValid(Target) && UVBAbilitySystemStatics::IsActorDead(Target))
	{
		SetLockOn(nullptr, EVBLockReason::TargetDied);
	}
	else if (!IsTargetEligible(Target))
	{
		SetLockOn(nullptr, EVBLockReason::InvalidTarget);
	}
	else if (!VBTargetLockPolicy::IsWithinDistance(FVector::DistSquared(OwnerCharacter->GetActorLocation(), Target->GetActorLocation()),
		GetTargetLockConfig()->LockBreakDistance))
	{
		SetLockOn(nullptr, EVBLockReason::OutOfRange);
	}
	else
	{
		TickRetreatSprint(*Target, DeltaTime);
	}
}

void UVBTargetLockComponent::TickRetreatSprint(const AActor& Target, float DeltaTime)
{
	const UCharacterMovementComponent* Movement = OwnerCharacter->GetCharacterMovement();
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	const bool bRetreating = Movement && !UVBFinisherComponent::IsActorInScene(OwnerCharacter.Get()) &&
		VBTargetLockPolicy::IsRetreatSprint(UVBLocomotionIntentComponent::IsSprintingFor(OwnerCharacter.Get()),
			Movement->IsMovingOnGround(),
			Movement->GetCurrentAcceleration(), Target.GetActorLocation() - OwnerCharacter->GetActorLocation(),
			Cfg->RetreatSprintDotThreshold);
	RetreatSprintSeconds = bRetreating ? RetreatSprintSeconds + DeltaTime : 0.0f;
	if (bRetreating && RetreatSprintSeconds >= Cfg->RetreatSprintUnlockSeconds)
	{
		SuppressHitUntil = GetWorld()->GetTimeSeconds() + Cfg->ManualUnlockHitDelay;
		SetLockOn(nullptr, EVBLockReason::RetreatSprint);
	}
	else if (UVBWeaponStateComponent* Weapons = OwnerWeaponState.Get())
	{
		// 전투 유지는 BeginServerLock 한 번으로 충분하지만, 이 호출이 이탈 타이머를 매 틱 되감아 락을 푼 뒤 전체 대기를 보장한다.
		// 락 해제 때 한 번 되감는 쪽으로 옮기면 사망 때 StopCombatTimer 로 멈춘 타이머가 되살아난다.
		Weapons->EnterCombat();
	}
}

void UVBTargetLockComponent::TickLocalCameraTracking(float DeltaTime)
{
	if (!CanTrackCamera())
	{
		OccludedSeconds = 0.0f;
		if (bWasCameraTracking)
		{
			ResetCameraInput();
		}

		bWasCameraTracking = false;
		return;
	}

	bWasCameraTracking = true;
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	VisibilityCheckElapsed += DeltaTime;
	if (VisibilityCheckElapsed >= FMath::Max(0.01f, Cfg->VisibilityCheckInterval))
	{
		VisibilityCheckElapsed = 0.0f;
		const FVector Start = OwnerController->PlayerCameraManager ?
			OwnerController->PlayerCameraManager->GetCameraLocation() : OwnerCharacter->GetPawnViewLocation();
		bTargetVisible = HasLineOfSight(GetLockedTarget(), Start);
	}

	if (bTargetVisible)
	{
		OccludedSeconds = 0.0f;
		LastVisibleAimPoint = GetAimPoint(GetLockedTarget());
		bHasVisibleAimPoint = true;
		return;
	}

	OccludedSeconds += DeltaTime;
	if (OccludedSeconds >= Cfg->OcclusionGraceSeconds && PendingRequestId == 0)
	{
		RequestLock(nullptr, EVBLockReason::Occluded);
	}
}

bool UVBTargetLockComponent::CanTrackCamera() const
{
	return !bSuppressCameraUntilResponse && (IsTargetEligible(GetLockedTarget()) || HasDefeatFraming()) && CanUseCombatCamera();
}

bool UVBTargetLockComponent::CanUseCombatCamera() const
{
	// 카메라 자격 넷을 먼저 본다 - 소유자, 컨트롤러 null 가드가 이 줄에 있다.
	if (!CanOwnerLock() || !OwnerCharacter->IsLocallyControlled() ||
		UVBLocomotionIntentComponent::IsAimingFor(OwnerCharacter.Get()) || OwnerController->GetViewTarget() != OwnerCharacter.Get())
	{
		return false;
	}
	// 캐릭터 상태(처형, 매달림/등반/사다리, 래그돌, 스마트 오브젝트, 환경 상호작용)는 VBBodyControlPolicy 가 정한다.
	return !VBBodyControlPolicy::BlocksCombatCamera(OwnerCharacter->GatherCapsuleRotationFacts());
}

void UVBTargetLockComponent::ResetCameraInput(bool bKeepFraming)
{
	if (!bKeepFraming)
	{
		EndCameraFraming(CameraFramingId);
	}

	LookOffset = FVector2D::ZeroVector;
	LastLookInputTime = -1.0;
	bWasCameraTracking = false;
}

void UVBTargetLockComponent::AddLookInput(float Yaw, float Pitch, bool bGamepad)
{
	if (!CanTrackCamera() || !FMath::IsFinite(Yaw) || !FMath::IsFinite(Pitch))
	{
		return;
	}

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	// 마우스 입력은 락온 중 화면 틀만 옮긴다 - 풀지 않는다.
	const float AppliedYaw = bGamepad ? 0.0f : Yaw;
	if (FMath::IsNearlyZero(AppliedYaw) && FMath::IsNearlyZero(Pitch))
	{
		return;
	}
	// 오프셋 단위는 control rotation 위의 도(degree)다.
	const FVector2D Degrees = LookInputToControlDegrees(OwnerController.Get(), AppliedYaw, Pitch);
	LookOffset.X = FMath::Clamp(LookOffset.X + Degrees.X, -double(Cfg->MaxLookYawOffset), double(Cfg->MaxLookYawOffset));
	LookOffset.Y = FMath::Clamp(LookOffset.Y + Degrees.Y, -double(Cfg->MaxLookPitchOffset), double(Cfg->MaxLookPitchOffset));
	LastLookInputTime = GetWorld()->GetTimeSeconds();
}

FVector2D UVBTargetLockComponent::LookInputToControlDegrees(const APlayerController* Controller, float Yaw, float Pitch)
{
	// APlayerController::AddYawInput/AddPitchInput 과 같은 변환. deprecated getter 는 설정이 꺼지면 경고를 남겨 설정을 먼저 보고 config 값을 직접 읽는다.
	if (!Controller || !GetDefault<UInputSettings>()->bEnableLegacyInputScales)
	{
		return FVector2D(Yaw, Pitch);
	}

	return FVector2D(Yaw * Controller->InputYawScale_DEPRECATED, Pitch * Controller->InputPitchScale_DEPRECATED);
}

uint64 UVBTargetLockComponent::BeginCameraFraming(AActor* Target, const FVBTargetLockFraming& Framing)
{
	RefreshOwner();
	if (!Framing.IsValid())
	{
		VB_LOG(Error, "Lock camera framing has invalid pitch limits or tracking speeds");
		return 0;
	}

	if (Target != GetLockedTarget() || !CanTrackCamera())
	{
		return 0;
	}

	CameraFraming = Framing;
	CameraFramingTarget = Target;
	DefeatFramingUntil = -1.0;
	CameraFramingId = ++NextCameraFramingId;
	if (CameraFramingId == 0)
	{
		CameraFramingId = ++NextCameraFramingId;
	}

	LookOffset = FVector2D::ZeroVector;
	return CameraFramingId;
}

void UVBTargetLockComponent::EndCameraFraming(uint64 RequestId)
{
	if (!HasCameraFraming(RequestId))
	{
		return;
	}

	CameraFramingId = 0;
	CameraFramingTarget.Reset();
	DefeatFramingUntil = -1.0;
}

bool UVBTargetLockComponent::HasDefeatFraming() const
{
	return CameraFramingId != 0 && CameraFramingTarget.IsValid() && DefeatFramingUntil >= 0.0
		&& GetWorld() && GetWorld()->GetTimeSeconds() < DefeatFramingUntil;
}

void UVBTargetLockComponent::UpdateCameraRotation(float DeltaTime)
{
	RefreshOwner();
	if (!CanTrackCamera())
	{
		return;
	}

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	if (GetWorld()->GetTimeSeconds() - LastLookInputTime > Cfg->LookOffsetReturnDelay)
	{
		LookOffset = FMath::Vector2DInterpTo(LookOffset, FVector2D::ZeroVector, DeltaTime, Cfg->LookOffsetReturnSpeed);
	}

	const FVector Start = OwnerController->PlayerCameraManager ?
		OwnerController->PlayerCameraManager->GetCameraLocation() : OwnerCharacter->GetPawnViewLocation();
	const bool bDefeatView = HasDefeatFraming();
	if (!bDefeatView && !bTargetVisible && !bHasVisibleAimPoint)
	{
		return;
	}

	const FVector Aim = bDefeatView ? GetAimPoint(CameraFramingTarget.Get())
		: (bTargetVisible ? GetAimPoint(GetLockedTarget()) : LastVisibleAimPoint);
	if ((Aim - Start).IsNearlyZero())
	{
		return;
	}

	if (!bDefeatView && CameraFramingTarget.Get() != GetLockedTarget())
	{
		EndCameraFraming(CameraFramingId);
	}

	const FVBTargetLockFraming* Framing = CameraFramingId ? &CameraFraming : nullptr;
	FRotator Desired = Framing ? Framing->DesiredRotation(OwnerCharacter->GetActorRotation().Yaw, Start, Aim) : (Aim - Start).Rotation();
	Desired.Yaw += LookOffset.X;
	Desired.Pitch = FMath::ClampAngle(Desired.Pitch + LookOffset.Y,
		Framing ? Framing->MinPitch : Cfg->MinCameraPitch, Framing ? Framing->MaxPitch : Cfg->MaxCameraPitch);
	const FRotator Current = OwnerController->GetControlRotation();
	FRotator Delta = (Desired - Current).GetNormalized();
	if (FMath::Abs(Delta.Yaw) < Cfg->TrackingDeadZoneDegrees)
	{
		Delta.Yaw = 0.0;
	}

	if (FMath::Abs(Delta.Pitch) < Cfg->TrackingDeadZoneDegrees)
	{
		Delta.Pitch = 0.0;
	}

	const double Alpha = 1.0 - FMath::Exp(-FMath::Max(0.0f, Framing ? Framing->InterpSpeed : Cfg->InterpSpeed) * DeltaTime);
	const double MaxStep = FMath::Max(0.0f, Framing ? Framing->MaxTrackingDegreesPerSecond : Cfg->MaxTrackingDegreesPerSecond) * DeltaTime;
	FRotator Next(Current.Pitch + FMath::Clamp(Delta.Pitch * Alpha, -MaxStep, MaxStep),
		Current.Yaw + FMath::Clamp(Delta.Yaw * Alpha, -MaxStep, MaxStep), 0.0);
	OwnerController->SetControlRotation(Next.GetNormalized());
}

bool UVBTargetLockComponent::IsTargetEligible(AActor* Actor) const
{
	if (!IsValid(Actor) || Actor == GetOwner())
	{
		return false;
	}

	return IVBTargetable::IsActorTargetable(Actor);
}

bool UVBTargetLockComponent::IsTargetValid(AActor* Actor) const
{
	return OwnerCharacter.IsValid() && IsTargetEligible(Actor) &&
		VBTargetLockPolicy::IsWithinDistance(FVector::DistSquared(OwnerCharacter->GetActorLocation(), Actor->GetActorLocation()),
			VBTargetLockPolicy::AcquisitionRange(GetTargetLockConfig()->MaxLockDistance, GetTargetLockConfig()->TargetSearchRadius));
}

bool UVBTargetLockComponent::IsAutoLockSuppressed() const
{
	return bSuppressCameraUntilResponse || !GetWorld() || GetWorld()->GetTimeSeconds() < SuppressHitUntil;
}

FVector UVBTargetLockComponent::GetAimPoint(AActor* Target) const
{
	if (const IVBTargetable* Targetable = Cast<IVBTargetable>(Target))
	{
		return Targetable->GetTargetMarkerLocation();
	}

	return IsValid(Target) ? Target->GetActorLocation() : FVector::ZeroVector;
}

bool UVBTargetLockComponent::HasLineOfSight(AActor* Target, const FVector& Start) const
{
	if (!IsValid(Target) || !GetWorld())
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBLockVisibility), false, GetOwner());
	TArray<AActor*> Attached;
	GetOwner()->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	// 마커 하나가 가려져도 몸통이 보이는 큰 적은 후보로 유지한다.
	for (const FVector& End : {GetAimPoint(Target), Target->GetActorLocation()})
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, GetTargetLockConfig()->VisibilityTraceChannel, Params) ||
			Hit.GetActor() == Target)
		{
			return true;
		}
	}

	return false;
}

bool UVBTargetLockComponent::ProjectTarget(AActor* Target, FVector2D& OutScreen, bool bRequireOnScreen) const
{
	if (!IsValid(Target) || !OwnerController.IsValid() || !OwnerController->PlayerCameraManager)
	{
		return false;
	}

	const ULocalPlayer* LP = OwnerController->GetLocalPlayer();
	if (!LP || !LP->ViewportClient || !LP->ViewportClient->Viewport)
	{
		return false;
	}

	FSceneViewProjectionData Projection;
	if (!LP->GetProjectionData(LP->ViewportClient->Viewport, Projection))
	{
		return false;
	}

	const FIntRect Rect = Projection.GetConstrainedViewRect();
	if (Rect.Width() <= 0 || Rect.Height() <= 0)
	{
		return false;
	}

	const FVector Aim = GetAimPoint(Target);
	const FVector Direction = Aim - OwnerController->PlayerCameraManager->GetCameraLocation();
	if (FVector::DotProduct(Direction, OwnerController->PlayerCameraManager->GetCameraRotation().Vector()) <= 0.0)
	{
		return false;
	}

	if (!OwnerController->ProjectWorldLocationToScreen(Aim, OutScreen, true))
	{
		return false;
	}

	OutScreen /= FVector2D(Rect.Width(), Rect.Height());
	return !OutScreen.ContainsNaN() && (!bRequireOnScreen ||
		(OutScreen.X >= 0.0 && OutScreen.X <= 1.0 && OutScreen.Y >= 0.0 && OutScreen.Y <= 1.0));
}

AActor* UVBTargetLockComponent::FindScreenTarget(float Direction, const FVector* RetargetAnchor)
{
	if (!CanOwnerLock() || !OwnerController->PlayerCameraManager)
	{
		return nullptr;
	}

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	const bool bSideSwitch = VBTargetLockPolicy::IsSideSwitch(Direction);
	TArray<AActor*> Candidates;
	TArray<AActor*> Ignore{OwnerCharacter.Get()};
	if (bSideSwitch)
	{
		Ignore.Add(GetLockedTarget());
	}

	const float Range = VBTargetLockPolicy::AcquisitionRange(Cfg->MaxLockDistance, Cfg->TargetSearchRadius);
	if (Range <= 0.0f)
	{
		return nullptr;
	}

	OverlapTargets(OwnerCharacter->GetActorLocation(), Range, Ignore, Candidates);

	VBTargetLockPolicy::FScreenTargetRules Rules;
	Rules.Direction = Direction;
	Rules.bRetarget = RetargetAnchor != nullptr;
	Rules.ConeHalfAngle = Cfg->LockAngle;
	Rules.MinSideDelta = Cfg->MinSwitchHorizontalDelta;
	Rules.VerticalWeight = Cfg->ScreenVerticalWeight;
	Rules.DistanceWeight = Cfg->AcquisitionDistanceWeight;
	Rules.Range = Range;
	if (bSideSwitch)
	{
		FVector2D Current;
		if (ProjectTarget(GetLockedTarget(), Current, false))
		{
			Rules.Origin = Current;
		}
	}

	AActor* Best = nullptr;
	double BestScore = TNumericLimits<double>::Max();
	double BestDistance = TNumericLimits<double>::Max();
	const APlayerCameraManager* CameraManager = OwnerController->PlayerCameraManager;
	const FVector Camera = CameraManager->GetCameraLocation();
	for (AActor* Candidate : Candidates)
	{
		FVector2D Screen;
		if (!IsTargetValid(Candidate) || !ProjectTarget(Candidate, Screen, true))
		{
			continue;
		}

		if (!VBTargetLockPolicy::PassesScreenFilter(Rules, CameraManager->GetCameraRotation().Vector(),
			GetAimPoint(Candidate) - Camera, Screen))
		{
			continue;
		}

		if (!HasLineOfSight(Candidate, Camera))
		{
			continue;
		}

		const double Distance = FVector::DistSquared(OwnerCharacter->GetActorLocation(), Candidate->GetActorLocation());
		// 처치 뒤 다음 대상은 죽은 자리에서 가까운 순이다.
		const double Score = RetargetAnchor ? FVector::DistSquared(*RetargetAnchor, Candidate->GetActorLocation())
			: VBTargetLockPolicy::ScreenTargetScore(Rules, Screen, Distance);
		const VBTargetLockPolicy::EScreenTargetOrder Order = VBTargetLockPolicy::CompareScreenTarget(Score, Distance, BestScore,
			BestDistance);
		if (Order == VBTargetLockPolicy::EScreenTargetOrder::Better
			|| (Order == VBTargetLockPolicy::EScreenTargetOrder::SameDistance && Best && Candidate->GetPathName() < Best->GetPathName()))
		{
			Best = Candidate;
			BestScore = Score;
			BestDistance = Distance;
		}
	}

	return Best;
}

bool UVBTargetLockComponent::OverlapTargets(const FVector& Origin, float Radius, const TArray<AActor*>& Ignore,
	TArray<AActor*>& OutActors) const
{
	TArray<TEnumAsByte<EObjectTypeQuery>> Types;
	Types.Add(UEngineTypes::ConvertToObjectType(GetTargetLockConfig()->SearchTraceChannel));
	return UKismetSystemLibrary::SphereOverlapActors(this, Origin, Radius, Types, nullptr, Ignore, OutActors);
}

AActor* UVBTargetLockComponent::ProposeDirectionalTarget(const FVector2D& WorldIntent, float StepMaxDistance) const
{
	const UVBTargetLockConfig* Cfg = GetTargetLockConfig();
	AActor* Current = LockState.Target.Get();
	if (!Cfg->bDirectionalAttackSwitch || !VBServerWrite::HasOwnerAuthority(*this) || !OwnerCharacter.IsValid()
		|| !IsValid(Current) || WorldIntent.ContainsNaN())
	{
		return nullptr;
	}

	const float Range = FMath::Min3(StepMaxDistance, Cfg->DirectionalMaxDistance,
		VBTargetLockPolicy::AcquisitionRange(Cfg->MaxLockDistance, Cfg->TargetSearchRadius));
	if (!(Range > 0.0f))
	{
		return nullptr;
	}

	const FVector Origin = OwnerCharacter->GetActorLocation();
	TArray<AActor*> Overlaps;
	OverlapTargets(Origin, Range, {OwnerCharacter.Get(), Current}, Overlaps);
	// 동점의 마지막 순서를 겹침 순서가 아니라 이름으로 정한다(같은 상황이면 같은 답).
	Overlaps.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });

	TArray<VBTargetLockPolicy::FDirectionalCandidate> Facts;
	const FVector View = OwnerCharacter->GetPawnViewLocation();
	for (AActor* Candidate : Overlaps)
	{
		const FVector Delta = Candidate->GetActorLocation() - Origin;
		VBTargetLockPolicy::FDirectionalCandidate& Fact = Facts.AddDefaulted_GetRef();
		Fact.GroundDirection = FVector2D(Delta.X, Delta.Y).GetSafeNormal();
		Fact.Distance2D = float(FVector2D(Delta.X, Delta.Y).Size());
		Fact.VerticalDelta = float(Delta.Z);
		// 시야(트레이스 둘)는 여기서 재지 않는다 - 고른 후보에만 아래에서 잰다.
		Fact.bEligible = IsTargetEligible(Candidate) && !UVBAbilitySystemStatics::IsActorDead(Candidate)
			&& !UVBAbilitySystemStatics::IsFriendlyFire(OwnerCharacter.Get(), Candidate);
	}

	VBTargetLockPolicy::FDirectionalParams Params;
	Params.MinInputMagnitude = Cfg->DirectionalMinInputMagnitude;
	Params.CandidateHalfAngle = Cfg->DirectionalCandidateHalfAngle;
	Params.CurrentKeepHalfAngle = Cfg->DirectionalCurrentKeepHalfAngle;
	Params.AngleAdvantage = Cfg->DirectionalAngleAdvantage;
	Params.AngleTieBand = Cfg->DirectionalAngleTieBand;
	Params.MaxDistance = Range;
	Params.MaxVerticalDelta = Cfg->DirectionalMaxVerticalDelta;
	const FVector ToCurrent = Current->GetActorLocation() - Origin;
	const FVector2D CurrentDirection = FVector2D(ToCurrent.X, ToCurrent.Y).GetSafeNormal();
	// 고른 후보가 가려졌으면 빼고 다시 고른다 - 결과는 모두 미리 시야를 잰 것과 같고, 트레이스는 고른 후보 수만큼만 돈다.
	for (int32 Picked = VBTargetLockPolicy::PickDirectionalTarget(WorldIntent, CurrentDirection, true, Facts, Params);
		Overlaps.IsValidIndex(Picked);
		Picked = VBTargetLockPolicy::PickDirectionalTarget(WorldIntent, CurrentDirection, true, Facts, Params))
	{
		if (HasLineOfSight(Overlaps[Picked], View))
		{
			return Overlaps[Picked];
		}

		Facts[Picked].bEligible = false;
	}

	return nullptr;
}

bool UVBTargetLockComponent::CommitDirectionalSwitch(AActor* Target, uint32 ExpectedRevision)
{
	RefreshOwner();
	// 일반 전환(ServerRequestLock 의 Switch)과 같은 관문 - 락온 중, 같은 판, 대상 자격, 거리, 시야. 쓰는 곳은 SetLockOn 하나다.
	if (!VBServerWrite::HasOwnerAuthority(*this) || !IsValid(LockState.Target) || ExpectedRevision != LockState.Revision
		|| Target == LockState.Target || !CanOwnerLock() || !IsTargetValid(Target) || !HasLineOfSight(Target,
			OwnerCharacter->GetPawnViewLocation()))
	{
		return false;
	}

	SetLockOn(Target, EVBLockReason::Switch);
	return true;
}

AActor* UVBTargetLockComponent::FindBestTarget() { return FindScreenTarget(0.0f); }
AActor* UVBTargetLockComponent::FindSideTarget(float AxisValue) { return FindScreenTarget(AxisValue); }

void UVBTargetLockComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshOwner();
	if (UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(GetOwner()))
	{
		Devices->OnInputDeviceChanged.AddDynamic(this, &ThisClass::OnInputDeviceChanged);
	}

	// OwnerCharacter, OwnerController 캐싱
	if (AVBCharacter* Owner = Cast<AVBCharacter>(GetOwner()))
	{
		OwnerCharacter = Owner;
		OwnerController = OwnerCharacter->GetController<APlayerController>();
	}
}

void UVBTargetLockComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndCameraFraming(CameraFramingId);
	CancelHitRecenter();
	if (UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(GetOwner()))
	{
		Devices->OnInputDeviceChanged.RemoveDynamic(this, &ThisClass::OnInputDeviceChanged);
	}

	if (GetOwner() && GetOwner()->HasAuthority() && bServerLockActive)
	{
		EndServerLock();
		bServerLockActive = false;
	}

	Super::EndPlay(EndPlayReason);
}

AActor* UVBTargetLockComponent::FindSoftLungeTarget() const
{
	// 락온 없이 공격을 시작할 때 첫 스윙이 돌진할 정면 최근접 적(소프트 타겟) - 콘 안이면 아무나라 술어가 늘 참이다.
	return FindConeTarget([](const AActor*) { return true; });
}

AActor* UVBTargetLockComponent::FindConeTarget(TFunctionRef<bool(const AActor*)> Predicate) const
{
	// 서버/클라가 같은 방향 규칙을 쓰되, 서로 다른 월드 스냅샷의 결과 일치까지 보장하지 않는다.
	if (!OwnerCharacter.IsValid())
	{
		return nullptr;
	}

	const UVBTargetLockConfig* Cfg = GetTargetLockConfig(); // CDO 폴백 - 절대 null 아님

	TArray<AActor*> OverlapResults;
	const FVector Origin = OwnerCharacter->GetActorLocation();
	const FVector Forward = OwnerCharacter->GetActorForwardVector(); // 카메라가 아니라 액터 forward

	if (!OverlapTargets(Origin, Cfg->SoftLungeRadius, {OwnerCharacter.Get()}, OverlapResults))
	{
		return nullptr;
	}

	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(Cfg->SoftLungeAngle));
	AActor* BestTarget = nullptr;
	float BestScore = -1.0f;

	for (AActor* Candidate : OverlapResults)
	{
		if (!Cast<IVBTargetable>(Candidate))
		{
			continue;
		}

		if (!IsTargetEligible(Candidate))
		{
			continue; // 소프트 런지는 자체 반경을 사용한다.
		}

		if (!Predicate(Candidate))
		{
			continue; // 소비자별 「쓸 수 있는 후보」. 점수 경쟁 안에서 걸러야 한다
		}

		const FVector Dir = (Candidate->GetActorLocation() - Origin).GetSafeNormal();
		const float Dot = FVector::DotProduct(Forward, Dir);
		if (Dot < CosLimit)
		{
			continue; // 정면 콘 밖 제외
		}

		const float Distance = FVector::Dist(Origin, Candidate->GetActorLocation());
		const float FinalScore = VBTargetLockPolicy::SoftLungeScore(Dot, Distance, Cfg->SoftLungeRadius, Cfg->DistanceScoreWeight);
		if (FinalScore > BestScore)
		{
			BestScore = FinalScore;
			BestTarget = Candidate;
		}
	}

	return BestTarget; // 상태 변이 0 - 워프 전용
}


void UVBTargetLockComponent::BeginServerLock()
{
	// 서버 전용 - 전투 자극(WSC) + 락온 태그 붙이기. EndServerLock 과 짝.
	//   락온 태그가 붙으면 전투 규칙 표의 State.Combat.LockedOn 줄이 질주 능력을 취소한다.
	if (!OwnerCharacter.IsValid())
	{
		return;
	}

	// 락온 = 전투 자극 - 평상이면 전투(무기 Off) 진입, 발도는 하지 않는다.
	if (UVBWeaponStateComponent* Weapons = OwnerWeaponState.Get())
	{
		Weapons->EnterCombat();
	}

	if (AVBPlayerState* PS = Cast<AVBPlayerState>(OwnerCharacter->GetPlayerState()))
	{
		LockOnTag.Hold(PS->GetVBAbilitySystemComponent(), FGameplayTagContainer(VBGameplayTags::State_Combat_LockedOn),
			EGameplayTagReplicationState::CountToOwner);
	}
}

void UVBTargetLockComponent::EndServerLock()
{
	// CombatMode 는 타이머 기반이라 여기서 강제로 풀지 않는다.
	LockOnTag.Release();
}


void UVBTargetLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBTargetLockComponent, LockState, COND_OwnerOnly, REPNOTIFY_Always);
}
