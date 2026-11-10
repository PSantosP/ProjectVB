// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBLadderMode.h"

#include "EngineUtils.h"
#include "Animation/VBLimbBones.h"
#include "Traversal/VBLadder.h"
#include "Traversal/VBLadderModeHost.h"
#include "Traversal/VBTraversalMontage.h"
#include "Traversal/VBTraversalRules.h"
#include "Traversal/VBTraversalSession.h"
#include "Data/VBTraversalConfig.h"
#include "Data/VBClimbAnimSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "VB/VB.h"

static TAutoConsoleVariable<int32> CVarLadderPlacementTelemetry(
	TEXT("vb.Ladder.PlacementTelemetry"), 0,
	TEXT("Ladder placement (stage B) per frame: 0=off, 1=consumed montage interval [from,to], height, placed capsule and mesh."),
	ECVF_Default);

namespace
{
	bool IsIdleMontage(const UVBClimbAnimSet& Clips, const UAnimMontage* Montage)
	{
		return Montage == Clips.LadderIdleMontage || Montage == Clips.LadderIdleMirrorMontage;
	}

	// Value 를 [-Spacing/2, Spacing/2] 범위로 감아 반환한다.
	float WrapToRung(float Value, float Spacing)
	{
		float Err = FMath::Fmod(Value, Spacing);
		if (Err > Spacing * 0.5f)
		{
			Err -= Spacing;
		}

		if (Err < -Spacing * 0.5f)
		{
			Err += Spacing;
		}

		return Err;
	}

	// 꼭대기 = 손이 가로대에 닿는 가장 높은 곳. 발이 서는 가로대로 내려 맞춰 한계와 대기 자세가 일치하게 한다.
	float ComputeTopHeight(const AVBLadder& Ladder, const UVBClimbAnimSet& Clips)
	{
		float Top = FMath::Max(Ladder.GetLadderHeight() - Clips.LadderTopHandReach, 0.0f);
		if (const float TopSpacing = Ladder.GetRungSpacing(); TopSpacing > KINDA_SMALL_NUMBER)
		{
			const float FootAtTop = Top + Clips.LadderIdleFootOffset - Ladder.GetRungTopOffset();
			Top = FMath::Max(Top - FMath::Fmod(FootAtTop, TopSpacing), 0.0f);
		}

		return Top;
	}

	// 몽타주 구간 [From, To] 의 루트모션을 뽑는다.
	FTransform ExtractLoopedRootMotion(const UAnimMontage& Montage, float From, float To)
	{
		if (To >= From)
		{
			return Montage.ExtractRootMotionFromTrackRange(From, To, FAnimExtractContext());
		}
		// 섹션이 한 바퀴 돌았다. 뒤로 간 것으로 읽지 않고 끝 구간과 앞 구간을 이어 붙인다.
		const float Len = Montage.GetPlayLength();
		FTransform Delta = Montage.ExtractRootMotionFromTrackRange(From, Len, FAnimExtractContext());
		const FTransform Head = Montage.ExtractRootMotionFromTrackRange(0.0f, To, FAnimExtractContext());
		Delta.AddToTranslation(Head.GetTranslation());
		return Delta;
	}
}

bool FVBLadderMode::TryStart(IVBLadderModeHost& Host)
{
	const UVBTraversalConfig* Cfg = Host.GetConfig();
	const UVBClimbAnimSet* Clips = Host.GetClips();
	AVBCharacter* Rider = Host.GetCharacter();
	UWorld* World = Rider ? Rider->GetWorld() : nullptr;
	if (Host.IsInSurfaceMode() || !Cfg || !World)
	{
		return false;
	}

	if (!Clips || !Clips->LadderIdleMontage)
	{
		return false; // 대기 몽타주가 없으면 사다리 기능은 꺼진 상태다
	}

	const FVector Origin = Rider->GetActorLocation();
	const FVector Forward = Rider->GetActorForwardVector();
	const float RadiusSq = Cfg->LadderFindRadius * Cfg->LadderFindRadius;
	AVBLadder* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<AVBLadder> It(World); It; ++It)
	{
		AVBLadder* Ladder = *It;
		if (!Ladder)
		{
			continue;
		}
		// 축 위 가장 가까운 점까지의 거리로 잰다. 액터 원점으로 재면 긴 사다리의 위쪽이 멀어 보인다.
		const FVector Near = Ladder->GetAxisPoint(Ladder->ProjectToHeight(Origin));
		const float DistSq = static_cast<float>(FVector::DistSquared(Near, Origin));
		if (DistSq > RadiusSq || DistSq >= BestDistSq)
		{
			continue;
		}

		const FVector ToLadder = (Near - Origin).GetSafeNormal();
		if (FVector::DotProduct(Forward, ToLadder) < Cfg->LadderFacingDot)
		{
			continue;
		}

		if (!VBTraversalRules::Ladder::IsLadderStartPlausible(Origin, Near, Ladder->GetFacingNormal(), Cfg->LadderFindRadius))
		{
			continue; // 사다리 뒤편이다 - 서버 재검사와 같은 판정
		}

		Best = Ladder;
		BestDistSq = DistSq;
	}

	if (!Best)
	{
		return false;
	}

	const float Height = Best->ProjectToHeight(Rider->GetMesh() ? Rider->GetMesh()->GetComponentLocation() : Origin);
	VB_LOG(Log, "Ladder 진입: %s 높이=%.0f/%.0f", *GetNameSafe(Best), Height, Best->GetLadderHeight());
	Host.SendLadderStartRequest(Best, Height);
	return true;
}

