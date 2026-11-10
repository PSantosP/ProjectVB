#include "Animation/AnimNode_VBLadderIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBLimbBones.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBClimbAnimSet.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Traversal/VBLadder.h"
#include "Traversal/VBTraversalComponent.h"
#include "TwoBoneIK.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBLadderIK, Log, All);

static TAutoConsoleVariable<int32> CVarLadderIKTelemetry(
	TEXT("vb.LadderIK.Telemetry"), 0,
	TEXT("Ladder hand/foot IK telemetry: 0=off, 1=transitions and 10Hz samples, 2=every evaluation. Positions are grip/ball world cm; solver output precedes node alpha blending."),
	ECVF_Default);

FAnimNode_VBLadderIK::FAnimNode_VBLadderIK()
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ends[Index].BoneName = VBLimbBones::Ends[Index];
		Contacts[Index].BoneName = VBLimbBones::Contacts[Index];
		Middles[Index] = Roots[Index] = FCompactPoseBoneIndex(INDEX_NONE);
	}
}

void FAnimNode_VBLadderIK::PreUpdate(const UAnimInstance* InAnimInstance)
{
	// UObject는 게임 스레드에서만 읽는다. 병렬 포즈 평가는 이 값 스냅샷만 쓴다.
	bEnabled = false;
	ClipRate = FVBLadderIKClipRate();
	const ACharacter* Character = Cast<ACharacter>(InAnimInstance->GetOwningActor());
	const UVBTraversalComponent* Traversal = UVBTraversalComponent::FindFor(Character);
	const AVBLadder* Ladder = Traversal ? Traversal->GetActiveLadderForIK() : nullptr;
	bool bRequested = false;
	const AVBLadder* SupportLadder = Traversal && !Ladder ? Traversal->GetLadderTopOutSupportForIK(bRequested) : nullptr;
	const UVBClimbAnimSet* Config = Traversal ? Traversal->GetClimbAnimSetForIK() : nullptr;
	const EVBLadderIKMode Previous = Mode;
	Mode = Config ? FVBLadderIKModeRule::Resolve(Ladder != nullptr, bRequested, SupportLadder != nullptr, bHasGeometry, bResidual)
		: EVBLadderIKMode::Off;
	// 사다리에 새로 들어왔으면 지난 올라서기, 줄이기의 고정점을 되살리지 않는다.
	bResetStates = Mode == EVBLadderIKMode::Ladder && Previous != EVBLadderIKMode::Ladder;
	if (Mode == EVBLadderIKMode::Off)
	{
		bHasGeometry = false;
		return;
	}
	// 기하, 설정은 출처가 있을 때만 새로 읽는다. Fade 는 사다리가 사라져도 줄일 수 있게 마지막 값을 쓴다.
	// 평가가 건너뛰어지는 동안(화면 밖) Fade 가 머무는 것은 의도다 - 다시 돌면 이어서 줄인다.
	if (const AVBLadder* Source = Ladder ? Ladder : SupportLadder)
	{
		LadderId = Source->GetUniqueID();
		Geometry.ToWorld = Source->GetActorTransform();
		Geometry.Height = Source->GetLadderHeight();
		Geometry.Width = Source->GetLadderWidth();
		Geometry.Spacing = FMath::Max(Source->GetRungSpacing(), 5.0f) / FMath::Max(Source->GetRungSubdivision(), 1);
		Geometry.Drop = FMath::Clamp(Source->GetRungDropOffset(), 0.0f, Geometry.Spacing);
		Geometry.Depth = FMath::Max(Source->GetRungDepth(), 0.0f);
		Geometry.Radius = Source->GetPartThickness() * 0.5f;
		MaxCorrection = FMath::Max(Config->LadderIKMaxCorrection, 0.0f);
		FootMaxCorrection = FMath::Max(Config->LadderFootIKMaxCorrection, 0.0f);
		FootPoseOffsetWS = Config->LadderFootPoseDepth > 0.0f
			? Geometry.GetPoseDepthOffset(Config->LadderFootPoseDepth, Config->LadderDepth) : FVector::ZeroVector;
		PoseBlendTime = FMath::Max(Config->LadderEnterBlendTime, 0.0f);
		HandWeight = FMath::Clamp(Config->LadderHandIKWeight, 0.0f, 1.0f);
		HandGripOffsets[0] = Config->LadderLeftHandGripOffset;
		HandGripOffsets[1] = Config->LadderRightHandGripOffset;
		FootWeight = FMath::Clamp(Config->LadderFootIKWeight, 0.0f, 1.0f);
		bHasGeometry = Geometry.Height > 0.0f && Geometry.Width > 0.0f;
	}
	// 사다리를 떠나는 순간의 기본 보정에서 시작해 발마다 따로 줄인다.
	if (Previous == EVBLadderIKMode::Ladder && Mode != EVBLadderIKMode::Ladder)
	{
		// 손은 고정점을 내려놓고, 마지막 평가에서 걸린 보정만 얼려 줄인다.
		for (int32 Foot = 2; Foot < 4; ++Foot)
		{
			SupportBase[Foot].Begin(PoseAlpha);
		}

		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			HandHold[Hand].Begin(LastHandCorrectionWS[Hand]);
		}
	}

	if (Mode == EVBLadderIKMode::Ladder)
	{
		// 후처리 인스턴스라 배속은 본체 AnimInstance 의 사다리 몽타주에서 읽는다. 몽타주 진행이 이 뒤라 이번 진행의 배속과 같다.
		ClipRate = ReadClipRate(Character->GetMesh()->GetAnimInstance(), Traversal->GetActiveLadderMontageForIK());
		if (ClipRate.Source == EVBLadderIKRateSource::Invalid && !bWarnedInvalidRate)
		{
			bWarnedInvalidRate = true;
			UE_LOG(LogVBLadderIK, Warning,
				TEXT("%s: ladder montage %s has a non-finite play rate - contact thresholds fall back to world units"),
				*Character->GetName(), *GetNameSafe(Traversal->GetActiveLadderMontageForIK()));
		}
	}

	bEnabled = bHasGeometry;
	if (CVarLadderIKTelemetry.GetValueOnGameThread() > 0)
	{
		DiagnosticActor = Character->GetName();
		DiagnosticFrame = GFrameCounter;
		DiagnosticWorldTime = Character->GetWorld()->GetTimeSeconds();
		// 이 노드는 후처리 인스턴스에 있으므로 본체 AnimInstance에서 몽타주를 읽는다.
		UAnimInstance* MainAnim = Character->GetMesh()->GetAnimInstance();
		UAnimMontage* Montage = MainAnim ? MainAnim->GetCurrentActiveMontage() : nullptr;
		DiagnosticMontage = GetNameSafe(Montage);
		DiagnosticMontagePosition = Montage ? MainAnim->Montage_GetPosition(Montage) : 0.0f;
	}
}

