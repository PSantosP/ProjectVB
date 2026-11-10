// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBHangMode.h"

#include "Animation/VBLimbBones.h"
#include "Traversal/VBLevelBlock_Traversable.h"
#include "Traversal/VBTraversalComponent.h"
#include "Traversal/VBTraversalDiagnostics.h"
#include "Traversal/VBTraversalMontage.h"
#include "Traversal/VBTraversalRules.h"
#include "Traversal/VBTraversalSession.h"
#include "Data/VBTraversalConfig.h"
#include "Data/VBClimbAnimSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "VB/VB.h"

bool FVBHangMode::IsAlwaysHangBlock(const FVBTraversalCheckResult& Result)
{
	const AVBLevelBlock_Traversable* Block = Result.HitComponent
		? Cast<AVBLevelBlock_Traversable>(Result.HitComponent->GetOwner()) : nullptr;
	return Block && Block->ShouldAlwaysHang();
}

bool FVBHangMode::ShouldHangInsteadOfMantle(const UVBTraversalComponent& Owner, const FVBTraversalCheckResult& Result,
	bool bFrontFloorFound) const
{
	// 올라설 길이 없으면 매달리지 않는다 - 클립 묶음이 곧 기능 스위치다.
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Clips || !Clips->HangClimbUpMontage || !Result.FrontLedgeSpline)
	{
		return false;
	}

	if (IsAlwaysHangBlock(Result))
	{
		return true;
	}
	// 렛지 아래 MantleHeightMax(+여유) 안에 바닥이 없다 = 한 번에 올라갈 벽이 아니다.
	return !bFrontFloorFound;
}

bool FVBHangMode::TryGrabLedge(UVBTraversalComponent& Owner, FVBTraversalCheckResult& Result)
{
	// 손이 닿을 때만 잡는다. 너무 이르면 보류 - 점프 궤적이 올려 주는 다음 틱에 다시 본다.
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	const float FootZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float LedgeAboveFeet = Result.FrontLedgeLocation.Z - FootZ;
	const float Reach = Clips->HangRootBelowLedge + Clips->HangReachTolerance;
	if (LedgeAboveFeet > Reach)
	{
		VB_LOG(Verbose, "Hang 보류: 렛지-발 %.0f > 손 닿는 높이 %.0f", LedgeAboveFeet, Reach);
		return false;
	}

	Result.ActionType = EVBTraversalActionType::Hang;
	VB_LOG(Log, "Traversal AIR: type=Hang H_feet=%.0f(reach %.0f) ledge=(%.0f,%.0f,%.0f) spline=%s",
		Result.ObstacleHeight, Reach, Result.FrontLedgeLocation.X, Result.FrontLedgeLocation.Y, Result.FrontLedgeLocation.Z,
		*GetNameSafe(Result.FrontLedgeSpline));
	Owner.ServerStartHang(Result);
	return true;
}

FTransform FVBHangMode::ComputeCapsuleTransform(const UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal) const
{
	// 얼굴은 벽 쪽(-법선). root 는 렛지에서 바깥으로 HangFootDepth, 아래로 HangRootBelowLedge.
	// 캡슐 중심은 메시 상대 위치를 빼서 구한다. 메시가 어디 붙어 있든 root 가 그 자리에 온다.
	const FRotator Facing = FRotationMatrix::MakeFromX(-LedgeNormal).Rotator();
	// 높이도 깊이와 같이 상태에 따라 다르다 - 옆걸음 자세는 손이 더 내려와 렛지 윗면 아래로 잠긴다.
	const float Below = State.BelowCurrent;
	// 깊이 기준은 손이다. 벽에 박히는 발은 발 IK 가 밀어낸다(반대로 하면 팔이 모자랄 때 못 푼다).
	// 깊이는 자세마다 달라 Update 가 옮겨 놓은 현재값을 쓴다.
	const float Behind = State.DepthCurrent;
	const FVector RootWorld = LedgePoint + LedgeNormal * Behind - FVector(0.0f, 0.0f, Below);
	const AVBCharacter* Character = Owner.GetCharacter();
	FVector MeshRel = FVector::ZeroVector;
	if (Character && Character->GetMesh())
	{
		MeshRel = Character->GetMesh()->GetRelativeLocation();
	}

	return FTransform(Facing, RootWorld - Facing.RotateVector(MeshRel));
}