bool FVBLadderMode::AcceptServerStart(const IVBLadderModeHost& Host, AVBLadder* Ladder, float ClientHeight, float& OutHeight) const
{
	const UVBTraversalConfig* Cfg = Host.GetConfig();
	const AVBCharacter* Rider = Host.GetCharacter();
	const TCHAR* Refusal = nullptr;
	if (!Rider || !Cfg)
	{
		Refusal = TEXT("캐릭터·설정 없음");
	}
	else if (Host.IsInAnyTraversal())
	{
		Refusal = TEXT("이미 트래버설 중");
	}
	else if (Host.IsBodyBusyForTraversal())
	{
		Refusal = TEXT("몸을 다른 동작이 쥠(상호작용·처형·래그돌·죽음)");
	}
	else
	{
		// 거리 = 클라이언트 탐색 반경 + 지연 여유, 면 = 클라이언트와 같은 판정.
		const FVector Origin = Rider->GetActorLocation();
		const FVector Near = Ladder->GetAxisPoint(Ladder->ProjectToHeight(Origin));
		if (!VBTraversalRules::Ladder::IsLadderStartPlausible(Origin, Near, Ladder->GetFacingNormal(),
			Cfg->LadderFindRadius + Cfg->LadderServerDistanceSlack))
		{
			Refusal = TEXT("너무 멀거나 사다리 뒤편");
		}
	}

	if (Refusal)
	{
		VB_LOG(Warning, "Ladder 서버 거절: %s - %s", *GetNameSafe(Ladder), Refusal);
		return false;
	}
	// 클라이언트가 보낸 높이는 믿지 않는다. 축 밖 값이 오면 캡슐이 엉뚱한 곳으로 간다.
	OutHeight = Ladder->ProjectToHeight(Ladder->GetAxisPoint(ClientHeight));
	return true;
}

void FVBLadderMode::Start(IVBLadderModeHost& Host, FVBTraversalSession& Session, AVBLadder* Ladder, float Height)
{
	AVBCharacter* Rider = Host.GetCharacter();
	UWorld* World = Rider ? Rider->GetWorld() : nullptr;
	if (!Ladder || !World)
	{
		return;
	}
	// 복구 모드를 먼저 정하고 탑승을 켠다. 탑승이 세션을 열어야 호스트 복구가 첫 줄에서 반환하지 않고 임시로 바꾼 것을 되돌린다.
	Session.SetRecoverMode(MOVE_Falling);
	Host.SetAttached(true);
	State = FVBLadderRuntime();
	State.ActiveLadder = Ladder;
	State.HeightOnAxis = Height;
	State.EnterLocation = Rider->GetActorLocation();
	State.EnterRotation = Rider->GetActorQuat();
	State.EnterTime = World->GetTimeSeconds();
	// 탑승자 캡슐만 사다리 막는 상자를 통과한다. 원래 있던 무시는 나중에 해제하지 않는다.
	if (UBoxComponent* Blocker = Ladder->GetBlocker())
	{
		UCapsuleComponent* Capsule = Rider->GetCapsuleComponent();
		if (Capsule && !Capsule->GetMoveIgnoreComponents().Contains(Blocker))
		{
			Capsule->IgnoreComponentWhenMoving(Blocker, true);
			State.IgnoredBlocker = Blocker;
		}
	}
	// SetAttached 다음이어야 한다. 안에서 적용하는 회전 모드가 탑승 플래그를 읽는다.
	Session.EnterSurface(*Rider, nullptr);
	BindTickOrder(Host);
	// 붙는 순간은 틱 순서 밖이라 두 단계를 여기서 한 번 실행한다.
	UpdateIntent(Host);
	if (Host.IsRidingLadder())
	{
		UpdatePlacement(Host, 0.0f);
	}
}

void FVBLadderMode::End(IVBLadderModeHost& Host)
{
	if (Host.IsRidingLadder())
	{
		Host.EndSessionFalling();
	}
}

void FVBLadderMode::ClearRiding(IVBLadderModeHost& Host)
{
	UnbindTickOrder();
	Host.SetAttached(false);
	State.ClearOnExit(VBTraversalMontage::FindAnim(Host.GetCharacter()), VBTraversalMontage::StopBlendTime(Host.GetClips()));
}

void FVBLadderMode::ReleaseOnRecover(IVBLadderModeHost& Host)
{
	if (UPrimitiveComponent* Blocker = State.IgnoredBlocker.Get())
	{
		const AVBCharacter* Rider = Host.GetCharacter();
		if (UCapsuleComponent* Capsule = Rider ? Rider->GetCapsuleComponent() : nullptr)
		{
			Capsule->IgnoreComponentWhenMoving(Blocker, false);
		}
	}

	State.IgnoredBlocker = nullptr;
	State.TopOutSupport = FVBLadderTopOutSupport();
}