FVBLadderIKClipRate FAnimNode_VBLadderIK::ReadClipRate(const UAnimInstance* Anim, const UAnimMontage* Montage)
{
	const FAnimMontageInstance* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	if (!Instance)
	{
		return FVBLadderIKClipRate::FromEvaluated(false, false, 0.0f);
	}
	// 구간 배속 = 재생 배속 x RateScale x 구간 배속. 구간이 없으면 0.
	const FAnimSegment* Segment = Montage->SlotAnimTracks.Num() > 0
		? Montage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(Instance->GetPosition()) : nullptr;
	return FVBLadderIKClipRate::FromEvaluated(true, Instance->IsPlaying(),
		Segment ? Instance->GetPlayRate() * Montage->RateScale * Segment->GetValidPlayRate() : 0.0f);
}

void FAnimNode_VBLadderIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);
	Elapsed += Context.GetDeltaTime();
	if (!bEnabled || EvaluatedLadderId != LadderId || bResetStates)
	{
		for (FVBLadderIKContact& State : States)
		{
			State = FVBLadderIKContact();
		}

		for (FVBLadderIKSupportBase& Base : SupportBase)
		{
			Base = FVBLadderIKSupportBase();
		}

		for (int32 Hand = 0; Hand < 2; ++Hand)
		{
			HandHold[Hand].Clear();
			LastHandCorrectionWS[Hand] = FVector::ZeroVector;
		}

		EvaluatedLadderId = LadderId;
		Elapsed = Context.GetDeltaTime();
		DiagnosticElapsed = 0.0f;
		PoseAlpha = 0.0f;
		bResidual = false;
		bResetStates = false;
	}
	// 기본 포즈 보정은 접촉 가중치와 독립이다. 진입 때만 섞고, 올라서기, 줄이기에서는 SupportBase 가 정한다.
	if (Mode == EVBLadderIKMode::Ladder)
	{
		PoseAlpha = PoseBlendTime > SMALL_NUMBER
			? FMath::FInterpConstantTo(PoseAlpha, 1.0f, Context.GetDeltaTime(), 1.0f / PoseBlendTime) : 1.0f;
	}
	else if (!bEnabled)
	{
		PoseAlpha = 0.0f;
	}
}

