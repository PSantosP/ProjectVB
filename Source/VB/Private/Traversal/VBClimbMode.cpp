// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBClimbMode.h"

#include "Animation/VBLimbBones.h"
#include "Traversal/VBTraversalComponent.h"
#include "Traversal/VBTraversalDiagnostics.h"
#include "Traversal/VBTraversalMontage.h"
#include "Traversal/VBTraversalRules.h"
#include "Traversal/VBTraversalSession.h"
#include "Data/VBTraversalConfig.h"
#include "Data/VBClimbAnimSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "VB/VB.h"

namespace
{
	// 벽에 붙은 캐릭터의 오른쪽(벽 평면) = 법선 x 위. 벽에 붙으면 앞이 -법선이기 때문이다.
	FVector ClimbPlaneRight(const FVector& SurfaceNormal)
	{
		return FVector::CrossProduct(SurfaceNormal, FVector::UpVector).GetSafeNormal();
	}
}

bool FVBClimbMode::TraceSurface(const UVBTraversalComponent& Owner, const FVector& Around, const FVector& Normal, FHitResult& OutHit) const
{
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	const AVBCharacter* Character = Owner.GetCharacter();
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Cfg || !World)
	{
		return false;
	}
	// 면 바깥에서 시작해 안쪽으로 쏜다. 면 위의 점에서 시작하면 그 면을 못 맞히는 수가 있다.
	const FVector Start = Around + Normal * Cfg->ClimbProbeOutset;
	const FVector End = Around - Normal * (Cfg->ClimbProbeDepth - Cfg->ClimbProbeOutset);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbSurface), false, Character);
	if (!World->LineTraceSingleByChannel(OutHit, Start, End, Cfg->ClimbTraceChannel.GetValue(), Params))
	{
		return false;
	}
	// 충분히 서 있는 면만. 바닥(법선 Z=1)에 붙어 기어가는 것을 막는다.
	return FMath::Abs(OutHit.ImpactNormal.Z) <= Cfg->ClimbMaxSurfaceTiltZ;
}

bool FVBClimbMode::HasFootprintAt(const UVBTraversalComponent& Owner, const FVector& SurfacePoint, const FVector& Normal, const float Side) const
{
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	// 폭을 모르면 있다고 답하지 않는다. 그 답이 곧 손발이 허공에 남는 결함이다.
	if (!Cfg || !Clips || FMath::IsNearlyZero(Side))
	{
		return false;
	}

	const FVector Reach = SurfacePoint + ClimbPlaneRight(Normal) *(FMath::Sign(Side) * Clips->ClimbLimbHalfSpan);
	FHitResult Unused;
	return TraceSurface(Owner, Reach, Normal, Unused)
		&& TraceSurface(Owner, Reach + FVector::UpVector * Cfg->ClimbBodyProbeRise, Normal, Unused);
}

bool FVBClimbMode::TryStart(UVBTraversalComponent& Owner)
{
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	const AVBCharacter* Character = Owner.GetCharacter();
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (Owner.IsHanging() || Owner.IsClimbing() || !Cfg || !World)
	{
		return false;
	}
	// 블렌드스페이스가 없으면 자유 등반은 꺼진 것이다 - 없는 것은 깨진 것이 아니다.
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Clips || !Clips->ClimbBlendSpace)
	{
		return false;
	}

	const FVector Forward = Character->GetActorForwardVector();
	const FVector Origin = Character->GetActorLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbEnter), false, Character);
	// 못 붙은 이유를 한 줄로 남긴다(0.5초에 한 번).
	const bool bLogThis = LongLivedDiag.ProbeLog.Ready(World->GetTimeSeconds());
	if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + Forward * Cfg->ClimbProbeDistance,
		Cfg->ClimbTraceChannel.GetValue(), Params))
	{
		if (bLogThis)
		{
			// 같은 선을 Visibility 로도 쏴 본다. 거기서 맞으면 「벽은 있는데 등반 표시가 없다」로 갈린다.
			FHitResult Vis;
			const bool bAnyWall = World->LineTraceSingleByChannel(Vis, Origin,
				Origin + Forward * Cfg->ClimbProbeDistance, Cfg->TraceChannel.GetValue(), Params);
			VB_LOG(Log, "Climb 진입 실패: 등반 면 없음(%.0fcm 앞까지). 같은 선에 일반 벽은 %s%s",
				Cfg->ClimbProbeDistance, bAnyWall ? TEXT("있다 -> 그 면에 VBClimbable 콜리전 프로필이 안 걸렸다: ") : TEXT("없다(너무 멀거나 다른 곳을 본다)"),
				bAnyWall ? *GetNameSafe(Vis.GetComponent()) : TEXT(""));
		}

		return false;
	}

	if (FMath::Abs(Hit.ImpactNormal.Z) > Cfg->ClimbMaxSurfaceTiltZ)
	{
		if (bLogThis)
		{
			VB_LOG(Log, "Climb 진입 실패: 면이 너무 누웠다(법선 Z %.2f, 상한 %.2f) - %s",
				Hit.ImpactNormal.Z, Cfg->ClimbMaxSurfaceTiltZ, *GetNameSafe(Hit.GetComponent()));
		}

		return false;
	}
	// 붙는 점은 root 높이다. 맞은 점(캡슐 중심)을 쓰면 캐릭터가 떠오른다.
	FVector SurfacePoint = Hit.ImpactPoint;
	if (const USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		SurfacePoint.Z = Mesh->GetComponentLocation().Z;
	}

	FHitResult AtRoot;
	if (TraceSurface(Owner, SurfacePoint, Hit.ImpactNormal, AtRoot))
	{
		SurfacePoint = AtRoot.ImpactPoint;
	}
	// 양옆 모두 손발 자리에 면이 있어야 붙는다. 모서리에서 붙으면 시작부터 한쪽 손발이 허공이다.
	for (const float Side : { -1.0f, 1.0f })
	{
		if (!HasFootprintAt(Owner, SurfacePoint, Hit.ImpactNormal, Side))
		{
			if (bLogThis)
			{
				VB_LOG(Log, "Climb 진입 실패: %s쪽 손발 자리(%.0fcm 옆)에 면이 없다 - 벽 모서리이거나 손발 폭보다 좁은 면 - %s",
					Side < 0.0f ? TEXT("왼") : TEXT("오른"), Clips->ClimbLimbHalfSpan, *GetNameSafe(Hit.GetComponent()));
			}

			return false;
		}
	}

	VB_LOG(Log, "Climb 진입: 면=%s 법선=(%.2f,%.2f,%.2f)", *GetNameSafe(Hit.GetComponent()),
		Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z);
	Owner.SendClimbStart(SurfacePoint, Hit.ImpactNormal, Hit.GetComponent());
	return true;
}

