#include "Camera/VBEnvironmentCameraSolver.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Traversal/VBTraversalComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBEnvironmentCamera, Log, All);
static TAutoConsoleVariable<int32> CVarVBEnvironmentCameraDebug(TEXT("vb.Camera.Environment.Debug"), 0,
	TEXT("1: draw desired/final camera and safe anchor; log collision state changes."));

namespace
{
	double CameraProbeRadius(FMinimalViewInfo View, FIntPoint ViewSize, const APlayerController* PC,
		const UVBEnvironmentCameraConfig& Config)
	{
		ViewSize.X = FMath::Max(1, ViewSize.X);
		ViewSize.Y = FMath::Max(1, ViewSize.Y);
		FIntRect Rect(0, 0, ViewSize.X, ViewSize.Y);
		FIntRect Constrained = Rect;
		if (View.bConstrainAspectRatio && View.AspectRatio > 0)
		{
			const int32 Width = FMath::Min(ViewSize.X, FMath::RoundToInt(ViewSize.Y * View.AspectRatio));
			const int32 Height = FMath::Min(ViewSize.Y, FMath::RoundToInt(ViewSize.X / View.AspectRatio));
			Constrained = FIntRect(0, 0, FMath::Max(1, Width), FMath::Max(1, Height));
		}

		FSceneViewProjectionData Projection;
		Projection.SetViewRectangle(Rect);
		const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		const auto Axis = View.AspectRatioAxisConstraint.Get(LocalPlayer ?
			LocalPlayer->AspectRatioAxisConstraint.GetValue() : AspectRatio_MaintainXFOV);
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Axis, Constrained, Projection);
		const FMatrix Inverse = Projection.ProjectionMatrix.Inverse();
		double Radius = FMath::Max(1.f, Config.MinimumProbeRadius);
		// Reversed-Z 투영이라 z=1 이 near plane 이다. 구가 네 모서리를 모두 감싼다.
		for (double X : {-1.0, 1.0}) for (double Y : {-1.0, 1.0})
		{
			const FVector4 P = Inverse.TransformFVector4(FVector4(X, Y, 1, 1));
			if (FMath::IsFinite(P.W) && FMath::Abs(P.W) > UE_SMALL_NUMBER)
			{
				const double Length = FVector(P.X / P.W, P.Y / P.W, P.Z / P.W).Length();
				if (FMath::IsFinite(Length))
				{
					Radius = FMath::Max(Radius, Length);
				}
			}
		}

		return Radius + FMath::Max(0.f, Config.SurfaceMargin);
	}

	// 구도를 막는 캐릭터 상태(래그돌, 매달림)는 VBBodyControlPolicy 한 곳이 정한다. 조준은 이 소비처의 조건이다.
	bool AllowsCameraComposition(const ACharacter* Pawn)
	{
		const UVBPlayerRagdollComponent* Ragdoll = UVBPlayerRagdollComponent::FindFor(Pawn);
		const UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Pawn);
		VBBodyControlPolicy::FEnvironmentCameraFacts BodyFacts;
		BodyFacts.bPlayerRagdoll = Ragdoll && Ragdoll->IsRagdollActive();
		BodyFacts.bTraversalFacing = Traversal && Traversal->OwnsCapsuleFacing();
		return !UVBLocomotionIntentComponent::IsAimingFor(Pawn) && !VBBodyControlPolicy::BlocksEnvironmentCameraComposition(BodyFacts);
	}

	// 캐릭터와 캐릭터에 붙은 소유 액터를 무시하는 질의를 만든다.
	FCollisionQueryParams MakeCameraQuery(const ACharacter* Pawn)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VBEnvironmentCamera), false, Pawn);
		TArray<AActor*> Attached;
		Pawn->GetAttachedActors(Attached, true, true);
		for (AActor* Actor : Attached)
		{
			if (Actor && Actor->IsOwnedBy(Pawn))
			{
				Query.AddIgnoredActor(Actor);
			}
		}

		return Query;
	}
}

// 한 프레임의 풀이 재료다. 구 하나로 자리가 안전한지 보고 길을 훑는다.
struct FVBEnvironmentCameraSolver::FSolveFrame
{
	UWorld* World;
	const UVBEnvironmentCameraConfig* Settings;
	FCollisionQueryParams Query;
	FCollisionShape Shape;
	ECollisionChannel Channel;
	double Radius;
	FVector Desired;
	FVector Up;
	FVector Right;
	FVector Center;
	FVector Anchor;
	double Inset;
	bool bAllowComposition;