void FVBHangMode::Start(UVBTraversalComponent& Owner, FVBTraversalSession& Session, const FVBTraversalCheckResult& Result)
{
	AVBCharacter* Character = Owner.GetCharacter();
	USplineComponent* Spline = Result.FrontLedgeSpline;
	const UWorld* World = Character ? Character->GetWorld() : nullptr;
	if (!Character || !Spline || !World)
	{
		return;
	}

	// 새 매달림: 지난 상태를 비운다. 누른 채 들어온 W/S 는 중립을 한 번 지나야 유효해진다.
	State = FVBHangRuntime();
	// 자세 기하가 없으면 붙을 자리를 모른다. 매달림 판정(ShouldHangInsteadOfMantle)이 먼저 막지만 시작도 스스로 거절한다.
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	if (!Clips)
	{
		VB_LOG(Error, "StartHang: ClimbAnimSet 이 비었다 - 매달림을 시작하지 않는다(BP 의 TraversalComponent 에 배선)");
		return;
	}

	Owner.Attach(EVBTraversalSurface::Hang);
	Session.SetRecoverMode(MOVE_Walking); // 올라서기 기본. 놓기가 Falling 으로 덮는다.
	State.Ledge = Spline;

	// 옆걸음 범위 = 렛지 폭에서 양끝 MinLedgeWidth/2 를 뺀 구간(GetLedgeTransforms 의 코너 클램프와 같은 값).
	const AVBLevelBlock_Traversable* Block = Cast<AVBLevelBlock_Traversable>(Spline->GetOwner());
	const float HalfWidth = Block ? Block->GetMinLedgeWidth() * 0.5f : 0.0f;
	const float Length = Spline->GetSplineLength();
	State.MinDistance = FMath::Min(HalfWidth, Length * 0.5f);
	State.MaxDistance = FMath::Max(Length - HalfWidth, Length * 0.5f);
	State.Distance = FMath::Clamp(
		Spline->GetDistanceAlongSplineAtLocation(Result.FrontLedgeLocation, ESplineCoordinateSpace::World),
		State.MinDistance, State.MaxDistance);

	Session.EnterSurface(*Character, Result.HitComponent);
	// 루트모션은 켜 둔다. 옆걸음 몽타주가 캡슐을 몰고, 여기서는 결과를 렛지 위로 되돌리기만 한다.

	State.GrabStartLocation = Character->GetActorLocation();
	State.GrabStartTime = World->GetTimeSeconds();
	State.DepthBase = Clips->HangHandDepth; // 잡는 순간은 대기 자세다
	State.BelowBase = Clips->HangRootBelowLedge;
	State.DepthCurrent = State.DepthBase;
	State.BelowCurrent = State.BelowBase;
	Update(Owner, 0.0f); // 첫 프레임부터 자리에 붙인다(블렌드 0 이면 즉시)
}