void FVBClimbMode::Start(UVBTraversalComponent& Owner, FVBTraversalSession& Session, const FVector& SurfacePoint, const FVector& SurfaceNormal,
	UPrimitiveComponent* Surface)
{
	AVBCharacter* Character = Owner.GetCharacter();
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// 매달림과 같이 트래버설 중으로 표시한다. 없으면 복구가 아무것도 되돌리지 않는다.
	// 새 등반: 지난 상태와 진단을 비운다(첫 틱은 비교할 직전 위치가 없다).
	State = FVBClimbRuntime();
	// 자세 기하가 없으면 벽에서 캐릭터를 얼마나 띄울지 모른다 - 붙지 않고 경고한다.
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Clips)
	{
		VB_LOG(Error, "StartClimb: ClimbAnimSet 이 비었다 - 등반을 시작하지 않는다(BP 의 TraversalComponent 에 배선)");
		return;
	}

	Owner.Attach(EVBTraversalSurface::Climb);
	State.SurfacePoint = SurfacePoint;
	State.SurfaceNormal = SurfaceNormal;
	State.Depth = Clips->ClimbWallDepthIdle;
	State.EnterLocation = Character->GetActorLocation();
	State.EnterRotation = Character->GetActorQuat();
	State.EnterTime = World->GetTimeSeconds();
	Session.EnterSurface(*Character, Surface);
	Update(Owner, 0.0f);
}

void FVBClimbMode::SetMoveInput(UVBTraversalComponent& Owner, const FVector2D& Input)
{
	State.MoveInput = Input;
	// 서버가 위치를 굴리므로 소유 클라는 축 값을 보낸다. 매 변경마다라 Unreliable.
	if (Owner.IsOwningClient())
	{
		Owner.ServerSetClimbMoveInput(Input);
	}
}

void FVBClimbMode::ApplyMoveInput(const FVector2D& Input)
{
	State.MoveInput = FVector2D(FMath::Clamp(Input.X, -1.0f, 1.0f), FMath::Clamp(Input.Y, -1.0f, 1.0f));
}

void FVBClimbMode::RequestDrop(UVBTraversalComponent& Owner)
{
	if (!Owner.IsClimbing() || !Owner.GetCharacter())
	{
		return;
	}

	Owner.RequireFreshAutoIntent(); // 매달림 놓기와 같은 이유 - 키를 누른 채 놓으면 같은 벽에 다시 붙는다
	Owner.SendClimbDrop();
}

void FVBClimbMode::EndByDrop(UVBTraversalComponent& Owner)
{
	if (!Owner.IsClimbing())
	{
		return;
	}
	// 뒷정리는 전부 복구가 맡는다 - 복구의 홈은 하나다.
	Owner.RecoverByFalling();
}

void FVBClimbMode::ClearOnExit(UVBTraversalComponent& Owner)
{
	Owner.Detach();
	State.ClearOnExit(VBTraversalMontage::FindAnim(Owner.GetCharacter()), VBTraversalMontage::StopBlendTime(Owner.GetClips()));
}