	bool IsSafe(const FVector& P) const
	{
		return !P.ContainsNaN() && !World->OverlapBlockingTestByChannel(P, FQuat::Identity, Channel, Shape, Query);
	}

	// Start 에서 End 로 훑어 닿기 전 자리를 Out 에 적는다. 안전한 자리를 찾았으면 true 다.
	bool Clip(const FVector& Start, const FVector& End, FVector& Out) const
	{
		FHitResult Hit;
		Out = End;
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel, Shape, Query))
		{
			if (Hit.bStartPenetrating)
			{
				return false;
			}

			const double Distance = FVector::Distance(Start, End);
			const double T = Distance > UE_SMALL_NUMBER ? FMath::Max(0.0, double(Hit.Time) - Settings->SurfaceMargin / Distance) : 0;
			Out = FMath::Lerp(Start, End, T);
		}

		return IsSafe(Out);
	}

	// 후보 넷의 목표: 그대로, 어깨 안쪽, 아래, 어깨 안쪽 아래.
	void FillCandidateTargets(FVector (&Targets)[4]) const
	{
		const double ShoulderOffset = FVector::DotProduct(Desired - Anchor, Right);
		const FVector Inward = -Right * FMath::Clamp(ShoulderOffset, -double(Settings->MaxLateralAdjustment),
			double(Settings->MaxLateralAdjustment));
		Targets[0] = Desired;
		Targets[1] = Desired + Inward;
		Targets[2] = Desired - Up * Settings->MaxHeightAdjustment;
		Targets[3] = Desired + (Inward - Up * Settings->MaxHeightAdjustment);
	}

	// 후보를 채우고 점수를 매긴다. 원하는 길이 막혔으면 true 다. 막히지 않았거나 구도가 막힌 캐릭터면 후보는 하나다.
	bool EvaluateCandidates(const FVector (&Targets)[4],
		TArray<VBEnvironmentCameraPolicy::FCandidate, TInlineAllocator<4>>& Candidates) const
	{
		FVector BaseSafe;
		const bool bBaseSafe = Clip(Anchor, Desired, BaseSafe);
		const bool bObstructed = !bBaseSafe || !BaseSafe.Equals(Desired, 0.1);
		const int32 Count = bObstructed && bAllowComposition ? 4 : 1;
		for (int32 I = 0; I < Count; ++I)
		{
			VBEnvironmentCameraPolicy::FCandidate& C = Candidates.AddDefaulted_GetRef();
			C.bSafe = I == 0 ? bBaseSafe : Clip(Anchor, Targets[I], C.Position);
			if (I == 0)
			{
				C.Position = BaseSafe;
			}

			if (!C.bSafe)
			{
				continue;
			}

			C.Score = FVector::Distance(C.Position, Desired);
			if (bObstructed)
			{
				for (double Height : {0.0, Inset})
				{
					FHitResult Visibility;
					if (World->LineTraceSingleByChannel(Visibility, C.Position, Center + Up * Height, Channel, Query))
					{
						C.Score += Settings->VisibilityPenalty;
					}
				}
			}
		}

		return bObstructed;
	}
};

void FVBEnvironmentCameraSolver::Reset()
{
	Recovery.Reset();
	LocalAdjustment = FVector::ZeroVector;
	LastCandidate = INDEX_NONE;
	bHasHistory = false;
}