void FVBHangMode::Update(UVBTraversalComponent& Owner, float DeltaTime)
{
	AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	USplineComponent* Spline = State.Ledge.Get();
	if (!Character || !Spline || !Character->GetWorld())
	{
		// 렛지 액터가 사라졌다(스트리밍 아웃 등) - 매달릴 데가 없으니 놓는다.
		EndByDrop(Owner);
		return;
	}

	// 거리 갱신은 서버와 소유 클라만(입력을 아는 쪽). 시뮬 프록시는 복제된 위치를 받는다.
	const bool bDrives = Character->HasAuthority() || Character->IsLocallyControlled();
	const FTransform LedgeXform = Spline->GetTransformAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
	const FVector LedgeNormal = LedgeXform.GetRotation().GetUpVector();
	if (bDrives && Clips)
	{
		AdvanceAlongLedge(Owner, *Spline, LedgeNormal, DeltaTime);
	}

	if (Clips)
	{
		UpdateDepth(Owner, DeltaTime);
	}

	PlaceCapsule(Owner, *Spline, LedgeNormal);
	if (VBTraversalDiagnostics::IsStatusTelemetryEnabled())
	{
		LogStatus(Owner, *Spline, LedgeNormal);
	}

	if (UCharacterMovementComponent* CMC = Character->GetCharacterMovement())
	{
		// 위치를 손으로 옮기므로 CMC 속도는 항상 0 이어야 한다 - 남으면 모션매칭 궤적이 이동 중으로 읽는다.
		CMC->Velocity = FVector::ZeroVector;
	}

	// 캡슐이 제자리에 선 뒤에 IK 목표를 뽑는다. 반대면 한 프레임 낡은 목표로 손이 끌린다.
	UpdateIKTargets(Owner, Spline->GetLocationAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World), LedgeNormal,
		DeltaTime);
}

void FVBHangMode::AdvanceAlongLedge(UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal, float DeltaTime)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	// 렛지 끝에서 바깥으로 미는 중이면 옆걸음을 걸지 않는다(걸었다 끄기를 매 틱 반복하지 않게).
	const FVector TangentAtNow = Spline.GetDirectionAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
	const FVector RightAtNow = FRotationMatrix::MakeFromX(-LedgeNormal).GetUnitAxis(EAxis::Y);
	const float SignAtNow = (FVector::DotProduct(TangentAtNow, RightAtNow) >= 0.0f) ? 1.0f : -1.0f;
	const float PushDir = ((State.MoveInput.X >= 0.0f) ? 1.0f : -1.0f) * SignAtNow;
	const bool bAtEndPushing = !FMath::IsNearlyZero(State.MoveInput.X) &&
		((PushDir > 0.0f && State.Distance >= State.MaxDistance - 0.5f) ||
			(PushDir < 0.0f && State.Distance <= State.MinDistance + 0.5f));

	// 몽타주 걸기/끊기/루프만 맡긴다. 몽타주가 도는 동안 반환값은 0 이고, 이동은 아래에서 실제로 옮겨진 양으로 잰다.
	const float Advance = TickShimmyMontage(Owner, DeltaTime, bAtEndPushing ? 0.0f : State.MoveInput.X);

	// 이 틱 루트모션 이동량 = 직전에 놓은 자리와 지금 자리의 차이(렛지 접선 성분만).
	float RootMotionAlong = 0.0f;
	if (State.bPlacedOnce)
	{
		const FVector Moved = Character->GetActorLocation() - State.LastPlacedLocation;
		const FVector TangentDir = Spline.GetDirectionAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
		RootMotionAlong = FVector::DotProduct(Moved, TangentDir);
	}

	// 입력 X(+오른쪽) 를 스플라인 진행 방향 부호로 바꾼다 - 스플라인이 어느 쪽으로 그어졌든 오른쪽 입력은 오른쪽으로.
	const FVector Tangent = Spline.GetDirectionAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
	const FVector Right = FRotationMatrix::MakeFromX(-LedgeNormal).GetUnitAxis(EAxis::Y);
	const float Sign = FVector::DotProduct(Tangent, Right) >= 0.0f ? 1.0f : -1.0f;
	const float Direction = (State.MoveInput.X >= 0.0f) ? 1.0f : -1.0f;
	// 몽타주가 돌면 RootMotionAlong 이 본체이고 Advance 는 0, 몽타주가 없으면 반대다.
	const float Step = RootMotionAlong + Advance * Direction * Sign;
	const float NewDistance = FMath::Clamp(State.Distance + Step, State.MinDistance, State.MaxDistance);
	// 렛지 끝에서 바깥으로 계속 미는가. 진행량 0 은 막힘이 아니다(손을 심은 구간, 몽타주 첫 프레임도 0).
	const bool bBlocked =
		(Step > 0.0f && FMath::IsNearlyEqual(NewDistance, State.MaxDistance)) ||
		(Step < 0.0f && FMath::IsNearlyEqual(NewDistance, State.MinDistance));
	State.Distance = NewDistance;
	// 블렌드스페이스 축. 끝에서 미는 중이면 0 으로 내려 제자리 옆걸음을 지운다.
	// bBlocked 는 진행량으로 판정해 몽타주를 끈 끝에서는 거짓이 되므로 bAtEndPushing 을 따로 본다.
	const float AxisTarget = (bAtEndPushing || bBlocked) ? 0.0f : FMath::Clamp(State.MoveInput.X, -1.0f, 1.0f);
	State.MoveAxis = FMath::FInterpTo(State.MoveAxis, AxisTarget, DeltaTime, Clips->HangMoveAxisInterpSpeed);
}