void FVBLadderMode::SetMoveInput(IVBLadderModeHost& Host, float InputY)
{
	if (!Host.IsRidingLadder())
	{
		return;
	}

	ApplyMoveInput(InputY);
	Host.SendLadderMoveInput(State.MoveInput);
}

void FVBLadderMode::ApplyMoveInput(float InputY)
{
	State.MoveInput = FMath::Clamp(InputY, -1.0f, 1.0f);
}

void FVBLadderMode::RequestDrop(IVBLadderModeHost& Host)
{
	if (!Host.IsRidingLadder() || !Host.GetCharacter())
	{
		return;
	}

	Host.RequireFreshAutoIntent();
	Host.SendLadderDropRequest();
}

void FVBLadderMode::StartTopOut(IVBLadderModeHost& Host, const FVector& LedgePoint, const FVector& LedgeNormal)
{
	const UVBClimbAnimSet* Clips = Host.GetClips();
	if (!Host.IsRidingLadder() || !Host.GetCharacter() || !Clips || !Clips->HangClimbUpMontage)
	{
		return; // 사다리에 남는다 - 서버 확인 뒤 여기 오는 것은 복제 순서가 엇갈린 경우뿐이다
	}
	// 탑승을 정리하기 전에 요청한다. IK 노드가 손발이 떨어질 때까지 마지막 지지를 이어 사용한다.
	State.TopOutSupport.Ladder = State.ActiveLadder;
	State.TopOutSupport.bRequested = true;
	ClearRiding(Host);
	Host.PlayTopOutMontage(LedgePoint, LedgeNormal, TEXT("Ladder"));
}

void FVBLadderMode::UpdateIntent(IVBLadderModeHost& Host)
{
	if (!Host.IsRidingLadder())
	{
		return;
	}

	AVBCharacter* Rider = Host.GetCharacter();
	const UVBClimbAnimSet* Clips = Host.GetClips();
	UWorld* World = Rider ? Rider->GetWorld() : nullptr;
	if (!World || !Clips || !State.ActiveLadder.IsValid())
	{
		End(Host);
		return;
	}

	AVBLadder* Ladder = State.ActiveLadder.Get();

	// 입력을 아는 쪽(서버, 소유 클라이언트)만 축 위를 움직인다. 시뮬레이티드 프록시는 복제된 위치를 받는다.
	const bool bDrives = Rider->HasAuthority() || Rider->IsLocallyControlled();
	const float Top = ComputeTopHeight(*Ladder, *Clips);
	float Want = bDrives ? State.MoveInput : 0.0f;
	// 바닥에서 아래 입력을 유지하면 내려선다. 서버만 결정하고 모두 멀티캐스트로 종료한다.
	if (Rider->HasAuthority() && Want < -Clips->LadderMoveInputThreshold && State.HeightOnAxis <= KINDA_SMALL_NUMBER)
	{
		Host.ConfirmLadderDrop();
		return;
	}

	if (TryTopOutAtLimit(Host, *Rider, *Ladder, Want, Top))
	{
		return; // 탑승이 방금 정리됐다
	}
	// 끝에서는 그 방향 입력을 버려 제자리에서 몽타주가 재생되지 않게 한다.
	if ((Want > 0.0f && State.HeightOnAxis >= Top) || (Want < 0.0f && State.HeightOnAxis <= 0.0f))
	{
		Want = 0.0f;
	}

	const float PlayRate = VBTraversalRules::PlayRate::ResolveTraversalPlayRate(UVBLocomotionIntentComponent::IsSprintingFor(Rider), Want,
		Clips->LadderFastUpRate, Clips->LadderFastDownRate);

	UAnimMontage* Wanted = PickWantedMontage(Host, *Clips, Want);
	if (USkeletalMeshComponent* DriveMesh = Rider->GetMesh())
	{
		DriveWantedMontage(Host, *Clips, DriveMesh->GetAnimInstance(), Wanted, PlayRate);
	}

	State.Frame.Frame = GFrameCounter;
	State.Frame.bDrives = bDrives;
	State.Frame.Want = Want;
	State.Frame.PlayRate = PlayRate;
	State.Frame.Top = Top;
	State.Frame.bRootMotionAtIntent = Rider->GetMesh() && Rider->GetMesh()->IsPlayingRootMotion();
}

bool FVBLadderMode::TryTopOutAtLimit(IVBLadderModeHost& Host, const AVBCharacter& Rider, const AVBLadder& Ladder, float Want, float Top)
{
	if (const UVBTraversalConfig* Cfg = Host.GetConfig(); Cfg
		&& VBTraversalRules::Ladder::ShouldTryLadderTopOut(Rider.HasAuthority(),
		GetEntryAlpha(Host) >= 1.0f, Want, State.HeightOnAxis, Top, Cfg->ClimbTopOutMinUpInput))
	{
		const FVBTopOutResult TopOut = FindTopOut(Host);
		if (TopOut.bFound)
		{
			Host.ConfirmLadderTopOut(TopOut.LedgePoint, Ladder.GetFacingNormal().GetSafeNormal2D());
			return true;
		}

		if (State.Diag.TopOutLog.Ready(Rider.GetWorld()->GetTimeSeconds()))
		{
			VB_LOG(Log, "Ladder 꼭대기 올라서기 안 함: %s", *TopOut.Refusal);
		}
	}

	return false;
}