FVBTopOutResult FVBClimbMode::FindTopOut(const UVBTraversalComponent& Owner) const
{
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	if (!Cfg)
	{
		FVBTopOutResult Missing;
		Missing.Refusal = TEXT("재료 없음(설정)");
		return Missing;
	}
	// 찾는 높이 구간 = 위로 갈 때 본 두 탐침 사이. 아래 탐침(root+Ahead)은 면을 맞혔고 위 탐침(root+Ahead+Rise)은 비었다.
	const float MinZ = State.SurfacePoint.Z + Cfg->ClimbEdgeProbeAhead;
	return Owner.FindTopOutStand(State.SurfacePoint, State.SurfaceNormal, FFloatInterval(MinZ, MinZ + Cfg->ClimbBodyProbeRise));
}

void FVBClimbMode::StartTopOut(UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal)
{
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Owner.IsClimbing() || !Owner.GetCharacter() || !Clips || !Clips->HangClimbUpMontage)
	{
		return; // 벽에 그대로 붙어 있는다 - 서버가 확인한 뒤라 여기 오는 것은 복제 순서가 엇갈린 경우뿐이다
	}
	// 등반 흔적만 걷고 Exiting 으로 간다. 세션은 몽타주 끝의 복구까지 유지 - 매달림 올라서기와 같은 계약.
	ClearOnExit(Owner);
	Owner.PlayTopOutMontage(LedgePoint, LedgeNormal, TEXT("Climb"));
}

void FVBClimbMode::Update(UVBTraversalComponent& Owner, float DeltaTime)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Character || !Character->GetWorld() || !Clips)
	{
		EndByDrop(Owner);
		return;
	}

	// 이 틱에 보이는 자리가 움직인 양을 잴 기준값(튐은 한 프레임에 생긴다).
	FTickMotion Motion;
	Motion.SurfaceBefore = State.SurfacePoint;
	Motion.DepthBefore = State.Depth;

	// 입력을 아는 쪽만 면 위를 움직인다. 시뮬 프록시는 복제된 위치를 받는다.
	const bool bDrives = Character->HasAuthority() || Character->IsLocallyControlled();
	if (bDrives && DeltaTime > 0.0f)
	{
		// 벽 평면의 두 축. 서 있는 평면만 받으므로 위 축이 곧 월드 위다(기울어진 면은 미지원).
		const FVector PlaneRight = ClimbPlaneRight(State.SurfaceNormal);
		FVector2D EffectiveInput = State.MoveInput;
		if (FilterInput(Owner, PlaneRight, EffectiveInput))
		{
			return; // 등반 상태가 방금 걷혔다 - 이 틱의 배치, IK 를 더 돌리지 않는다
		}

		TickMontage(Owner, EffectiveInput);
		MoveAlongSurface(Owner, EffectiveInput, PlaneRight, DeltaTime, Motion);
	}

	// 대기 자세가 이동 자세보다 벽에 깊어 한 값으로 두면 한쪽이 어긋난다.
	const bool bMoving = State.MoveInput.SizeSquared() > Clips->ClimbMoveInputSizeSquaredMin;
	const float TargetDepth = bMoving ? Clips->ClimbWallDepthMoving : Clips->ClimbWallDepthIdle;
	State.Depth = FMath::FInterpConstantTo(State.Depth, TargetDepth, DeltaTime, Clips->ClimbDepthInterpSpeed);

	// 말단을 벽에 붙인다. 배치가 몸통만 맞추므로 손발 접지는 여기가 맡는다.
	// 전환 블렌드 동안에도 가중치는 유지하고 판정만 쉰다. 가중치를 내리면 말단이 튕겨 떨린다.
	UpdateIKTargets(Owner, DeltaTime);
	if (VBTraversalDiagnostics::IsStatusTelemetryEnabled())
	{
		TrackLimbRate(Owner, DeltaTime);
		LogStatus(Owner);
	}

	PlaceCapsule(Owner, DeltaTime, Motion);
}