void FVBHangMode::UpdateDepth(const UVBTraversalComponent& Owner, float DeltaTime)
{
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	// 몸통을 렛지에서 뗄 깊이와 내릴 높이. 옆걸음 중에는 클립 root 의 흔들림을 더한다.
	// 고정값으로 되돌리면 root 흔들림이 손으로 옮겨가 손이 벽에 들어가거나 뜬다.
	const float TargetDepth = State.ShimmyMontage ? Clips->HangHandDepthShimmy : Clips->HangHandDepth;
	const float TargetBelow = State.ShimmyMontage ? Clips->HangRootBelowLedgeShimmy : Clips->HangRootBelowLedge;
	State.DepthBase = FMath::FInterpConstantTo(State.DepthBase, TargetDepth, DeltaTime, Clips->HangDepthInterpSpeed);
	State.BelowBase = FMath::FInterpConstantTo(State.BelowBase, TargetBelow, DeltaTime, Clips->HangDepthInterpSpeed);
	State.DepthCurrent = State.DepthBase - State.SwayTowardWall;
	State.BelowCurrent = State.BelowBase - State.SwayRise;
}

void FVBHangMode::PlaceCapsule(const UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal)
{
	AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	// 캡슐 트랜스폼은 매 틱 스플라인에서 파생한다. 잡는 순간에는 짧게 미끄러져 들어간다.
	const FTransform Target = ComputeCapsuleTransform(Owner, Spline.GetLocationAtDistanceAlongSpline(State.Distance,
		ESplineCoordinateSpace::World), LedgeNormal);
	FVector Location = Target.GetLocation();
	const float BlendTime = Clips ? Clips->HangGrabBlendTime : 0.0f;
	if (BlendTime > 0.0f)
	{
		const float Alpha = FMath::Clamp(static_cast<float>((Character->GetWorld()->GetTimeSeconds() - State.GrabStartTime) / BlendTime),
			0.0f, 1.0f);
		Location = FMath::Lerp(State.GrabStartLocation, Location, Alpha);
	}

	Character->SetActorLocationAndRotation(Location, Target.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	State.LastPlacedLocation = Character->GetActorLocation();
	State.bPlacedOnce = true;
}

void FVBHangMode::LogStatus(const UVBTraversalComponent& Owner, const USplineComponent& Spline, const FVector& LedgeNormal)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	// 1초에 한 줄 진단: 옆걸음 진행량과 부위별 벽 평면 부호 거리(양수 = 바깥, 음수 = 파고듦).
	if (State.Diag.TraceLog.Ready(Character->GetWorld()->GetTimeSeconds()))
	{
		const USkeletalMeshComponent* M = Character->GetMesh();
		FString Parts;
		if (M)
		{
			// 벽 평면 = 렛지 점 + 법선. 각 뼈의 월드 위치를 그 평면에 대고 부호 거리를 잰다.
			const FVector Plane = Spline.GetLocationAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
			auto Signed = [&](const FName Bone) -> float
			{
				return static_cast<float>(FVector::DotProduct(M->GetSocketLocation(Bone) - Plane, LedgeNormal));
			};
			Parts = FString::Printf(TEXT("손L%+.0f 손R%+.0f 골반%+.0f 무릎L%+.0f 발L%+.0f 발끝L%+.0f 발끝R%+.0f | 손높이L%+.0f"),
				Signed(VBLimbBones::HandL), Signed(VBLimbBones::HandR), Signed(TEXT("pelvis")),
				Signed(TEXT("calf_l")), Signed(VBLimbBones::FootL), Signed(VBLimbBones::BallL), Signed(VBLimbBones::BallR),
				static_cast<float>(M->GetSocketLocation(VBLimbBones::HandL).Z - Plane.Z));
		}
		// 흔들림과 축(MoveAxis)도 찍는다. 축은 BS_VB_LedgeHang 의 X 핀에 물려 있다.
		VB_LOG(Log, "Hang 상태: dist=%.0f (1초간 %+.0f) 입력X=%.2f 축=%.2f 몽타주=%s 깊이=%.1f 높이=%.1f 흔들림(앞뒤%+.1f 상하%+.1f) 스플라인=%s",
			State.Distance, State.Distance - State.Diag.DistanceAtTrace, State.MoveInput.X, State.MoveAxis,
			State.ShimmyMontage ? *State.ShimmyMontage->GetName() : TEXT("없음"), State.DepthCurrent, State.BelowCurrent,
			State.SwayTowardWall, State.SwayRise, *GetNameSafe(&Spline));
		VB_LOG(Log, "Hang 접지: %s   (양수=벽 바깥, 음수=벽 안쪽. 손높이는 렛지 윗면 기준)", *Parts);
		State.Diag.DistanceAtTrace = State.Distance;
	}
}