UAnimMontage* FVBLadderMode::PickWantedMontage(const IVBLadderModeHost& Host, const UVBClimbAnimSet& Clips, float Want) const
{
	UAnimMontage* Wanted = nullptr;
	if (Want > Clips.LadderMoveInputThreshold)
	{
		Wanted = Clips.LadderUpMontage;
	}
	else if (Want < -Clips.LadderMoveInputThreshold)
	{
		Wanted = Clips.LadderDownMontage;
	}

	if (!Wanted)
	{
		// 이미 재생 중인 대기 세트를 유지한다. 매 틱 다시 고르면 두 발 높이가 비슷할 때 깜빡인다.
		const bool bAlreadyIdle = State.ActiveMontage && IsIdleMontage(Clips, State.ActiveMontage);
		Wanted = bAlreadyIdle ? State.ActiveMontage.Get() : PickIdle(Host);
	}

	return Wanted;
}

void FVBLadderMode::DriveWantedMontage(const IVBLadderModeHost& Host, const UVBClimbAnimSet& Clips, UAnimInstance* Anim,
	UAnimMontage* Wanted, float PlayRate)
{
	// 새 오르내리기 몽타주는 발이 가로대에 닿는 위상에서 시작하고, 같은 몽타주면 멈춘 위상에서 이어 재생한다.
	float StartTime = 0.0f;
	const bool bWantedIsIdle = IsIdleMontage(Clips, Wanted);
	if (Wanted && Wanted != State.ActiveMontage && !bWantedIsIdle)
	{
		StartTime = (Wanted == State.ResumeMontage) ? State.ResumePhase : FindMontageStart(Host, Wanted, State.HeightOnAxis);
	}

	bool bStarted = false;
	State.ActiveMontage = VBTraversalMontage::DriveLoopingMontage(Anim, Wanted, State.ActiveMontage, Clips.MontageStopBlendTime, bStarted, StartTime);
	if (bStarted)
	{
		// 새 인스턴스의 실제 시작 위상을 기록해 B 가 첫 구간을 빠짐없이 소비하게 한다.
		const FAnimMontageInstance* Started = State.ActiveMontage && Anim
			? Anim->GetActiveInstanceForMontage(State.ActiveMontage) : nullptr;
		State.bMontageTracked = Started != nullptr;
		State.MontagePosition = Started ? Started->GetPosition() : 0.0f;
	}
	// 재생 속도만 바꾼다(재시작 없음). 위상이 유지된다. 대기는 루트모션이 없어 1 로 둔다.
	if (State.ActiveMontage && !bWantedIsIdle && Anim)
	{
		Anim->Montage_SetPlayRate(State.ActiveMontage, PlayRate);
	}
}

void FVBLadderMode::UpdatePlacement(IVBLadderModeHost& Host, float DeltaTime)
{
	AVBCharacter* Rider = Host.GetCharacter();
	const UVBClimbAnimSet* Clips = Host.GetClips();
	UWorld* World = Rider ? Rider->GetWorld() : nullptr;
	if (!World || !Clips || !State.ActiveLadder.IsValid())
	{
		End(Host);
		return;
	}

	const AVBLadder* Ladder = State.ActiveLadder.Get();
	const FVector Axis = Ladder->GetActorUpVector();
	const USkeletalMeshComponent* Mesh = Rider->GetMesh();
	const bool bRootMotionAtPlace = Mesh && Mesh->IsPlayingRootMotion();
	WarnIfOutOfIntentOrder(*World, bRootMotionAtPlace);

	const float ConsumedFrom = State.bMontageTracked ? State.MontagePosition : -1.0f;
	FVector RootMotionMoved = FVector::ZeroVector;
	float Along = ConsumeMontageRootMotion(VBTraversalMontage::FindAnim(Rider), Mesh, *Clips, Axis, RootMotionMoved);
	Along = AdvanceHeight(*Clips, *World, Along, DeltaTime);
	UpdateIdleSettle(*Ladder, *Clips, DeltaTime);
	if (State.Diag.StatusLog.Ready(World->GetTimeSeconds()))
	{
		VB_LOG(Log, "Ladder 상태: 높이=%.0f/%.0f 입력=%.2f 깊이=%.1f 몽타주=%s 루트모션=%.2fcm(축%.2f) 적용=%.2f",
			State.HeightOnAxis, Ladder->GetLadderHeight(), State.MoveInput, Clips->LadderDepth,
			State.ActiveMontage ? *State.ActiveMontage->GetName() : TEXT("없음"),
			RootMotionMoved.Size(), static_cast<float>(FVector::DotProduct(RootMotionMoved, Axis)), Along);
	}

	const FTransform Target = ComputeCapsuleTransform(Host, State.HeightOnAxis);
	FVector Location = Target.GetLocation();
	FQuat Rotation = Target.GetRotation();
	if (const float Alpha = GetEntryAlpha(Host); Alpha < 1.0f)
	{
		// 위치와 회전을 같은 알파로 섞는다. 회전을 안 섞으면 붙는 프레임에 각도가 꺾인다.
		Location = FMath::Lerp(State.EnterLocation, Location, Alpha);
		Rotation = FQuat::Slerp(State.EnterRotation, Rotation, Alpha).GetNormalized();
	}

	ReportVisibleJump(*Rider, *World, Location, RootMotionMoved, Axis, Along, DeltaTime);
	Rider->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	// 실제 놓인 위치를 다시 읽는다. 이 프레임의 마지막 쓰기이므로 다음 표류는 외부 요인이다.
	State.Diag.LastPlacedLocation = Rider->GetActorLocation();
	State.Diag.bPlacedOnce = true;
	LogPlacementTelemetry(Host, *World, Mesh, ConsumedFrom, Along, bRootMotionAtPlace);
}