bool FVBClimbMode::FilterInput(UVBTraversalComponent& Owner, const FVector& PlaneRight, FVector2D& InOutInput)
{
	// 방향에 맞는 몽타주를 걸고 이동은 루트모션이 맡는다(고정 속도로 밀면 캐릭터만 끌려가 미끄러진다).
	// 가려는 방향에 벽이 있나를 걸기 전에 본다(root 와 그 위 ClimbBodyProbeRise). 없으면 위 성분만 지운다.
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	if (!Cfg)
	{
		return false;
	}

	const FVector2D Want(FMath::Clamp(State.MoveInput.X, -1.0f, 1.0f), FMath::Clamp(State.MoveInput.Y, -1.0f, 1.0f));
	if (Want.IsNearlyZero())
	{
		return false;
	}

	const FVector PlaneUp = FVector::UpVector;
	const float Ahead = Cfg->ClimbEdgeProbeAhead;
	const FVector Look = State.SurfacePoint + PlaneRight * (Want.X * Ahead) + PlaneUp * (Want.Y * Ahead);
	FHitResult Probe;
	if (!TraceSurface(Owner, Look, State.SurfaceNormal, Probe))
	{
		InOutInput = FVector2D::ZeroVector;
	}
	else if (Want.Y > 0.0f && !TraceSurface(Owner, Look + PlaneUp * Cfg->ClimbBodyProbeRise, State.SurfaceNormal, Probe))
	{
		InOutInput.Y = 0.0f;
		// 꼭대기다. 위로 밀면 서버가 윗면을 찾아 올라선다(클라는 좌표를 안 보낸다). 못 찾으면 벽에 남는다.
		if (Owner.GetCharacter()->HasAuthority() && Want.Y >= Cfg->ClimbTopOutMinUpInput && TryTopOut(Owner))
		{
			return true;
		}
	}
	// 옆으로 갈 때는 가는 쪽 손발 자리까지 본다. 옆 성분만 지운다 - 옆 끝에서도 위아래는 간다.
	if (!FMath::IsNearlyZero(InOutInput.X) && !HasFootprintAt(Owner, Look, State.SurfaceNormal, Want.X))
	{
		InOutInput.X = 0.0f;
	}

	return false;
}

bool FVBClimbMode::TryTopOut(UVBTraversalComponent& Owner)
{
	const FVBTopOutResult TopOut = FindTopOut(Owner);
	if (TopOut.bFound)
	{
		Owner.MulticastClimbTopOut(TopOut.LedgePoint, State.SurfaceNormal.GetSafeNormal2D());
		return true;
	}

	const UWorld* World = Owner.GetCharacter()->GetWorld();
	if (State.Diag.TopOutLog.Ready(World->GetTimeSeconds()))
	{
		VB_LOG(Log, "Climb 꼭대기 올라서기 안 함: %s", *TopOut.Refusal);
	}

	return false;
}

void FVBClimbMode::MoveAlongSurface(const UVBTraversalComponent& Owner, const FVector2D& EffectiveInput, const FVector& PlaneRight,
	float DeltaTime, FTickMotion& Motion)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	const FVector PlaneUp = FVector::UpVector;
	const FVector2D MontageDir = State.ActiveMontage ? State.ActiveDir : FVector2D::ZeroVector;

	// 이 틱에 루트모션이 캐릭터를 얼마나 옮겼나. 내가 직전에 놓은 자리와 지금 자리의 차이가 그것이다.
	if (State.bPlacedOnce)
	{
		Motion.RootMotionMoved = Character->GetActorLocation() - State.LastPlacedLocation;
	}

	const FVector Moved = ClampStep(Owner, Motion.RootMotionMoved, DeltaTime);
	// 루트모션을 그 몽타주가 맡은 방향에만 투영한다. 직각 흔들림은 도중에 끊으면 면 좌표에 쌓인다.
	// 벽 쪽 성분도 버린다. 깊이는 배치가, 접지는 IK 가 맡는다.
	float AlongRight = 0.0f;
	float AlongUp = 0.0f;
	if (MontageDir.IsNearlyZero())
	{
		// 맡을 몽타주가 없으면(MontageDir 0 한 술어로만 판단) 질주 배속을 받아 일정 속도로 민다.
		const float Step = Clips->ClimbSpeed * DeltaTime * VBTraversalRules::PlayRate::ResolveTraversalPlayRate(
			UVBLocomotionIntentComponent::IsSprintingFor(Character), static_cast<float>(EffectiveInput.Size()),
			Clips->ClimbFastRate, Clips->ClimbFastRate);
		AlongRight = FMath::Clamp(EffectiveInput.X, -1.0f, 1.0f) * Step;
		AlongUp = FMath::Clamp(EffectiveInput.Y, -1.0f, 1.0f) * Step;
	}
	else
	{
		const FVector AskedDir = (PlaneRight * MontageDir.X + PlaneUp * MontageDir.Y).GetSafeNormal();
		const float Progress = static_cast<float>(FVector::DotProduct(Moved, AskedDir));
		AlongRight = Progress * MontageDir.X;
		AlongUp = Progress * MontageDir.Y;
	}

	Motion.AppliedRight = AlongRight;
	Motion.AppliedUp = AlongUp;
	const FVector Desired = State.SurfacePoint + PlaneRight * AlongRight + PlaneUp * AlongUp;

	// 가려는 자리에서 면을 다시 찾는다. 못 찾으면 가장자리라 안 간다.
	FHitResult Hit;
	if (TraceSurface(Owner, Desired, State.SurfaceNormal, Hit))
	{
		State.SurfacePoint = Hit.ImpactPoint;
		State.SurfaceNormal = Hit.ImpactNormal;
	}

	// 축도 갈 수 있는 입력을 본다. 원래 입력을 넣으면 끝에서 제자리걸음이 된다.
	const FVector2D AxisTarget(FMath::Clamp(EffectiveInput.X, -1.0f, 1.0f), FMath::Clamp(EffectiveInput.Y, -1.0f, 1.0f));
	State.MoveAxis = FMath::Vector2DInterpTo(State.MoveAxis, AxisTarget, DeltaTime, Clips->ClimbMoveAxisInterpSpeed);
}