void FVBHangMode::UpdateIKTargets(const UVBTraversalComponent& Owner, const FVector& LedgePoint, const FVector& LedgeNormal, float DeltaTime)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	USplineComponent* Spline = State.Ledge.Get();
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Spline || !Mesh || !Clips)
	{
		State.IKTargets = FVBHangIKTargets();
		return;
	}

	// 차오르는 세기. 잡는 순간 1 로 튀면 손이 순간이동한다.
	const float BlendIn = Clips->HangIKBlendInTime;
	State.IKAlpha = (BlendIn > 0.0f) ? FMath::Clamp(State.IKAlpha + DeltaTime / BlendIn, 0.0f, 1.0f) : 1.0f;

	// 손: 클립이 그린 자리에서 벽 바깥으로 상수만큼 민다(ABP TwoBoneIK 가 BCS_BoneSpace 로 읽는 오프셋).
	// 월드 한 점을 주면 IK 가 클립과 싸워 손이 떨린다.
	const FVector PushLocal = Clips->HangHandPushDirLocal.GetSafeNormal() * Clips->HangHandPushOut;

	// 각 손이 뻗는 중인지 옆걸음 몽타주 위상으로 가른다. 몽타주가 없으면 둘 다 심은 것으로 본다.
	float ReachAlphaL = 0.0f;
	float ReachAlphaR = 0.0f;
	if (State.ShimmyMontage)
	{
		const USkeletalMeshComponent* M = Character->GetMesh();
		const UAnimInstance* A = M ? M->GetAnimInstance() : nullptr;
		const float Len = State.ShimmyMontage->GetPlayLength();
		const float Phase = (A && Len > 0.0f) ? FMath::Frac(FMath::Max(0.0f, A->Montage_GetPosition(State.ShimmyMontage)) / Len) : 0.0f;
		const float Blend = FMath::Max(Clips->HandReachBlendPhase, KINDA_SMALL_NUMBER);
		// 구간 안이면 1, 밖이면 0, 가장자리에서 Blend 만큼 완만히. 한 프레임에 켜졌다 꺼지면 손이 튄다.
		auto ReachAlpha = [Blend](float Phase01, float Start, float End) -> float
		{
			const float In = FMath::Clamp((Phase01 - (Start - Blend)) / Blend, 0.0f, 1.0f);
			const float Out = FMath::Clamp(((End + Blend) - Phase01) / Blend, 0.0f, 1.0f);
			return FMath::Min(In, Out);
		};
		const float LeadAlpha = ReachAlpha(Phase, Clips->HandReachLeadStart, Clips->HandReachLeadEnd);
		const float TrailAlpha = ReachAlpha(Phase, Clips->HandReachTrailStart, Clips->HandReachTrailEnd);
		// 왼쪽 몽타주는 왼손이 앞서고, 오른쪽 몽타주는 오른손이 앞선다.
		const bool bLeftMontage = (State.ShimmyMontage == Clips->ShimmyMontageL);
		ReachAlphaL = bLeftMontage ? LeadAlpha : TrailAlpha;
		ReachAlphaR = bLeftMontage ? TrailAlpha : LeadAlpha;
	}

	State.bHandReachingL = ReachAlphaL > 0.5f;
	State.bHandReachingR = ReachAlphaR > 0.5f;

	// 두 손 모두 같은 오프셋. 뻗는 손은 허공이라 안 밀어도 되므로 세기만 낮춘다.
	State.IKTargets.HandL = PushLocal;
	State.IKTargets.HandR = PushLocal;
	const float HandBase = State.IKAlpha * Clips->HangHandIKWeight;
	State.IKTargets.HandWeightL = HandBase * (1.0f - ReachAlphaL);
	State.IKTargets.HandWeightR = HandBase * (1.0f - ReachAlphaR);

	// 발: 애니가 둔 발 위치에서 벽 쪽으로 쏴 표면을 찾는다. 못 찾으면 가중치 0 으로 애니 그대로.
	const float Probe = Clips->HangFootProbeDistance;
	const float FootWeight = Clips->HangFootIKWeight * State.IKAlpha;
	const UWorld* World = Character->GetWorld();
	const UVBTraversalConfig* Cfg = Owner.GetConfig();
	auto TraceFoot = [&](const FName BoneName, FVector& OutTarget, float& OutWeight)
	{
		OutWeight = 0.0f;
		if (!World || Mesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			return;
		}

		const FVector FootNow = Mesh->GetSocketLocation(BoneName);
		const FVector Start = FootNow + LedgeNormal * Probe; // 벽 바깥에서 시작
		const FVector End = FootNow - LedgeNormal * Probe; // 벽 안쪽으로 통과
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VBHangFootProbe), false, Character);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, End, Cfg->TraceChannel.GetValue(), Params))
		{
			// 발목을 표면에서 띄운다 - 딱 붙이면 앞으로 뻗은 발가락이 벽을 파고든다.
			const FVector WorldTarget = Hit.ImpactPoint + LedgeNormal * Clips->HangFootWallClearance;
			// 노드가 받는 것은 그 뼈 공간의 오프셋이다(월드 위치가 아니다).
			// 지금은 HangFootIKWeight=0 이지만 켜는 날 월드값이 뼈 공간으로 해석되지 않게 맞춰 둔다.
			OutTarget = Mesh->GetSocketTransform(BoneName).InverseTransformVector(WorldTarget - FootNow);
			OutWeight = FootWeight;
		}
	};
	TraceFoot(VBLimbBones::FootL, State.IKTargets.FootL, State.IKTargets.FootLWeight);
	TraceFoot(VBLimbBones::FootR, State.IKTargets.FootR, State.IKTargets.FootRWeight);
}