void FVBLadderMode::WarnIfOutOfIntentOrder(const UWorld& World, bool bRootMotionAtPlace)
{
	// 여기서는 막을 수 없어 경고만 한다. 이 프레임의 손발 IK 는 배치 전 트랜스폼에서 계산된다.
	if ((State.Frame.Frame != GFrameCounter || bRootMotionAtPlace != State.Frame.bRootMotionAtIntent)
		&& State.Diag.OrderLog.Ready(World.GetTimeSeconds()))
	{
		VB_LOG(Warning, "Ladder: A 단계와 어긋난 배치 - A 프레임=%llu 지금=%llu 루트모션 A=%d B=%d (몽타주를 바꾼 것이 A 밖에 있다)",
			State.Frame.Frame, GFrameCounter, State.Frame.bRootMotionAtIntent, bRootMotionAtPlace);
	}
}

float FVBLadderMode::ConsumeMontageRootMotion(UAnimInstance* Anim, const USkeletalMeshComponent* Mesh, const UVBClimbAnimSet& Clips,
	const FVector& Axis, FVector& OutRootMotionMoved)
{
	// 이동량은 액터가 아니라 몽타주에서 읽는다. 액터는 캡슐이 벽에서 밀려날 때도 움직인다.
	float Along = 0.0f;
	if (State.ActiveMontage != nullptr && Anim && Mesh)
	{
		const float Pos = Anim->Montage_GetPosition(State.ActiveMontage);
		if (!State.bMontageTracked)
		{
			State.bMontageTracked = true; // A 가 위상을 기록하지 못했다 - 이번 프레임은 비교 대상이 없다
		}
		else
		{
			const FTransform Delta = ExtractLoopedRootMotion(*State.ActiveMontage, State.MontagePosition, Pos);
			// 루트모션은 메시 공간이라 월드로 회전한 뒤 사다리 축에 투영한다.
			OutRootMotionMoved = Mesh->GetComponentQuat().RotateVector(Delta.GetTranslation());
			Along = static_cast<float>(FVector::DotProduct(OutRootMotionMoved, Axis));
		}

		State.MontagePosition = Pos;
		if (!IsIdleMontage(Clips, State.ActiveMontage))
		{
			State.ResumeMontage = State.ActiveMontage;
			State.ResumePhase = Pos;
		}
	}
	else
	{
		State.bMontageTracked = false;
		State.MontagePosition = 0.0f;
	}

	return Along;
}

float FVBLadderMode::AdvanceHeight(const UVBClimbAnimSet& Clips, const UWorld& World, float Along, float DeltaTime)
{
	const float Want = State.Frame.Want;
	const float PlayRate = State.Frame.PlayRate;
	// 재생 중인 몽타주가 있으면 높이를 몽타주가 결정한다. 대기로 넘어가는 블렌드아웃에도 남은 이동이 있다.
	const bool bMontageDrivesHeight = State.ActiveMontage != nullptr;
	if (State.Frame.bDrives && DeltaTime > 0.0f)
	{
		if (!bMontageDrivesHeight)
		{
			if (!FMath::IsNearlyZero(Want))
			{
				// 그 방향 몽타주가 없으면 일정 속도로 민다. 이 구간만 미끄러진다.
				Along = Clips.LadderSpeed * DeltaTime * Want * PlayRate;
			}
		}
		else
		{
			// 잘렸다면 클립 말고 다른 이동이 더해진 것이다.
			const float MaxStep = Clips.LadderSpeed * DeltaTime * Clips.LadderMaxStepSpeedScale * PlayRate;
			if (FMath::Abs(Along) > MaxStep && MaxStep > KINDA_SMALL_NUMBER)
			{
				if (State.Diag.ClampLog.Ready(World.GetTimeSeconds()))
				{
					VB_LOG(Log, "Ladder: 한 틱 이동 %.1fcm 가 상한 %.1f 을 넘어 잘랐다(몽타주 전환·감김 구간일 수 있다)", Along, MaxStep);
				}

				Along = FMath::Sign(Along) * MaxStep;
			}
		}

		State.HeightOnAxis = FMath::Clamp(State.HeightOnAxis + Along, 0.0f, State.Frame.Top);
	}
	else
	{
		Along = 0.0f;
	}

	return Along;
}