void FAnimNode_VBLadderIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Ends[Index].Initialize(RequiredBones);
		Contacts[Index].Initialize(RequiredBones);
		const FCompactPoseBoneIndex End = Ends[Index].GetCompactPoseIndex(RequiredBones);
		Middles[Index] = End != INDEX_NONE ? RequiredBones.GetParentBoneIndex(End) : FCompactPoseBoneIndex(INDEX_NONE);
		Roots[Index] = Middles[Index] != INDEX_NONE ? RequiredBones.GetParentBoneIndex(Middles[Index]) : FCompactPoseBoneIndex(INDEX_NONE);
		States[Index] = FVBLadderIKContact();
	}

	Elapsed = 0.0f;
	DiagnosticElapsed = 0.0f;
	PoseAlpha = 0.0f;
}

bool FAnimNode_VBLadderIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Ends[Index].IsValidToEvaluate(RequiredBones) && Contacts[Index].IsValidToEvaluate(RequiredBones)
			&& Roots[Index] != INDEX_NONE && Middles[Index] != INDEX_NONE)
		{
			return true;
		}
	}

	return false;
}

// 말단 하나의 이번 평가 값. 단계 함수가 차례로 채우고 읽는다.
struct FAnimNode_VBLadderIK::FLimbFrame
{
	int32 Index = 0;
	float Dt = 0.0f;
	bool bLadderMode = false;
	bool bEnding = false;
	bool bHandContact = false;
	float Strength = 0.0f;
	float LimbMaxCorrection = 0.0f;
	FCompactPoseBoneIndex EndIndex = FCompactPoseBoneIndex(INDEX_NONE);
	FVector SourceContactWS = FVector::ZeroVector;
	FVector FullBaseWS = FVector::ZeroVector;
	float BaseAlpha = 0.0f;
	FVector PoseCorrectionWS = FVector::ZeroVector;
	FVector ContactWS = FVector::ZeroVector;
	FVector Raw = FVector::ZeroVector;
	bool bWasPlanted = false;
	bool bReset = false;
	bool bSpeedValid = false;
	double Speed = -1.0;
	double AxisSpeed = -1.0;
	float Clearance = 0.0f;
	float AcquireSpeed = 0.0f;
	float DwellTime = 0.0f;
	float ReleaseAt = 0.0f;
	bool bHandHold = false;
	FVector HandHoldNowWS = FVector::ZeroVector;
	bool bLog = false;
};

void FAnimNode_VBLadderIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	if (!bEnabled)
	{
		return;
	}

	const float Dt = Elapsed;
	Elapsed = 0.0f;
	const int32 Telemetry = CVarLadderIKTelemetry.GetValueOnAnyThread();
	++DiagnosticEvaluation;
	DiagnosticElapsed += Dt;
	const bool bSample = Telemetry > 1 || DiagnosticElapsed >= 0.1f;
	if (bSample)
	{
		DiagnosticElapsed = 0.0f;
	}

	bool bAnyResidual = false;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		bAnyResidual |= EvaluateLimb(Output, Index, Dt, Telemetry, bSample, OutBoneTransforms);
	}

	bResidual = bAnyResidual;
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