FVector FVBClimbMode::ClampStep(const UVBTraversalComponent& Owner, const FVector& RootMotionMoved, float DeltaTime)
{
	FVector Moved = RootMotionMoved;
	// 한 틱 이동 상한(질주 배속 반영). 잘리면 경고 없이 무시하지 않고 로그를 남긴다.
	// 몽타주 전환 순간이동 가설을 확인하는 자리다. 로그가 안 뜨면 원인은 다른 데 있다.
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	const float MaxStep = Clips->ClimbSpeed * DeltaTime * Clips->ClimbMaxStepSpeedScale * (State.ActiveMontage ? State.PlayRate : 1.0f);
	if (Moved.SizeSquared() > MaxStep * MaxStep && MaxStep > KINDA_SMALL_NUMBER)
	{
		const UWorld* World = Owner.GetCharacter()->GetWorld();
		if (State.Diag.ClampLog.Ready(World->GetTimeSeconds()))
		{
			VB_LOG(Log, "Climb: 한 틱 이동 %.0fcm 가 상한 %.0f 을 넘어 잘랐다(몽타주 전환 구간일 수 있다)",
				Moved.Size(), MaxStep);
		}

		Moved = Moved.GetSafeNormal() * MaxStep;
	}

	return Moved;
}

void FVBClimbMode::TrackLimbRate(const UVBTraversalComponent& Owner, float DeltaTime)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	// 말단의 액터 기준 속도. 판정이 쉬는 전환 구간에도 늘 잰다.
	if (const USkeletalMeshComponent* LimbMesh = Character->GetMesh())
	{
		const FTransform LimbActorXform = Character->GetActorTransform();
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector Rel = LimbActorXform.InverseTransformPosition(LimbMesh->GetSocketLocation(VBLimbBones::Ends[i]));
			if (State.Diag.bLimbRatePrimed && DeltaTime > 0.0f)
			{
				State.Diag.MaxLimbRate = FMath::Max(State.Diag.MaxLimbRate,
					static_cast<float>((Rel - State.Diag.PrevLimbRel[i]).Size()) / DeltaTime);
			}

			State.Diag.PrevLimbRel[i] = Rel;
		}

		State.Diag.bLimbRatePrimed = true;
	}
}

void FVBClimbMode::LogStatus(const UVBTraversalComponent& Owner)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	// 1초에 한 줄 진단: 면을 못 찾았는지, 자리가 틀렸는지 가른다.
	if (State.Diag.StatusLog.Ready(Character->GetWorld()->GetTimeSeconds()))
	{
		FString Parts;
		if (const USkeletalMeshComponent* M = Character->GetMesh())
		{
			// 각 말단이 벽 평면에서 얼마나 바깥인가. 양수=벽 바깥, 음수=벽 안쪽.
			auto Signed = [&](const FName Bone) -> float
			{
				return static_cast<float>(FVector::DotProduct(M->GetSocketLocation(Bone) - State.SurfacePoint,
					State.SurfaceNormal));
			};
			// root 뼈의 메시 원점 기준 위치(세 성분). 메시가 캡슐을 따라오는가를 본다.
			// 등반 클립은 bForceRootLock 이라 0 이어야 한다. 0 에서 크게 벗어나면 결함이다.
			const FVector RootRel = M->GetBoneTransform(0, FTransform::Identity).GetLocation();
			// 발은 벽에 닿는 발볼을 찍는다(IK 대상도 그쪽).
			Parts = FString::Printf(TEXT("손L%+.0f 손R%+.0f 발볼L%+.0f 발볼R%+.0f root=(%.0f,%.0f,%.0f)"),
				Signed(VBLimbBones::HandL), Signed(VBLimbBones::HandR), Signed(VBLimbBones::BallL), Signed(VBLimbBones::BallR),
				RootRel.X, RootRel.Y, RootRel.Z);
		}
		// 접지는 벽면에서 몇 cm 인지로 찍는다. 양수=바깥, 음수=벽 안쪽. IK 가 듣는지 이 숫자가 말한다.
		VB_LOG(Log, "Climb 상태: 면(%.0f,%.0f,%.0f) 입력(%.2f,%.2f) 축(%.2f,%.2f) 깊이=%.1f "
			"몽타주=%s 최대(몸%.0f 말단%.0f) IK세기(손%.2f/%.2f 발%.2f/%.2f) | %s",
			State.SurfacePoint.X, State.SurfacePoint.Y, State.SurfacePoint.Z,
			State.MoveInput.X, State.MoveInput.Y, State.MoveAxis.X, State.MoveAxis.Y, State.Depth,
			State.ActiveMontage ? *State.ActiveMontage->GetName() : TEXT("없음"),
			State.Diag.MaxBodyRate, State.Diag.MaxLimbRate,
			State.IKTargets.HandWeightL, State.IKTargets.HandWeightR,
			State.IKTargets.FootLWeight, State.IKTargets.FootRWeight,
			*Parts);
		// 이 줄이 그 1초를 대표하므로 여기서 다시 센다. 안 비우면 최대값이 영원히 안 내려간다.
		State.Diag.MaxBodyRate = 0.0f;
		State.Diag.MaxLimbRate = 0.0f;
	}
}