void FVBLadderMode::UpdateIdleSettle(const AVBLadder& Ladder, const UVBClimbAnimSet& Clips, float DeltaTime)
{
	// 입력이 한동안 없을 때만 가장 가까운 가로대로 한 번 맞춘다. 연타 사이에는 끼어들지 않는다.
	if (!FMath::IsNearlyZero(State.Frame.Want))
	{
		State.IdleQuietTime = 0.0f;
		State.bSettling = false;
	}
	else if (DeltaTime > 0.0f)
	{
		State.IdleQuietTime += DeltaTime;
		if (!State.bSettling && State.IdleQuietTime >= Clips.LadderIdleSettleDelay)
		{
			if (const float Spacing = Ladder.GetRungSpacing(); Spacing > KINDA_SMALL_NUMBER)
			{
				const float FootNow = State.HeightOnAxis + Clips.LadderIdleFootOffset;
				const float Err = WrapToRung(FootNow - Ladder.GetRungTopOffset(), Spacing);
				State.SettleTarget = FMath::Clamp(State.HeightOnAxis - Err, 0.0f, State.Frame.Top);
				State.bSettling = true;
			}
		}
	}

	if (State.bSettling && State.Frame.bDrives && DeltaTime > 0.0f && Clips.LadderIdleSettleSpeed > 0.0f)
	{
		State.HeightOnAxis = FMath::FInterpTo(State.HeightOnAxis, State.SettleTarget, DeltaTime, Clips.LadderIdleSettleSpeed);
		if (FMath::IsNearlyEqual(State.HeightOnAxis, State.SettleTarget, 0.05f))
		{
			State.HeightOnAxis = State.SettleTarget;
			State.bSettling = false;
		}
	}
}

void FVBLadderMode::ReportVisibleJump(const AVBCharacter& Rider, const UWorld& World, const FVector& Location,
	const FVector& RootMotionMoved, const FVector& Axis, float Along, float DeltaTime)
{
	// 클립의 최고 세로 속도(약 98 cm/s)를 넘는 순간 이동은 클립 때문이 아니다.
	if (State.Diag.bPlacedOnce && DeltaTime > 0.0f)
	{
		const FVector Visible = Location - State.Diag.LastPlacedLocation;
		const float Limit = 100.0f * DeltaTime;
		if (Visible.SizeSquared() > Limit * Limit && State.Diag.JumpLog.Ready(World.GetTimeSeconds()))
		{
			const FVector Drifted = Rider.GetActorLocation() - State.Diag.LastPlacedLocation;
			VB_LOG(Warning, "Ladder 튐: dt=%.1fms 보임=%.1fcm | 클립=%.2f(축%.2f) 적용=%.2f 높이=%.1f "
				"액터표류=%.2fcm(축%.2f) 위상=%.2f 몽타주=%s 입력=%.2f",
				DeltaTime * 1000.0f, Visible.Size(), RootMotionMoved.Size(), static_cast<float>(FVector::DotProduct(RootMotionMoved, Axis)),
				Along, State.HeightOnAxis, Drifted.Size(), static_cast<float>(FVector::DotProduct(Drifted, Axis)),
				State.MontagePosition, State.ActiveMontage ? *State.ActiveMontage->GetName() : TEXT("없음"), State.MoveInput);
		}
	}
}

void FVBLadderMode::LogPlacementTelemetry(const IVBLadderModeHost& Host, const UWorld& World, const USkeletalMeshComponent* Mesh,
	float ConsumedFrom, float Along, bool bRootMotionAtPlace) const
{
	if (CVarLadderPlacementTelemetry.GetValueOnGameThread() > 0)
	{
		VB_LOG(Log, "LadderPlace frame=%llu time=%.3f montage=%s from=%.4f to=%.4f along=%.3f height=%.3f placed=(%s) mesh=(%s) rmAtIntent=%d rmAtPlace=%d settling=%d entryAlpha=%.3f",
			GFrameCounter, World.GetTimeSeconds(), *GetNameSafe(State.ActiveMontage), ConsumedFrom,
			State.ActiveMontage != nullptr ? State.MontagePosition : -1.0f, Along, State.HeightOnAxis,
			*State.Diag.LastPlacedLocation.ToString(),
			Mesh ?*Mesh->GetComponentLocation().ToString() : TEXT("none"), State.Frame.bRootMotionAtIntent, bRootMotionAtPlace,
			State.bSettling, GetEntryAlpha(Host));
	}
}