void FVBHangMode::SetMoveInput(UVBTraversalComponent& Owner, const FVector2D& Input)
{
	if (!Owner.IsHanging())
	{
		return;
	}

	State.MoveInput = Input;
	if (Owner.IsOwningClient())
	{
		Owner.ServerSetHangMoveInputX(Input.X);
	}
	// 앞 = 올라서기, 뒤 = 놓기. 잡을 때 누르고 있던 축은 중립을 한 번 지나야 유효해지고, 같은 홀드로 한 번만 보낸다.
	if (FMath::Abs(Input.Y) <= 0.5f)
	{
		State.bExitInputArmed = true;
	}

	if (State.bExitInputArmed && !State.bExitRequested)
	{
		if (Input.Y > 0.5f)
		{
			RequestClimbUp(Owner);
		}
		else if (Input.Y < -0.5f)
		{
			RequestDrop(Owner);
		}
	}
}

void FVBHangMode::ApplyMoveInputX(float InputX)
{
	State.MoveInput.X = FMath::Clamp(InputX, -1.0f, 1.0f);
}

void FVBHangMode::RequestClimbUp(UVBTraversalComponent& Owner)
{
	if (!Owner.IsHanging() || State.bExitRequested)
	{
		return;
	}

	State.bExitRequested = true;
	Owner.ServerHangClimbUp();
}