void FVBClimbMode::PlaceCapsule(const UVBTraversalComponent& Owner, float DeltaTime, const FTickMotion& Motion)
{
	AVBCharacter* Character = Owner.GetCharacter();
	const FTransform Target = ComputeCapsuleTransform(Owner, State.SurfacePoint, State.SurfaceNormal);
	FVector Location = Target.GetLocation();
	FQuat Rotation = Target.GetRotation();
	// 진입 보간이 이 틱에 캐릭터를 끈 양. 튐 진단에 이름 붙은 항으로 넣는다.
	FVector EnterPull = FVector::ZeroVector;
	const float BlendTime = Owner.GetClips()->ClimbEnterBlendTime;
	if (BlendTime > 0.0f)
	{
		// 위치와 회전을 같은 알파로 섞는다. 회전을 안 섞으면 붙잡는 프레임에 각도가 꺾인다.
		const float Alpha = FMath::Clamp(
			static_cast<float>((Character->GetWorld()->GetTimeSeconds() - State.EnterTime) / BlendTime), 0.0f, 1.0f);
		const FVector Unblended = Location;
		Location = FMath::Lerp(State.EnterLocation, Location, Alpha);
		Rotation = FQuat::Slerp(State.EnterRotation, Rotation, Alpha).GetNormalized();
		EnterPull = Location - Unblended;
	}

	ReportJump(Owner, Location, EnterPull, DeltaTime, Motion);
	Character->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	State.LastPlacedLocation = Character->GetActorLocation();
	State.bPlacedOnce = true;
}

void FVBClimbMode::ReportJump(const UVBTraversalComponent& Owner, const FVector& Location, const FVector& EnterPull, float DeltaTime,
	const FTickMotion& Motion)
{
	// 이 틱에 보이는 자리가 뛴 양. 1초 진단으로는 못 보므로 프레임 단위로 잡는다.
	// 문턱 200 cm/s = 클립 root 최고 세로 속도(119)의 약 1.7배.
	if (State.bPlacedOnce && DeltaTime > 0.0f)
	{
		const FVector Visible = Location - State.LastPlacedLocation;
		const float Limit = 200.0f * DeltaTime;
		const UWorld* World = Owner.GetCharacter()->GetWorld();
		State.Diag.MaxBodyRate = FMath::Max(State.Diag.MaxBodyRate, static_cast<float>(Visible.Size()) / DeltaTime);
		if (Visible.SizeSquared() > Limit * Limit && State.Diag.JumpLog.Ready(World->GetTimeSeconds()))
		{
			const FVector RightNow = ClimbPlaneRight(State.SurfaceNormal);
			const FVector SurfaceDelta = State.SurfacePoint - Motion.SurfaceBefore;
			VB_LOG(Warning, "Climb 튐: dt=%.1fms 보임=%.1fcm | 면=%.1f(가로%.1f 위%.1f) 깊이=%+.1f 진입보간=%.1f "
				"루트모션=%.1f(가로%.1f 위%.1f 법선%.1f) 적용(가로%.1f 위%.1f) 몽타주=%s 입력(%.2f,%.2f)",
				DeltaTime * 1000.0f, Visible.Size(),
				SurfaceDelta.Size(),
				static_cast<float>(FVector::DotProduct(SurfaceDelta, RightNow)),
				static_cast<float>(SurfaceDelta.Z),
				State.Depth - Motion.DepthBefore,
				EnterPull.Size(),
				Motion.RootMotionMoved.Size(),
				static_cast<float>(FVector::DotProduct(Motion.RootMotionMoved, RightNow)),
				static_cast<float>(Motion.RootMotionMoved.Z),
				static_cast<float>(FVector::DotProduct(Motion.RootMotionMoved, State.SurfaceNormal)),
				Motion.AppliedRight, Motion.AppliedUp,
				State.ActiveMontage ? *State.ActiveMontage->GetName() : TEXT("없음"),
				State.MoveInput.X, State.MoveInput.Y);
		}
	}
}