void FVBEnvironmentCameraSolver::Update(ACharacter* Pawn, const UVBEnvironmentCameraConfig* Settings,
	float DeltaTime, bool bCameraCut, FMinimalViewInfo& Pose)
{
	using namespace VBEnvironmentCameraPolicy;
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Settings || !PC || !PC->IsLocalController() || Pawn->GetNetMode() == NM_DedicatedServer)
	{
		Reset();
		return;
	}

	UWorld* World = Pawn->GetWorld();
	const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
	if (!World || !Capsule)
	{
		return;
	}

	if (GFrameCounter > LastSolvedFrame + 1)
	{
		Reset();
	}

	LastSolvedFrame = GFrameCounter;
	if (Pose.Location.ContainsNaN())
	{
		Reset();
		return;
	}

	FSolveFrame Frame = BuildSolveFrame(*Pawn, *PC, *Capsule, *Settings, Pose);
	if (!SecureAnchor(Frame, *Pawn, Pose))
	{
		return;
	}

	if (bCameraCut || (bHasHistory && FVector::DistSquared(Frame.Anchor, LastAnchor) > FMath::Square(Settings->TeleportResetDistance)))
	{
		Reset();
	}

	const double DesiredLength = FVector::Distance(Frame.Anchor, Frame.Desired);
	FVector Targets[4];
	Frame.FillCandidateTargets(Targets);
	TArray<FCandidate, TInlineAllocator<4>> Candidates;
	const bool bObstructed = Frame.EvaluateCandidates(Targets, Candidates);
	const int32 Selected = SelectCandidate(Candidates, LastCandidate, Settings->CandidateSwitchMargin);
	if (Selected == INDEX_NONE)
	{
		Pose.Location = Frame.Anchor;
		Reset();
		return;
	}

	const bool bWasConstrained = Recovery.Fraction < 0.9999;
	const FVector FinalSafe = SolveFinalPosition(Frame, Targets[Selected], bObstructed, DeltaTime);
	Pose.Location = FinalSafe;
	LastAnchor = Frame.Anchor;
	LastPosition = FinalSafe;
	LastCandidate = Selected;
	bHasHistory = true;
	if (CVarVBEnvironmentCameraDebug.GetValueOnGameThread())
	{
		ReportDebug(Frame, *Pawn, bWasConstrained, Selected, DesiredLength, FinalSafe);
	}
}

FVBEnvironmentCameraSolver::FSolveFrame FVBEnvironmentCameraSolver::BuildSolveFrame(ACharacter& Pawn, const APlayerController& PC,
	const UCapsuleComponent& Capsule, const UVBEnvironmentCameraConfig& Settings, const FMinimalViewInfo& Pose)
{
	const FVector Desired = Pose.Location;
	const FVector Up = -Pawn.GetCharacterMovement()->GetGravityDirection();
	const FVector Right = FVector::VectorPlaneProject(Pose.Rotation.RotateVector(FVector::RightVector), Up).GetSafeNormal();
	FIntPoint ViewSize;
	PC.GetViewportSize(ViewSize.X, ViewSize.Y);
	const double Radius = CameraProbeRadius(Pose, ViewSize, &PC, Settings);
	const double PushIn = Radius + Settings.SurfaceMargin;
	const double Inset = FMath::Max(0.0, Capsule.GetScaledCapsuleHalfHeight() - PushIn);
	// 기준점은 늘 캡슐 안이다(Lyra 의 안전 위치) - 래그돌 중에도 캡슐은 래그돌 컴포넌트가 바닥 위에 세운다. 골반은 바닥을 뚫을 수 있다.
	const FVector Center = Capsule.GetComponentLocation();
	const FVector Anchor = VBEnvironmentCameraPolicy::CapsuleSafeLocation(Center, Up, Capsule.GetScaledCapsuleHalfHeight(),
		Capsule.GetScaledCapsuleRadius(), PushIn, Desired, Pose.Rotation.Vector());
	const bool bAllowComposition = AllowsCameraComposition(&Pawn);
	return FSolveFrame{Pawn.GetWorld(), &Settings, MakeCameraQuery(&Pawn), FCollisionShape::MakeSphere(Radius), Settings.TraceChannel,
		Radius, Desired, Up, Right, Center, Anchor, Inset, bAllowComposition};
}

bool FVBEnvironmentCameraSolver::SecureAnchor(FSolveFrame& Frame, const ACharacter& Pawn, FMinimalViewInfo& Pose)
{
	if (!Frame.IsSafe(Frame.Anchor))
	{
		if (!Frame.IsSafe(Frame.Center))
		{
			if (!bWasUnresolved)
			{
				UE_LOG(LogVBEnvironmentCamera, Warning, TEXT("No safe camera anchor for %s; camera held inside the capsule"),
					*Pawn.GetName());
			}

			bWasUnresolved = true;
			// 검사 안 된 입력 위치는 내보내지 않는다 - 직전 안전 위치, 없으면 캡슐 가운데(캐릭터가 가린다).
			Pose.Location = bHasHistory && Frame.IsSafe(LastPosition) ? LastPosition : Frame.Center;
			Reset();
			return false;
		}

		Frame.Anchor = Frame.Center;
	}

	if (bWasUnresolved)
	{
		UE_LOG(LogVBEnvironmentCamera, Log, TEXT("Camera anchor recovered for %s"), *Pawn.GetName());
	}

	bWasUnresolved = false;
	return true;
}