void FVBHangMode::RequestDrop(UVBTraversalComponent& Owner)
{
	if (!Owner.IsHanging() || State.bExitRequested)
	{
		return;
	}

	State.bExitRequested = true;
	// C 로 놓았다 - 질주 자동 진입이 같은 렛지를 곧바로 다시 물지 않게, 의도가 한 번 꺼질 때까지 쉰다.
	Owner.RequireFreshAutoIntent();
	Owner.ServerHangDrop();
}

void FVBHangMode::StartClimbUp(UVBTraversalComponent& Owner, FVBTraversalSession& Session)
{
	AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	USplineComponent* Spline = State.Ledge.Get();
	if (!Owner.IsHanging() || !Character || !Spline || !Clips || !Clips->HangClimbUpMontage)
	{
		EndByDrop(Owner); // 올라설 재료가 없으면 놓는다 - 매달린 채 굳지 않게
		return;
	}
	// 렛지에서 떨어져 올라서기 몽타주로 넘어간다(Exiting). 세션은 복구까지 유지.
	Owner.Detach();
	State.MoveAxis = 0.0f;
	Session.SetRecoverMode(MOVE_Walking);
	// 올라서기는 루트모션이 캐릭터를 렛지 위로 옮기는 동작이다 - 매달림 동안 껐던 것을 되돌린다.
	Character->SetAnimRootMotionTranslationScale(1.0f);

	// 워프 타겟: 지금 매달린 지점의 렛지 안쪽 stand 위치(UpdateWarpTargets 의 Hang 분기).
	const FTransform LedgeXform = Spline->GetTransformAtDistanceAlongSpline(State.Distance, ESplineCoordinateSpace::World);
	FVBTraversalCheckResult R;
	R.ActionType = EVBTraversalActionType::Hang;
	R.bHasFrontLedge = true;
	R.FrontLedgeLocation = LedgeXform.GetLocation();
	R.FrontLedgeNormal = LedgeXform.GetRotation().GetUpVector();
	Owner.UpdateWarpTargets(R, Clips->HangClimbUpMontage);

	VB_LOG(Log, "Traversal Hang: 올라서기 montage=%s dist=%.0f", *Clips->HangClimbUpMontage->GetName(), State.Distance);
	Owner.PlayTraversalMontage(Clips->HangClimbUpMontage, 0.0f, 1.0f);
}

void FVBHangMode::EndByDrop(UVBTraversalComponent& Owner)
{
	if (!Owner.IsHanging())
	{
		return;
	}

	VB_LOG(Log, "Traversal Hang: 놓기 dist=%.0f", State.Distance);
	Owner.BlockHangRegrab();
	Owner.RecoverByFalling(); // 매달림 흔적은 복구가 지운다
}

void FVBHangMode::ClearOnExit(const UVBTraversalComponent& Owner)
{
	State.ClearOnExit(VBTraversalMontage::FindAnim(Owner.GetCharacter()), VBTraversalMontage::StopBlendTime(Owner.GetClips()));
}