FTransform FVBClimbMode::ComputeCapsuleTransform(const UVBTraversalComponent& Owner, const FVector& SurfacePoint,
	const FVector& SurfaceNormal) const
{
	// 얼굴은 벽 쪽(-법선). root 는 면에서 바깥으로 State.Depth(높이는 입력이 정한다).
	const FRotator Facing = FRotationMatrix::MakeFromX(-SurfaceNormal).Rotator();
	const FVector RootWorld = SurfacePoint + SurfaceNormal * State.Depth;
	const AVBCharacter* Character = Owner.GetCharacter();
	FVector MeshRel = FVector::ZeroVector;
	if (Character && Character->GetMesh())
	{
		MeshRel = Character->GetMesh()->GetRelativeLocation();
	}

	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

void FVBClimbMode::UpdateIKTargets(const UVBTraversalComponent& Owner, float DeltaTime)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Mesh || !Clips || !Cfg || !World || DeltaTime <= 0.0f)
	{
		return;
	}

	// 말단을 벽면에서 여유만큼 떨어뜨린다. 오프셋은 뼈 공간으로 클립에 더하고, 보정량은 오차만큼 적분한다.
	// 월드 좌표로 덮으면 면 안쪽 방향이 얼어붙고, 한 번에 맞추면 뼈 회전, 길이 한계에서 어긋난다.
	const float Probe = Cfg->ClimbProbeOutset;
	const float MaxCorr = Clips->ClimbIKMaxCorrection;
	const float Gain = FMath::Clamp(Clips->ClimbIKGain, 0.01f, 1.0f);
	const FVector N = State.SurfaceNormal;

	// ContactBone 은 벽에 닿는 뼈, Bone 은 IK 가 옮기는 뼈다(발은 발볼 대 발목). 보정은 둘에 같다.
	auto Solve = [&](const FName Bone, const FName ContactBone, const float Clearance,
		float& InOutCorr, float& InOutWeight, const float ConfiguredWeight, FVector& OutLocalOffset)
	{
		OutLocalOffset = FVector::ZeroVector;
		bool bHit = false;
		if (Mesh->GetBoneIndex(ContactBone) != INDEX_NONE)
		{
			const FVector Contact = Mesh->GetSocketLocation(ContactBone);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(VBClimbLimbProbe), false, Character);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Contact + N * Probe, Contact - N * Probe,
				Cfg->ClimbTraceChannel.GetValue(), Params))
			{
				bHit = true;
				// 이 말단이 면에서 떨어진 거리(목표 Clearance). 이미 넣은 보정이 포함돼 있어 오차 0 에서 멈춘다.
				const float Measured = static_cast<float>(FVector::DotProduct(Contact - Hit.ImpactPoint, N));
				InOutCorr = FMath::Clamp(InOutCorr + (Clearance - Measured) * Gain, -MaxCorr, MaxCorr);
			}
		}

		if (!bHit)
		{
			// 면이 없다 = 허공으로 뻗는 중. 보정을 녹여 클립에 돌려준다.
			InOutCorr = FMath::FInterpTo(InOutCorr, 0.0f, DeltaTime, Clips->ClimbIKCorrectionFadeSpeed);
		}

		InOutWeight = FMath::FInterpTo(InOutWeight, bHit ? ConfiguredWeight : 0.0f, DeltaTime,
			Clips->ClimbIKWeightInterpSpeed);
		// 월드 방향을 그 뼈 공간으로 되돌린다. 위치가 아니라 방향만 옮기면 되므로 회전만 되돌린다.
		OutLocalOffset = Mesh->GetSocketTransform(Bone).InverseTransformVector(N * InOutCorr);
	};

	Solve(VBLimbBones::HandL, VBLimbBones::HandL, Clips->ClimbLimbWallClearance,
		State.CorrHandL, State.WeightHandL, Clips->ClimbHandIKWeight, State.IKTargets.HandL);
	Solve(VBLimbBones::HandR, VBLimbBones::HandR, Clips->ClimbLimbWallClearance,
		State.CorrHandR, State.WeightHandR, Clips->ClimbHandIKWeight, State.IKTargets.HandR);
	Solve(VBLimbBones::FootL, VBLimbBones::BallL, Clips->ClimbFootWallClearance,
		State.CorrFootL, State.WeightFootL, Clips->ClimbFootIKWeight, State.IKTargets.FootL);
	Solve(VBLimbBones::FootR, VBLimbBones::BallR, Clips->ClimbFootWallClearance,
		State.CorrFootR, State.WeightFootR, Clips->ClimbFootIKWeight, State.IKTargets.FootR);

	State.IKTargets.HandWeightL = State.WeightHandL;
	State.IKTargets.HandWeightR = State.WeightHandR;
	State.IKTargets.FootLWeight = State.WeightFootL;
	State.IKTargets.FootRWeight = State.WeightFootR;
}