bool FAnimNode_VBLadderIK::EvaluateLimb(FComponentSpacePoseContext& Output, int32 Index, float Dt, int32 Telemetry, bool bSample,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	const FTransform& MeshToWorld = Output.AnimInstanceProxy->GetComponentTransform();
	FVBLadderIKContact& State = States[Index];
	FLimbFrame Limb;
	Limb.Index = Index;
	Limb.Dt = Dt;
	Limb.bLadderMode = Mode == EVBLadderIKMode::Ladder;
	Limb.bEnding = Mode == EVBLadderIKMode::Fade;
	Limb.bHandContact = Index < 2;
	Limb.Strength = Index < 2 ? HandWeight : FootWeight;
	Limb.LimbMaxCorrection = Index < 2 ? MaxCorrection : FootMaxCorrection;
	if (Limb.Strength <= 0.0f || !Ends[Index].IsValidToEvaluate(Bones) || !Contacts[Index].IsValidToEvaluate(Bones)
		|| Roots[Index] == INDEX_NONE || Middles[Index] == INDEX_NONE)
	{
		State = FVBLadderIKContact();
		if (Index < 2)
		{
			LastHandCorrectionWS[Index] = FVector::ZeroVector;
		}

		return false;
	}

	SampleLimbContact(Output, MeshToWorld, Limb);
	MeasureLimbMotion(Limb);
	StepLimbContact(Limb);

	const bool bLimbResidual = State.Weight > SMALL_NUMBER
		|| (Limb.bHandHold && HandHold[Index].IsActive())
		|| (!Limb.bHandContact && (Limb.bLadderMode ? PoseAlpha : SupportBase[Index].Weight) * Limb.Strength > SMALL_NUMBER);
	Limb.bLog = Telemetry > 0
		&& (bSample || Limb.bReset || !Limb.bSpeedValid || Limb.bWasPlanted != State.bPlanted);
	if (State.Weight <= SMALL_NUMBER && Limb.PoseCorrectionWS.IsNearlyZero() && Limb.HandHoldNowWS.IsNearlyZero())
	{
		if (Limb.bHandContact && Limb.bLadderMode)
		{
			LastHandCorrectionWS[Index] = FVector::ZeroVector;
		}

		LogLimb(Limb, MeshToWorld, Limb.ContactWS, FVector::ZeroVector);
		return bLimbResidual;
	}

	SolveLimb(Output, MeshToWorld, Limb, OutBoneTransforms);
	return bLimbResidual;
}

void FAnimNode_VBLadderIK::SampleLimbContact(FComponentSpacePoseContext& Output, const FTransform& MeshToWorld, FLimbFrame& Limb)
{
	const int32 Index = Limb.Index;
	const FBoneContainer& Bones = Output.Pose.GetPose().GetBoneContainer();
	Limb.EndIndex = Ends[Index].GetCompactPoseIndex(Bones);
	const FCompactPoseBoneIndex ContactIndex = Contacts[Index].GetCompactPoseIndex(Bones);
	const FTransform& ContactTransform = Output.Pose.GetComponentSpaceTransform(ContactIndex);
	const FVector ContactCS = Index < 2
		? ContactTransform.TransformPosition(HandGripOffsets[Index]) : ContactTransform.GetLocation();
	Limb.SourceContactWS = MeshToWorld.TransformPosition(ContactCS);
	// 사다리 위에서는 진입 블렌드가, 올라서기, 줄이기에서는 발마다의 지지 가중치가 기본 보정을 정한다.
	Limb.FullBaseWS = Index < 2 ? FVector::ZeroVector : FootPoseOffsetWS * Limb.Strength;
	Limb.BaseAlpha = Limb.bLadderMode ? PoseAlpha : Index < 2 ? HandHold[Index].Weight : SupportBase[Index].Weight;
	Limb.PoseCorrectionWS = Limb.FullBaseWS * Limb.BaseAlpha;
	Limb.ContactWS = Limb.SourceContactWS + Limb.PoseCorrectionWS;
	// 판정은 원래 움직임으로 한다. 줄어드는 보정을 발이 떠난 것으로 읽으면 안 된다.
	Limb.Raw = Geometry.ToWorld.InverseTransformPosition(Limb.bLadderMode ? Limb.ContactWS : Limb.SourceContactWS + Limb.FullBaseWS);
}

void FAnimNode_VBLadderIK::MeasureLimbMotion(FLimbFrame& Limb)
{
	FVBLadderIKContact& State = States[Limb.Index];
	const float Dt = Limb.Dt;
	const FVector& Raw = Limb.Raw;
	Limb.bWasPlanted = State.bPlanted;
	// 긴 평가 공백/순간이동 뒤에는 이전 접촉을 당겨 오지 않는다.
	if (Dt > 0.25f || (State.bPrimed && Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw).Size() > 100.0))
	{
		State = FVBLadderIKContact();
		Limb.bReset = true;
	}

	Limb.bSpeedValid = State.bPrimed && Dt > SMALL_NUMBER;
	Limb.Speed = Limb.bSpeedValid ? Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw).Size() / Dt : -1.0;
	Limb.Clearance = Limb.Index < 2 ? 0.0f : Geometry.Radius + FootSoleOffset;
	// 속도, 시간 문턱만 클립 배속으로 환산한다. 공간 값은 그대로다.
	Limb.AcquireSpeed = ClipRate.Speed(Limb.bHandContact ? HandApproachAxisSpeed : PlantSpeed);
	Limb.DwellTime = Limb.bHandContact ? 0.0f : ClipRate.Time(PlantDwellTime);
	Limb.AxisSpeed = Limb.bSpeedValid
		? FMath::Abs(FVector::DotProduct(Geometry.ToWorld.TransformVector(Raw - State.PreviousRaw),
			Geometry.ToWorld.GetUnitAxis(EAxis::Z))) / Dt : -1.0;
	Limb.ReleaseAt = FMath::Max(ClipRate.Speed(ReleaseSpeed), Limb.AcquireSpeed);
}