void FVBLadderMode::BindTickOrder(IVBLadderModeHost& Host)
{
	const FVBLadderTickTargets Targets = Host.GetLadderTickTargets();
	if (!Targets.Owner || !Targets.IntentTick || !Targets.PlacementTick)
	{
		VB_LOG(Warning, "Ladder: 틱 순서 재료 없음 - 손발 IK 가 놓기 전 자리에서 풀린다");
		return;
	}
	// 기록은 UnbindTickOrder 까지 유지된다. 두 번째 호출은 이미 추가한 의존을 다시 추가하지 않는다.
	TickEdges.Owner = Targets.Owner;
	TickEdges.IntentTick = Targets.IntentTick;
	TickEdges.PlacementTick = Targets.PlacementTick;
	// 없는 의존만 추가하고 추가한 것만 기록한다. 다른 곳에서 만든 의존은 해제 때 남긴다.
	const auto Own = [](FTickFunction& Dependent, UObject* Object, FTickFunction& Prerequisite, bool& bOwned)
	{
		const FTickPrerequisite Edge(Object, Prerequisite);
		if (bOwned || Dependent.GetPrerequisites().Contains(Edge))
		{
			return;
		}

		Dependent.AddPrerequisite(Object, Prerequisite);
		bOwned = Dependent.GetPrerequisites().Contains(Edge);
	};
	FTickFunction& Intent = *Targets.IntentTick;
	FTickFunction& Placement = *Targets.PlacementTick;
	if (AActor* Controller = Targets.Controller)
	{
		TickEdges.Controller = Controller;
		Own(Intent, Controller, Controller->PrimaryActorTick, TickEdges.bIntentAfterController);
	}

	UActorComponent* Movement = Targets.Movement;
	if (Movement)
	{
		TickEdges.Movement = Movement;
		Own(Movement->PrimaryComponentTick, Targets.Owner, Intent, TickEdges.bMovementAfterIntent);
		Own(Placement, Movement, Movement->PrimaryComponentTick, TickEdges.bPlacementAfterMovement);
	}

	USkeletalMeshComponent* Mesh = Targets.Mesh;
	if (Mesh)
	{
		TickEdges.Mesh = Mesh;
		Own(Mesh->PrimaryComponentTick, Targets.Owner, Placement, TickEdges.bMeshAfterPlacement);
	}

	Intent.SetTickFunctionEnable(true);
	// 막을 수 없어 경고만 한다. 입력 의존은 필수가 아니다 - 시뮬레이티드 프록시에는 컨트롤러가 없다.
	const bool bOrdered = Movement && Mesh
		&& Movement->PrimaryComponentTick.GetPrerequisites().Contains(FTickPrerequisite(Targets.Owner, Intent))
		&& Placement.GetPrerequisites().Contains(FTickPrerequisite(Movement, Movement->PrimaryComponentTick))
		&& Mesh->PrimaryComponentTick.GetPrerequisites().Contains(FTickPrerequisite(Targets.Owner, Placement));
	if (!bOrdered)
	{
		VB_LOG(Warning, "Ladder: 틱 순서(A -> CMC -> B -> 메시)를 못 걸었다 - 손발 IK 가 놓기 전 자리에서 풀린다");
	}
}

void FVBLadderMode::UnbindTickOrder()
{
	if (!TickEdges.IntentTick || !TickEdges.PlacementTick)
	{
		return;
	}

	FTickFunction& Intent = *TickEdges.IntentTick;
	FTickFunction& Placement = *TickEdges.PlacementTick;
	if (AActor* Controller = TickEdges.Controller.Get(); Controller && TickEdges.bIntentAfterController)
	{
		Intent.RemovePrerequisite(Controller, Controller->PrimaryActorTick);
	}

	if (UActorComponent* Movement = TickEdges.Movement.Get())
	{
		if (TickEdges.bMovementAfterIntent)
		{
			Movement->PrimaryComponentTick.RemovePrerequisite(TickEdges.Owner, Intent);
		}

		if (TickEdges.bPlacementAfterMovement)
		{
			Placement.RemovePrerequisite(Movement, Movement->PrimaryComponentTick);
		}
	}

	if (USkeletalMeshComponent* Mesh = TickEdges.Mesh.Get(); Mesh && TickEdges.bMeshAfterPlacement)
	{
		Mesh->PrimaryComponentTick.RemovePrerequisite(TickEdges.Owner, Placement);
	}

	Intent.SetTickFunctionEnable(false);
	TickEdges = FTickEdges();
}

UAnimMontage* FVBLadderMode::PickIdle(const IVBLadderModeHost& Host) const
{
	// 대기 세트가 둘이면 지금 어느 발이 위인지로 고른다(오르내리기는 주기마다 좌우가 바뀐다).
	const UVBClimbAnimSet* Clips = Host.GetClips();
	UAnimMontage* const Base = Clips ? Clips->LadderIdleMontage.Get() : nullptr;
	UAnimMontage* const Mirror = Clips ? Clips->LadderIdleMirrorMontage.Get() : nullptr;
	if (!Mirror)
	{
		return Base;
	}

	const AVBCharacter* Rider = Host.GetCharacter();
	const USkeletalMeshComponent* Mesh = Rider ? Rider->GetMesh() : nullptr;
	const AVBLadder* Ladder = State.ActiveLadder.Get();
	if (!Mesh || !Ladder || Mesh->GetBoneIndex(VBLimbBones::BallL) == INDEX_NONE || Mesh->GetBoneIndex(VBLimbBones::BallR) == INDEX_NONE)
	{
		return Base;
	}
	// 기본 대기는 오른발이 위다.
	const FVector Axis = Ladder->GetActorUpVector();
	const float Left = static_cast<float>(FVector::DotProduct(Mesh->GetSocketLocation(VBLimbBones::BallL), Axis));
	const float Right = static_cast<float>(FVector::DotProduct(Mesh->GetSocketLocation(VBLimbBones::BallR), Axis));
	return (Right >= Left) ? Base : Mirror;
}