void FVBEnvironmentCameraSolver::UpdateLocalAdjustment(const FSolveFrame& Frame, const FVector& Target, bool bObstructed, float DeltaTime)
{
	const FVector TargetOffset = Target - Frame.Desired;
	const FVector WantedAdjustment(0, FVector::DotProduct(TargetOffset, Frame.Right), FVector::DotProduct(TargetOffset, Frame.Up));
	const double Alpha = VBEnvironmentCameraPolicy::RecoveryAlpha(DeltaTime, Frame.Settings->RecoveryHalfLife);
	// 트인 쪽 유지 동안에는 거리 회복이 시작될 때까지 고른 로컬 오프셋도 유지한다.
	if (!bHasHistory || bObstructed)
	{
		LocalAdjustment = FMath::Lerp(LocalAdjustment, WantedAdjustment, Alpha);
	}
	else if (Recovery.ClearTime >= Frame.Settings->ClearHoldSeconds)
	{
		LocalAdjustment = FMath::Lerp(LocalAdjustment, FVector::ZeroVector, Alpha);
	}

	if (!Frame.bAllowComposition)
	{
		LocalAdjustment = FVector::ZeroVector;
	}
}

FVector FVBEnvironmentCameraSolver::SolveFinalPosition(const FSolveFrame& Frame, const FVector& Target, bool bObstructed, float DeltaTime)
{
	UpdateLocalAdjustment(Frame, Target, bObstructed, DeltaTime);
	const UVBEnvironmentCameraConfig* Settings = Frame.Settings;
	const FVector& Anchor = Frame.Anchor;
	const FVector AdjustedTarget = Frame.Desired + Frame.Right * LocalAdjustment.Y + Frame.Up * LocalAdjustment.Z;
	FVector Safe;
	if (!Frame.Clip(Anchor, AdjustedTarget, Safe))
	{
		Safe = Anchor;
	}

	const double AdjustedLength = FVector::Distance(Anchor, AdjustedTarget);
	const double SafeLength = FVector::Distance(Anchor, Safe);
	const double SafeFraction = AdjustedLength > UE_SMALL_NUMBER ? SafeLength / AdjustedLength : 1;
	const bool bHasClearance = !bObstructed || SafeLength - Recovery.Fraction * AdjustedLength >= Settings->ClearDistanceMargin;
	const double Fraction = Recovery.Update(SafeFraction, DeltaTime, Settings->RecoveryHalfLife,
		Settings->ClearHoldSeconds, bHasClearance);
	const FVector Final = FMath::Lerp(Anchor, AdjustedTarget, Fraction);
	// 직전 -> 이번 위치 재스윕은 두지 않는다 - 모서리에서 카메라가 박혀 멈춘다. 안전은 아래 Anchor -> Final 스윕이 보장한다.
	//   대가: 빠르게 모서리를 돌 때 한 프레임 건너뛸 수 있다.
	FVector FinalSafe;
	if (!Frame.Clip(Anchor, Final, FinalSafe))
	{
		FinalSafe = Anchor;
	}

	return FinalSafe;
}

void FVBEnvironmentCameraSolver::ReportDebug(const FSolveFrame& Frame, const ACharacter& Pawn, bool bWasConstrained, int32 Selected,
	double DesiredLength, const FVector& FinalSafe) const
{
	const bool bConstrained = Recovery.Fraction < 0.9999;
	if (bWasConstrained != bConstrained)
	{
		UE_LOG(LogVBEnvironmentCamera, Log,
			TEXT("%s crouch=%d candidate=%d desired=%.1f actual=%.1f"), bConstrained ? TEXT("Constrained") : TEXT("Clear"),
			Pawn.bIsCrouched, Selected, DesiredLength, FVector::Distance(Frame.Anchor, FinalSafe));
	}

	DrawDebugLine(Frame.World, Frame.Anchor, Frame.Desired, FColor::Red);
	DrawDebugLine(Frame.World, Frame.Anchor, FinalSafe, FColor::Green);
	DrawDebugSphere(Frame.World, FinalSafe, Frame.Radius, 12, FColor::Green);
}