void FAnimNode_VBLadderIK::StepLimbContact(FLimbFrame& Limb)
{
	const int32 Index = Limb.Index;
	const float Dt = Limb.Dt;
	FVBLadderIKContact& State = States[Index];
	// 올라서기, 줄이기의 손은 고정점 대신 얼린 보정을 줄인다(FVBLadderIKHandHold).
	Limb.bHandHold = Limb.bHandContact && !Limb.bLadderMode;
	Limb.HandHoldNowWS = Limb.bHandHold ? HandHold[Index].Step(Dt, ContactBlendTime) : FVector::ZeroVector;
	if (Limb.bHandHold)
	{
		State = FVBLadderIKContact();
	}
	else if (Limb.bHandContact && Limb.bLadderMode && PoseAlpha < 1.0f)
	{
		// 캐릭터가 사다리로 들어오는 앞뒤 이동을 손의 착지로 오인하지 않는다.
		State = FVBLadderIKContact();
	}
	else
	{
		// 출처가 끝났으면 잡은 것을 놓고 줄이기만 한다. 사다리 밖에서는 새로 잡지 않는다.
		if (Limb.bEnding)
		{
			State.bPlanted = false;
		}

		State.Update(Geometry, Limb.Raw, Dt, Limb.Clearance,
			Limb.AcquireSpeed, Limb.ReleaseAt, ClipRate.Time(ContactBlendTime), Limb.DwellTime,
			Limb.LimbMaxCorrection, Limb.bHandContact, HandReleaseTravel, Limb.bLadderMode);
	}

	if (!Limb.bLadderMode && !Limb.bHandContact)
	{
		// 발이 실제로 떠날 때 그 발의 기본 보정만 줄이기 시작한다.
		const bool bLift = (Limb.bWasPlanted && !State.bPlanted) || (!State.bPlanted && Limb.bSpeedValid && Limb.Speed > Limb.ReleaseAt);
		SupportBase[Index].Update(bLift, Limb.bEnding, Dt, ClipRate.Time(ContactBlendTime));
	}
}

void FAnimNode_VBLadderIK::SolveLimb(FComponentSpacePoseContext& Output, const FTransform& MeshToWorld, const FLimbFrame& Limb,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	const int32 Index = Limb.Index;
	const FVBLadderIKContact& State = States[Index];
	FTransform Root = Output.Pose.GetComponentSpaceTransform(Roots[Index]);
	FTransform Middle = Output.Pose.GetComponentSpaceTransform(Middles[Index]);
	FTransform End = Output.Pose.GetComponentSpaceTransform(Limb.EndIndex);
	const FVector LockCorrectionWS = Limb.bHandHold ? Limb.HandHoldNowWS : State.Weight > SMALL_NUMBER
		? (Geometry.ToWorld.TransformPosition(State.Anchor) - Limb.ContactWS).GetClampedToMaxSize(Limb.LimbMaxCorrection) *
			State.Weight * Limb.Strength : FVector::ZeroVector;
	const FVector CorrectionWS = Limb.PoseCorrectionWS + LockCorrectionWS;
	if (Limb.bHandContact && Limb.bLadderMode)
	{
		LastHandCorrectionWS[Index] = CorrectionWS;
	}
	// 말단 목표 = 접촉 목표 - 현재 손목->그립/발목->발볼 벡터. 말단 회전과 스케일을 유지한다.
	const FVector Target = End.GetLocation() + MeshToWorld.InverseTransformVector(CorrectionWS);
	const FQuat EndRotation = End.GetRotation();
	const FVector OriginalEndLocation = End.GetLocation();
	AnimationCore::SolveTwoBoneIK(Root, Middle, End, Middle.GetLocation(), Target, false, 1.0, 1.0);
	End.SetRotation(EndRotation);
	// 말단 회전/스케일이 그대로이므로 그립/발볼도 말단의 이동량만큼 이동한다.
	// 최종 메시 측정값이 아니라 이 노드의 알파 블렌드 전 solver 결과다.
	LogLimb(Limb, MeshToWorld, Limb.SourceContactWS + MeshToWorld.TransformVector(End.GetLocation() - OriginalEndLocation), CorrectionWS);
	OutBoneTransforms.Emplace(Roots[Index], Root);
	OutBoneTransforms.Emplace(Middles[Index], Middle);
	OutBoneTransforms.Emplace(Limb.EndIndex, End);
}