float FVBHangMode::TickShimmyMontage(const UVBTraversalComponent& Owner, float DeltaTime, float InputX)
{
	const AVBCharacter* Character = Owner.GetCharacter();
	const UVBClimbAnimSet* Clips = Owner.GetClips();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	const float StopBlend = VBTraversalMontage::StopBlendTime(Clips);
	const bool bShimmyInput = Clips && FMath::Abs(InputX) > Clips->HangShimmyInputThreshold;
	UAnimMontage* Wanted = nullptr;
	if (bShimmyInput)
	{
		Wanted = (InputX >= 0.0f) ? Clips->ShimmyMontageR : Clips->ShimmyMontageL;
	}
	// 질주(Shift) 배속 - 사다리, 등반과 같은 꼴(좌우 같은 배율이라 크기로 판정).
	const float ShimmyPlayRate = Clips
		? VBTraversalRules::PlayRate::ResolveTraversalPlayRate(UVBLocomotionIntentComponent::IsSprintingFor(Character),
		FMath::Abs(InputX), Clips->ShimmyFastRate, Clips->ShimmyFastRate) : 1.0f;

	// 몽타주 경로가 없으면 옛 경로(일정 속도)로 떨어진다. 없는 것은 깨진 것이 아니다.
	if (!Anim || !Wanted)
	{
		if (State.ShimmyMontage && Anim)
		{
			Anim->Montage_Stop(StopBlend, State.ShimmyMontage);
			State.ShimmyMontage = nullptr;
		}

		State.SwayTowardWall = 0.0f;
		State.SwayRise = 0.0f;
		// 옛 경로로 떨어진 것을 밝힌다. 조용한 것은 문제 없음이 아니다.
		if (bShimmyInput && !LongLivedDiag.bShimmyFallbackWarned)
		{
			LongLivedDiag.bShimmyFallbackWarned = true;
			VB_LOG(Warning, "Hang 옆걸음: 몽타주 미할당 - 일정 속도(%.0f cm/s) 폴백. 심은 손이 미끄러진다",
				Clips->HangShimmySpeed);
		}

		return Clips ? Clips->HangShimmySpeed * DeltaTime * FMath::Abs(FMath::Clamp(InputX, -1.0f,
			1.0f)) * ShimmyPlayRate : 0.0f;
	}

	// 걸기, 섹션 루프, 끊기는 DriveLoopingMontage 가 맡는다.
	// 끝날 때 다시 걸면 BlendOut 만큼 일찍 끝나 자세가 튀고 남은 루트모션이 버려진다.
	bool bShimmyStarted = false;
	State.ShimmyMontage = VBTraversalMontage::DriveLoopingMontage(Anim, Wanted, State.ShimmyMontage, StopBlend, bShimmyStarted);
	// 배속만 바꾼다(다시 걸지 않음 - 위상 유지). 이동은 몽타주 루트모션이 맡으니 배속이 곧 이동 배율이다.
	Anim->Montage_SetPlayRate(State.ShimmyMontage, ShimmyPlayRate);
	if (bShimmyStarted)
	{
		State.ShimmyPosition = 0.0f;
		State.SwayTowardWall = 0.0f;
		State.SwayRise = 0.0f;
		// 한 주기에 한 줄. 미끄러짐이 몽타주가 안 걸린 탓인지 값이 이상한 탓인지 이 줄이 가른다.
		VB_LOG(Log, "Hang 옆걸음: %s dist=%.0f 입력=%.2f", *Wanted->GetName(), State.Distance, InputX);
		return 0.0f; // 시작 프레임은 진행 0 - 아직 아무것도 재생 안 됐다
	}

	// 몽타주가 도는 동안 렛지를 따라가는 이동은 루트모션이 맡는다. 여기서 또 값을 내면 두 곳이 같은 이동을 더한다.
	State.ShimmyPosition = Anim->Montage_GetPosition(State.ShimmyMontage);

	// 앞뒤, 상하 흔들림은 배치가 고정값으로 되돌리므로 위상 0 부터의 누적 루트모션에서 꺼내 돌려준다.
	// 증분이 아니라 누적이라 루프에서 스스로 0 이 된다. 메시 공간이라 +Y 가 벽 쪽이다.
	const FTransform Accum = State.ShimmyMontage->ExtractRootMotionFromTrackRange(0.0f, State.ShimmyPosition,
		FAnimExtractContext());
	State.SwayTowardWall = static_cast<float>(Accum.GetTranslation().Y);
	State.SwayRise = static_cast<float>(Accum.GetTranslation().Z);
	return 0.0f;
}