UAnimMontage* FVBClimbMode::PickMontage(const UVBTraversalComponent& Owner, const FVector2D& Input, FVector2D& OutDir) const
{
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	// 몽타주와 그 몽타주가 맡은 방향은 한 자리에서 같이 낸다(문턱값을 두 곳에 두지 않게).
	OutDir = FVector2D::ZeroVector;
	if (!Clips || Input.SizeSquared() <= Clips->ClimbMoveInputSizeSquaredMin)
	{
		return nullptr; // 입력 없음 = 대기. 블렌드스페이스가 대기 자세를 든다.
	}
	// 8방향 중 가장 가까운 것.
	const float AxisMin = Clips->ClimbAxisInputThreshold;
	const bool bUp = Input.Y > AxisMin;
	const bool bDown = Input.Y < -AxisMin;
	const bool bLeft = Input.X < -AxisMin;
	const bool bRight = Input.X > AxisMin;
	const float Diag = UE_INV_SQRT_2;
	if (bUp && bLeft)
	{
		OutDir = FVector2D(-Diag, Diag);
		return Clips->ClimbMontageUpLeft;
	}

	if (bUp && bRight)
	{
		OutDir = FVector2D(Diag, Diag);
		return Clips->ClimbMontageUpRight;
	}

	if (bDown && bLeft)
	{
		OutDir = FVector2D(-Diag, -Diag);
		return Clips->ClimbMontageDownLeft;
	}

	if (bDown && bRight)
	{
		OutDir = FVector2D(Diag, -Diag);
		return Clips->ClimbMontageDownRight;
	}

	if (bUp)
	{
		OutDir = FVector2D(0.0f, 1.0f);
		return Clips->ClimbMontageUp;
	}

	if (bDown)
	{
		OutDir = FVector2D(0.0f, -1.0f);
		return Clips->ClimbMontageDown;
	}

	if (bLeft)
	{
		OutDir = FVector2D(-1.0f, 0.0f);
		return Clips->ClimbMontageLeft;
	}

	if (bRight)
	{
		OutDir = FVector2D(1.0f, 0.0f);
		return Clips->ClimbMontageRight;
	}

	return nullptr; // 여기까지 오면 입력이 문턱을 못 넘은 것이다(대각도 아니고 축도 아닌 애매한 값).
}

FVector2D FVBClimbMode::TickMontage(const UVBTraversalComponent& Owner, const FVector2D& Input)
{
	// 걸기, 루프, 끊기는 DriveLoopingMontage 가 맡는다. 여기는 8방향 선택과 전환 유지 시간만.

	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Anim)
	{
		State.ActiveMontage = nullptr;
		State.ActiveDir = FVector2D::ZeroVector;
		State.PlayRate = 1.0f;
		return FVector2D::ZeroVector;
	}

	FVector2D WantedDir = FVector2D::ZeroVector;
	UAnimMontage* Wanted = PickMontage(Owner, Input, WantedDir);
	if (!Wanted)
	{
		if (State.ActiveMontage)
		{
			Anim->Montage_Stop(VBTraversalMontage::StopBlendTime(Clips), State.ActiveMontage);
			State.ActiveMontage = nullptr;
		}

		State.ActiveDir = FVector2D::ZeroVector;
		State.PendingMontage = nullptr;
		State.PlayRate = 1.0f; // 대기 - 배속 없음(한 틱 상한도 1 배로)
		return FVector2D::ZeroVector;
	}
	// 방향이 바뀌면 잠깐 유지되는지 보고 갈아끼운다(키보드 전환 중 구획을 스쳐 손발이 튄다).
	// 대기에서 처음 거는 것에는 안 건다. 입력이 늦게 먹는 것으로 느껴진다.
	if (State.ActiveMontage && Wanted != State.ActiveMontage && Clips)
	{
		const double Now = Character->GetWorld()->GetTimeSeconds();
		if (State.PendingMontage != Wanted)
		{
			State.PendingMontage = Wanted;
			State.PendingSince = Now;
		}

		if (Now - State.PendingSince < Clips->ClimbDirectionHoldTime)
		{
			return State.ActiveDir; // 아직 안 바꾼다. 돌고 있는 것의 방향을 그대로 돌려준다
		}
	}

	State.PendingMontage = nullptr;
	bool bClimbStarted = false;
	State.ActiveMontage = VBTraversalMontage::DriveLoopingMontage(Anim, Wanted, State.ActiveMontage, VBTraversalMontage::StopBlendTime(Clips),
		bClimbStarted);
	if (bClimbStarted)
	{
		VB_LOG(Log, "Climb 몽타주: %s 입력(%.2f,%.2f)", *Wanted->GetName(), Input.X, Input.Y);
	}
	// 질주(Shift) 배속. 배속만 바꾸니 위상이 그대로다(8방향 같은 배율).
	State.PlayRate = Clips ? VBTraversalRules::PlayRate::ResolveTraversalPlayRate(UVBLocomotionIntentComponent::IsSprintingFor(Character),
		static_cast<float>(Input.Size()), Clips->ClimbFastRate, Clips->ClimbFastRate) : 1.0f;
	Anim->Montage_SetPlayRate(State.ActiveMontage, State.PlayRate);
	State.ActiveDir = WantedDir;
	return WantedDir;
}