void FAnimNode_VBLadderIK::LogLimb(const FLimbFrame& Limb, const FTransform& MeshToWorld, const FVector& SolverContactWS,
	const FVector& RequestedCorrectionWS) const
{
	if (!Limb.bLog)
	{
		return;
	}

	const int32 Index = Limb.Index;
	const FVBLadderIKContact& State = States[Index];
	const FVector& ContactWS = Limb.ContactWS;
	const FVector& SourceContactWS = Limb.SourceContactWS;
	FVector Candidate;
	const bool bCandidate = Geometry.FindContact(Limb.Raw, Limb.Clearance, Candidate);
	const FVector CandidateWS = bCandidate ? Geometry.ToWorld.TransformPosition(Candidate) : ContactWS;
	const bool bAnchor = State.bPlanted || State.Weight > SMALL_NUMBER;
	const FVector AnchorWS = bAnchor ? Geometry.ToWorld.TransformPosition(State.Anchor) : ContactWS;
	const TCHAR* Event = Limb.bReset ? TEXT("Reset") : !Limb.bSpeedValid ? TEXT("Prime")
		: Limb.bWasPlanted != State.bPlanted ? (State.bPlanted ? TEXT("Plant") : TEXT("Release")) : TEXT("Sample");
	UE_LOG(LogVBLadderIK, Log,
		TEXT("%s frame=%llu eval=%llu time=%.3f actor=%s ladder=%u bone=%s event=%s dt=%.4f montage=%s pos=%.3f speed=%.2f quiet=%.3f planted=%d->%d weight=%.3f strength=%.3f nodeAlpha=%.3f candidate=%d distance=%.2f rawWS=(%s) candidateWS=(%s) anchorValid=%d anchorWS=(%s) requested=%.2f solvedMove=%.2f residual=%.2f solverWS=(%s) maxCorrection=%.2f plantSpeed=%.2f dwell=%.3f sourceWS=(%s) poseOffset=(%s) poseAlpha=%.3f lockRequested=%.2f axisSpeed=%.2f clipRate=%.3f rateSource=%s meshWS=(%s) mode=%s base=%.3f"),
		Index < 2 ? TEXT("Hand") : TEXT("Foot"), DiagnosticFrame, DiagnosticEvaluation, DiagnosticWorldTime, *DiagnosticActor, LadderId,
		*Ends[Index].BoneName.ToString(), Event, Limb.Dt, *DiagnosticMontage, DiagnosticMontagePosition,
		Limb.Speed, State.QuietTime, Limb.bWasPlanted, State.bPlanted, State.Weight, Limb.Strength, ActualAlpha,
		bCandidate, bCandidate ? FVector::Distance(ContactWS, CandidateWS) : -1.0,
		*ContactWS.ToString(), *CandidateWS.ToString(), bAnchor, *AnchorWS.ToString(),
		RequestedCorrectionWS.Size(), FVector::Distance(SourceContactWS, SolverContactWS),
		bAnchor ? FVector::Distance(SolverContactWS, AnchorWS) : -1.0, *SolverContactWS.ToString(),
		Limb.LimbMaxCorrection, Limb.AcquireSpeed, Limb.DwellTime, *SourceContactWS.ToString(), *Limb.PoseCorrectionWS.ToString(),
		PoseAlpha, (RequestedCorrectionWS - Limb.PoseCorrectionWS).Size(), Limb.AxisSpeed, ClipRate.Rate, ClipRate.GetSourceName(),
		*MeshToWorld.GetLocation().ToString(),
		Limb.bLadderMode ? TEXT("Ladder") : Limb.bEnding ? TEXT("Fade") : TEXT("Support"), Limb.BaseAlpha);
}