float FVBLadderMode::FindMontageStart(const IVBLadderModeHost& Host, UAnimMontage* Montage, float HeightNow) const
{
	// 캐릭터 대신 클립 시작 위상을 옮겨 발을 가로대에 맞춘다(순간이동이 안 보이게).
	//   f(t0) = HeightNow + StancePhase - RungTopOffset (mod 간격), f = 누적 루트 Z.
	const AVBLadder* Ladder = State.ActiveLadder.Get();
	const UVBClimbAnimSet* Clips = Host.GetClips();
	if (!Montage || !Ladder || !Clips)
	{
		return 0.0f;
	}

	const float Spacing = Ladder->GetRungSpacing();
	const float Len = Montage->GetPlayLength();
	if (Spacing <= KINDA_SMALL_NUMBER || Len <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const float Target = HeightNow + Clips->LadderStancePhase - Ladder->GetRungTopOffset();
	// 128 칸: 최악 오차가 루트모션 반 칸(1cm 미만)이라 IK 보정이 흡수한다.
	const int32 Steps = 128;
	float Acc = 0.0f;
	float PrevT = 0.0f;
	float BestT = 0.0f;
	float BestErr = MAX_FLT;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = Len * static_cast<float>(i) / static_cast<float>(Steps);
		if (i > 0)
		{
			Acc += static_cast<float>(Montage->ExtractRootMotionFromTrackRange(PrevT, T, FAnimExtractContext()).GetTranslation().Z);
		}

		PrevT = T;
		const float Err = FMath::Abs(WrapToRung(Acc - Target, Spacing));
		if (Err < BestErr)
		{
			BestErr = Err;
			BestT = T;
		}
	}

	return BestT;
}

FTransform FVBLadderMode::ComputeCapsuleTransform(const IVBLadderModeHost& Host, float Height) const
{
	// 사다리를 바라본다(-법선). 루트는 축에서 바깥으로 LadderDepth 만큼 떨어진다.
	const AVBCharacter* Rider = Host.GetCharacter();
	const AVBLadder* Ladder = State.ActiveLadder.Get();
	const UVBClimbAnimSet* Clips = Host.GetClips();
	if (!Ladder || !Clips)
	{
		return Rider ? Rider->GetActorTransform() : FTransform::Identity;
	}

	const FVector Normal = Ladder->GetFacingNormal();
	const FRotator Facing = FRotationMatrix::MakeFromX(-Normal).Rotator();
	const FVector RootWorld = Ladder->GetAxisPoint(Height) + Normal * Clips->LadderDepth;
	const FVector MeshRel = Rider && Rider->GetMesh() ? Rider->GetMesh()->GetRelativeLocation() : FVector::ZeroVector;
	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

float FVBLadderMode::GetEntryAlpha(const IVBLadderModeHost& Host) const
{
	const UVBClimbAnimSet* Clips = Host.GetClips();
	const AVBCharacter* Rider = Host.GetCharacter();
	const UWorld* World = Rider ? Rider->GetWorld() : nullptr;
	const float BlendTime = Clips ? Clips->LadderEnterBlendTime : 0.0f;
	if (BlendTime <= 0.0f || !World)
	{
		return 1.0f;
	}

	return FMath::Clamp(static_cast<float>((World->GetTimeSeconds() - State.EnterTime) / BlendTime), 0.0f, 1.0f);
}

FVBTopOutResult FVBLadderMode::FindTopOut(const IVBLadderModeHost& Host) const
{
	const UVBTraversalConfig* Cfg = Host.GetConfig();
	const AVBLadder* Ladder = State.ActiveLadder.Get();
	if (!Cfg || !Ladder)
	{
		FVBTopOutResult Missing;
		Missing.Refusal = TEXT("재료 없음(설정·사다리)");
		return Missing;
	}
	// 모서리 = 축의 꼭대기. 사다리는 질의에서 빼지 않는다(꼭대기보다 낮은 바닥은 막는 상자에 걸쳐 거절이 맞다).
	const FVector Top = Ladder->GetAxisPoint(Ladder->GetLadderHeight());
	const FFloatInterval Band = VBTraversalRules::Ladder::ComputeLadderTopOutBand(static_cast<float>(Top.Z), Cfg->LedgeProbeSkin,
		Cfg->LadderTopOutFloorTolerance);
	return Host.FindTopOutStand(Top, Ladder->GetFacingNormal(), Band);
}
