// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "PlayBot/Scenarios/VBPlayBotScenario_Ladder.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBClimbAnimSet.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "Serialization/Archive.h"
#include "Traversal/VBLadder.h"
#include "Traversal/VBTraversalComponent.h"
#include "UObject/Package.h"

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_Ladder, "Ladder")

namespace VBPlayBotLadder
{
	FString Quoted(const FString& Text) { return TEXT("\"") + Text.ReplaceCharWithEscapedChar() + TEXT("\""); }
	FString Vec(const FVector& V) { return FString::Printf(TEXT("[%.6f,%.6f,%.6f]"), V.X, V.Y, V.Z); }
	FString CsvVector(const FVector& V) { return FString::Printf(TEXT("%.6f,%.6f,%.6f"), V.X, V.Y, V.Z); }
	bool Number(const FVBPlayBotRunConfig& C, const TCHAR* Key, double& Out, FString& Error)
	{
		FString Text;
		if (C.RequireString(Key, Text, Error) && FDefaultValueHelper::ParseDouble(Text, Out) && FMath::IsFinite(Out))
		{
			return true;
		}

		Error = FString::Printf(TEXT("[%s] %s needs a finite number"), *C.Section, Key);
		return false;
	}

	bool Point(const FVBPlayBotRunConfig& C, const FString& Prefix, FVector& Out, FString& Error)
	{
		return Number(C, *(Prefix + TEXT("X")), Out.X, Error)
			&& Number(C, *(Prefix + TEXT("Y")), Out.Y, Error)
			&& Number(C, *(Prefix + TEXT("Z")), Out.Z, Error);
	}

	AActor* UniqueActor(UWorld* World, const FString& Name)
	{
		AActor* Match = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName() != Name && It->GetActorNameOrLabel() != Name)
			{
				continue;
			}

			if (Match)
			{
				return nullptr;
			}

			Match = *It;
		}

		return Match;
	}

	double Median(TArray<double> Values)
	{
		if (Values.IsEmpty())
		{
			return -1;
		}

		Values.Sort();
		const int32 Middle = Values.Num() / 2;
		return Values.Num() % 2 ? Values[Middle] : (Values[Middle - 1] + Values[Middle]) * 0.5;
	}
}

FVBPlayBotScenario_Ladder::~FVBPlayBotScenario_Ladder() { StopObserver(); }

bool FVBPlayBotScenario_Ladder::Setup(FVBPlayBotContext& Ctx, FString& OutError)
{
	using namespace VBPlayBotLadder;
	Player = Cast<AVBCharacter>(Ctx.Player);
	Traversal = UVBTraversalComponent::FindFor(Player.Get());
	Mesh = Player.IsValid() ? Player->GetMesh() : nullptr;
	if (!Player.IsValid() || !Traversal.IsValid() || !Mesh.IsValid() || !Ctx.PlayerASC
		|| !Ctx.World || Ctx.World->WorldType != EWorldType::Game || Ctx.World->GetNetMode() != NM_Standalone
		|| !Player->HasAuthority() || !Ctx.Input || !Ctx.Checks || !Ctx.Overrides || !Ctx.Human)
	{
		OutError = TEXT("LadderContact requires the normal standalone -game runner, VB player/traversal/mesh/ASC and pad context");
		return false;
	}

	const FVBPlayBotRunConfig& C = *Ctx.Config;
	if (Ctx.World->GetOutermost()->GetName() != C.Map || Traversal->IsDoingTraversal()
		|| !FVBPlayBotContext::IsGrounded(Ctx.Player)
		|| !FMath::IsNearlyEqual(Player->CustomTimeDilation, 1.f)
		|| !FMath::IsNearlyEqual(Ctx.World->GetWorldSettings()->GetEffectiveTimeDilation(), 1.f))
	{
		OutError = TEXT("wrong map, busy/ungrounded start, or non-unit dilation; no reset/teleport fallback");
		return false;
	}

	FString Name, Text, AssetPath;
	if (!C.RequireString(TEXT("LadderName"), Name, OutError))
	{
		return false;
	}

	Ladder = Cast<AVBLadder>(UniqueActor(Ctx.World, Name));
	if (!C.RequireString(TEXT("LandingName"), Name, OutError))
	{
		return false;
	}

	Landing = UniqueActor(Ctx.World, Name);
	if (!Ladder.IsValid() || !Landing.IsValid())
	{
		OutError = TEXT("named ladder or landing is missing/ambiguous (names/labels are not assumed to survive other targets)");
		return false;
	}

	FVector ExpectedOrigin, ExpectedLanding, ExpectedSize;
	double FixtureTolerance = 0.0, GeometryTolerance = 0.0;
	if (!Point(C, TEXT("LadderOrigin"), ExpectedOrigin, OutError)
		|| !Point(C, TEXT("Landing"), ExpectedLanding, OutError)
		|| !Point(C, TEXT("LandingSize"), ExpectedSize, OutError)
		|| !Number(C, TEXT("FixtureToleranceCm"), FixtureTolerance, OutError)
		|| !Number(C, TEXT("GeometryToleranceCm"), GeometryTolerance, OutError))
	{
		return false;
	}

	FVector BoundsOrigin, BoundsExtent;
	Landing->GetActorBounds(false, BoundsOrigin, BoundsExtent);
	if (FixtureTolerance <= 0 || GeometryTolerance <= 0
		|| !Ladder->GetActorLocation().Equals(ExpectedOrigin, FixtureTolerance)
		|| !Ladder->GetActorScale3D().Equals(FVector::OneVector, GeometryTolerance)
		|| !Ladder->GetActorQuat().Equals(FQuat::Identity, GeometryTolerance)
		|| !BoundsOrigin.Equals(ExpectedLanding, FixtureTolerance)
		|| !(BoundsExtent * 2).Equals(ExpectedSize, FixtureTolerance))
	{
		OutError = TEXT("fixture transform/bounds differ from the measured test ladder/landing; refresh the input manifest");
		return false;
	}

	const TPair<const TCHAR*, double> Geometry[] = {
		{TEXT("LadderHeight"), Ladder->GetLadderHeight()}, {TEXT("LadderWidth"), Ladder->GetLadderWidth()},
		{TEXT("LadderSpacing"), Ladder->GetRungSpacing()}, {TEXT("LadderSubdivision"), Ladder->GetRungSubdivision()},
		{TEXT("LadderDrop"), Ladder->GetRungDropOffset()}, {TEXT("LadderDepth"), Ladder->GetRungDepth()},
		{TEXT("LadderThickness"), Ladder->GetPartThickness()}};
	for (const auto& Item : Geometry)
	{
		double Expected;
		if (!Number(C, Item.Key, Expected, OutError))
		{
			return false;
		}

		if (!FMath::IsNearlyEqual(Item.Value, Expected, GeometryTolerance))
		{
			OutError = FString::Printf(TEXT("fixture %s %.6f differs from %.6f"), Item.Key, Item.Value, Expected);
			return false;
		}
	}

	if (!C.RequireString(TEXT("JumpAction"), Text, OutError))
	{
		return false;
	}

	JumpAction = FName(*Text);
	if (!C.RequireString(TEXT("SprintAction"), Text, OutError))
	{
		return false;
	}

	SprintAction = FName(*Text);
	if (!VBPlayBotSkills::EnsureAction(Ctx, JumpAction, OutError)
		|| !VBPlayBotSkills::EnsureAction(Ctx, SprintAction, OutError))
	{
		return false;
	}

	struct FNumberField { const TCHAR* Key; double* Value; };
	const FNumberField Fields[] = {
		{TEXT("ApproachDistanceCm"), &ApproachDistance}, {TEXT("ApproachTimeoutSeconds"), &ApproachTimeout},
		{TEXT("StateTimeoutSeconds"), &StateTimeout}, {TEXT("MoveTimeoutSeconds"), &MoveTimeout},
		{TEXT("FacingDot"), &FaceDot}, {TEXT("FacingStickMagnitude"), &FaceMagnitude},
		{TEXT("CameraAlignedDegrees"), &FaceAngle}, {TEXT("IdleSeconds"), &IdleSeconds},
		{TEXT("StopSeconds"), &StopSeconds}, {TEXT("StopMeasureSeconds"), &StopMeasureSeconds},
		{TEXT("StopHeightToleranceCm"), &StopTolerance}, {TEXT("FirstHeightCm"), &FirstHeight},
		{TEXT("ResumeHeightCm"), &ResumeHeight}, {TEXT("LowHeightCm"), &LowHeight},
		{TEXT("LandingFloorZ"), &LandingZ}, {TEXT("LandingFloorToleranceCm"), &FloorTolerance},
		{TEXT("RateTolerance"), &RateTolerance}, {TEXT("RateMaxSampleSeconds"), &RateMaxSampleSeconds},
		{TEXT("RateMaxPhaseDelta"), &RateMaxPhaseDelta}};
	for (const FNumberField& Field : Fields)
	{
		if (!Number(C, Field.Key, *Field.Value, OutError))
		{
			return false;
		}

		if (*Field.Value <= 0) { OutError = FString::Printf(TEXT("%s must be positive"), Field.Key); return false; }
	}

	double ApproachTolerance;
	if (!Number(C, TEXT("ApproachToleranceCm"), ApproachTolerance, OutError))
	{
		return false;
	}

	RequiredLandingFrames = C.GetInt(TEXT("LandingStableFrames"), 0);
	RequiredRateSamples = C.GetInt(TEXT("MinRateSamples"), 0);
	// -VBLadderAnimSet=<asset>: 후보 사다리 세트로 코스를 돈다. ClimbAnimSet 은 실행 override 로 바꾸므로
	// 모든 출구(끝, 중단, 준비 실패, 월드 종료)에서 속도 변형과 함께 저작 포인터를 되돌린다.
	// 매달림, 등반도 같은 필드를 읽지만 비교는 사다리만 한다.
	FString ConfiguredSet, CandidateSet;
	if (!C.RequireString(TEXT("ClimbAnimSet"), ConfiguredSet, OutError))
	{
		return false;
	}

	const bool bSwapSet = FParse::Value(FCommandLine::Get(), TEXT("VBLadderAnimSet="), CandidateSet);
	if (bSwapSet && CandidateSet.IsEmpty())
	{
		OutError = TEXT("-VBLadderAnimSet= needs an asset path (an empty value would silently run the authored set)");
		return false;
	}

	if (bSwapSet)
	{
		FVBPlayBotVariant Swap;
		Swap.Name = TEXT("LadderAnimSet");
		Swap.ObjectPath = Traversal->GetPathName();
		Swap.Property = FName(TEXT("ClimbAnimSet"));
		Swap.Value = CandidateSet;
		FString SwapReadback;
		if (!Ctx.Overrides->Apply(Swap, SwapReadback, OutError))
		{
			return false;
		}

		const UVBClimbAnimSet* CandidateAsset = LoadObject<UVBClimbAnimSet>(nullptr, *CandidateSet);
		if (!CandidateAsset || Traversal->GetClimbAnimSetForIK() != CandidateAsset)
		{
			OutError = FString::Printf(TEXT("ladder anim set swap readback differs: %s"), *SwapReadback);
			return false;
		}
	}

	AssetPath = CandidateSet.IsEmpty() ? ConfiguredSet : CandidateSet;
	const UVBClimbAnimSet* Set = Traversal->GetClimbAnimSetForIK();
	if (!Set || !Set->LadderUpMontage || !Set->LadderDownMontage || !Set->HangClimbUpMontage
		|| RequiredLandingFrames <= 0 || RequiredRateSamples <= 0 || ApproachTolerance <= 0
		|| FaceDot >= 1 || FaceMagnitude > 1 || FaceAngle >= 180 || StopMeasureSeconds >= StopSeconds
		|| !(LowHeight < FirstHeight && FirstHeight < ResumeHeight)
		|| ResumeHeight >= Ladder->GetLadderHeight() - Set->LadderTopHandReach - Ladder->GetRungSpacing())
	{
		OutError = TEXT("invalid course thresholds, missing active clips, or insufficient pre-top-out measurement space");
		return false;
	}

	if (LoadObject<UVBClimbAnimSet>(nullptr, *AssetPath) != Set)
	{
		OutError = TEXT("variant object must be the traversal component's actual active climb asset");
		return false;
	}

	bUseSprint = !FParse::Param(FCommandLine::Get(), TEXT("VBLadderNoSprint"));
	bDirectReverse = FParse::Param(FCommandLine::Get(), TEXT("VBLadderDirectReverse"));
	if (!FParse::Value(FCommandLine::Get(), TEXT("VBLadderVariant="), ActiveVariant) || ActiveVariant.IsEmpty())
	{
		OutError = TEXT("choose one fresh-process variant with -VBLadderVariant=Rate1x or Rate2x");
		return false;
	}

	FVBPlayBotVariant Chosen;
	TSet<FString> VariantNames;
	for (const FString& Value : C.GetArray(TEXT("Variant")))
	{
		FVBPlayBotVariant Candidate;
		if (!Candidate.Parse(Value, OutError))
		{
			return false;
		}

		if (VariantNames.Contains(Candidate.Name) || Candidate.ObjectPath != ConfiguredSet
			|| Candidate.Property != FName(TEXT("LadderFastUpRate")))
		{ OutError = TEXT("variants need unique names and the configured LadderFastUpRate property"); return false; }
		VariantNames.Add(Candidate.Name);
		if (Candidate.Name == ActiveVariant)
		{
			Chosen = Candidate;
		}
	}

	Chosen.ObjectPath = AssetPath; // the rate goes to the set that actually runs (candidate or authored)
	if (VariantNames.Num() != 2 || Chosen.IsAuthored() || !FDefaultValueHelper::ParseDouble(Chosen.Value, Rate) || (Rate != 1 && Rate != 2))
	{ OutError = TEXT("selected variant is missing or is not a 1x/2x comparison"); return false; }
	if (!bUseSprint && Rate != 1)
	{ OutError = TEXT("-VBLadderNoSprint climbs at the normal clip rate - pair it with Rate1x only"); return false; }
	FString UpReadback, DownReadback;
	if (!Ctx.Overrides->Apply(Chosen, UpReadback, OutError))
	{
		return false;
	}

	Chosen.Property = FName(TEXT("LadderFastDownRate"));
	if (!Ctx.Overrides->Apply(Chosen, DownReadback, OutError))
	{
		return false;
	}

	if (!FMath::IsNearlyEqual(static_cast<double>(Set->LadderFastUpRate), Rate)
		|| !FMath::IsNearlyEqual(static_cast<double>(Set->LadderFastDownRate), Rate))
	{ OutError = TEXT("runtime variant readback differs"); return false; }

	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("VBPlayBotOut="), OutDir) || OutDir.IsEmpty())
	{ OutError = TEXT("missing runner output directory"); return false; }
	const FString CsvPath = FPaths::Combine(OutDir, TEXT("ladder_pose.csv"));
	PoseFile.Reset(IFileManager::Get().CreateFileWriter(*CsvPath, FILEWRITE_NoReplaceExisting));
	if (!PoseFile) { OutError = TEXT("cannot create a fresh ladder_pose.csv (no overwrite)"); return false; }
	WritePose(TEXT("engine_frame,world_seconds,finalize_serial,intent_phase,limb,bone_valid,bone_x,bone_y,bone_z,qx,qy,qz,qw,sx,sy,sz,contact_x,contact_y,contact_z,mesh_x,mesh_y,mesh_z,capsule_x,capsule_y,capsule_z,capsule_radius,capsule_halfheight,projected_mesh_height,montage_position,montage_rate,on_ladder,sprinting\n"));
	Telemetry = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.LadderIK.Telemetry"));
	if (!Telemetry) { OutError = TEXT("existing ladder IK telemetry CVar missing"); return false; }
	OldTelemetry = Telemetry->GetInt();
	Telemetry->Set(2, ECVF_SetByConsole);
	bChangedTelemetry = true;
	if (Telemetry->GetInt() != 2) { OutError = TEXT("cannot enable existing ladder IK telemetry"); return false; }
	PlacementTelemetry = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Ladder.PlacementTelemetry"));
	if (!PlacementTelemetry) { OutError = TEXT("ladder placement telemetry CVar missing"); return false; }
	OldPlacementTelemetry = PlacementTelemetry->GetInt();
	PlacementTelemetry->Set(1, ECVF_SetByConsole);
	bChangedPlacementTelemetry = true;
	if (PlacementTelemetry->GetInt() != 1) { OutError = TEXT("cannot enable ladder placement telemetry"); return false; }
	FinalizedHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(
		FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateRaw(this, &FVBPlayBotScenario_Ladder::OnFinalized));
	if (!FinalizedHandle.IsValid()) { OutError = TEXT("finalized-bone observer registration failed"); return false; }
	Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
	{
		if (Press.Action == JumpAction)
		{
			bJumpInjected = true;
		}

		if (Press.Action == SprintAction)
		{
			bSprintInjected = true;
		}
	};
	Ctx.Human->BeginTrial(0);
	Walk.Then<FVBSkillApproachPoint>(TEXT("walk to measured ladder front"), [this](FVBPlayBotContext&)
	{
		return Ladder->GetAxisPoint(0) + Ladder->GetFacingNormal() * ApproachDistance;
	}, ApproachTolerance, ApproachTimeout);
	Enter.Then<FVBSkillPress>(TEXT("normal jump/traversal entry"), JumpAction);
	PhaseFrame = Ctx.Frame;
	Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"ladder_fixture\",\"f\":%lld,\"variant\":%s,\"rate\":%.3f,\"ladder\":%s,\"ladder_id\":%u,\"landing\":%s,\"origin\":%s,\"landing_bounds_origin\":%s,\"landing_bounds_extent\":%s,\"active_set\":%s,\"configured_set\":%s,\"mesh\":%s,\"main_instance\":%s,\"post_instance\":%s,\"post_disabled\":%s,\"mesh_relative\":%s,\"depth\":%.6f,\"foot_pose_depth\":%.6f,\"hand_weight\":%.6f,\"foot_weight\":%.6f,\"left_grip\":%s,\"right_grip\":%s,\"up_readback\":%s,\"down_readback\":%s,\"csv\":%s,\"fixed_views\":false,\"sprint\":%s}"),
		Ctx.Frame, *Quoted(ActiveVariant), Rate, *Quoted(Ladder->GetPathName()), Ladder->GetUniqueID(),
		*Quoted(Landing->GetPathName()), *Vec(Ladder->GetActorLocation()), *Vec(BoundsOrigin), *Vec(BoundsExtent),
		*Quoted(Set->GetPathName()), *Quoted(ConfiguredSet), *Quoted(GetPathNameSafe(Mesh->GetSkeletalMeshAsset())),
		*Quoted(GetPathNameSafe(Mesh->GetAnimInstance())), *Quoted(GetPathNameSafe(Mesh->GetPostProcessInstance())),
		Mesh->GetDisablePostProcessBlueprint() ? TEXT("true") : TEXT("false"), *Quoted(Mesh->GetRelativeTransform().ToString()),
		Set->LadderDepth, Set->LadderFootPoseDepth, Set->LadderHandIKWeight, Set->LadderFootIKWeight,
		*Vec(Set->LadderLeftHandGripOffset), *Vec(Set->LadderRightHandGripOffset), *Quoted(UpReadback), *Quoted(DownReadback), *Quoted(CsvPath), bUseSprint ? TEXT("true") : TEXT("false")));
	for (const auto& Item : Geometry)
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"ladder_geometry\",\"f\":%lld,\"key\":%s,\"value\":%.6f}"), Ctx.Frame, *Quoted(Item.Key), Item.Value));
	// 재구성한 격자만이 아니라 실제 보이는 인스턴스(레일, 발판, 브래킷)를 기록한다.
	TArray<UInstancedStaticMeshComponent*> PartComponents;
	Ladder->GetComponents(PartComponents);
	int32 InstanceCount = 0;
	for (const UInstancedStaticMeshComponent* Parts : PartComponents)
	{
		for (int32 Index = 0; Index < Parts->GetInstanceCount(); ++Index)
		{
			FTransform Instance;
			if (!Parts->GetInstanceTransform(Index, Instance, true))
			{ OutError = TEXT("ladder visual instance readback failed"); return false; }
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"ladder_visual_instance\",\"f\":%lld,\"component\":%s,\"index\":%d,\"world_transform\":%s}"),
				Ctx.Frame, *Quoted(Parts->GetPathName()), Index, *Quoted(Instance.ToString())));
			++InstanceCount;
		}
	}

	if (!InstanceCount) { OutError = TEXT("ladder has no visible mesh instances to compare against"); return false; }
	return true;
}

const TCHAR* FVBPlayBotScenario_Ladder::PhaseName() const
{
	switch (Phase)
	{
	case EPhase::Walk: return TEXT("walk"); case EPhase::Face: return TEXT("face");
	case EPhase::Enter: return TEXT("enter"); case EPhase::Idle: return TEXT("idle");
	case EPhase::Sprint: return TEXT("sprint"); case EPhase::Up: return TEXT("up");
	case EPhase::Stop: return TEXT("stop"); case EPhase::Resume: return TEXT("resume");
	case EPhase::Down: return TEXT("down"); case EPhase::LowStop: return TEXT("low_stop");
	case EPhase::Top: return TEXT("top"); case EPhase::Land: return TEXT("land");
	default: return TEXT("done");
	}
}

void FVBPlayBotScenario_Ladder::SetPhase(FVBPlayBotContext& Ctx, EPhase Next)
{
	Ctx.Input->SetStickWant(FVector2D::ZeroVector);
	Ctx.Input->SetLookWant(FVector2D::ZeroVector);
	Phase = Next;
	PhaseFrame = Ctx.Frame;
	ActionReadyFrame = Ctx.Frame + Ctx.Human->GetProfile().PresentationFrames
		+ Ctx.MsToFrames(Ctx.Human->SampleSimpleReactionMs());
	Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"ladder_phase\",\"f\":%lld,\"engine_frame\":%llu,\"gt\":%.6f,\"phase\":\"%s\",\"action_ready\":%lld}"),
		Ctx.Frame, GFrameCounter, Ctx.World->GetTimeSeconds(), PhaseName(), ActionReadyFrame));
}

bool FVBPlayBotScenario_Ladder::OnExpectedLadder() const
{
	return Traversal.IsValid() && Traversal->IsOnLadder() && Traversal->GetActiveLadderForIK() == Ladder.Get();
}

double FVBPlayBotScenario_Ladder::Height() const
{
	return Ladder->ProjectToHeight(Mesh->GetComponentLocation());
}

bool FVBPlayBotScenario_Ladder::CheckLanding() const
{
	const UCharacterMovementComponent* Move = Player->GetCharacterMovement();
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	return Move && Capsule && !Traversal->IsDoingTraversal() && !Traversal->IsOnLadder()
		&& Move->IsMovingOnGround() && Move->CurrentFloor.IsWalkableFloor()
		&& Move->CurrentFloor.HitResult.GetActor() == Landing.Get()
		&& FMath::Abs(Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight() - LandingZ) <= FloorTolerance;
}

FString FVBPlayBotScenario_Ladder::DescribeLanding() const
{
	const UCharacterMovementComponent* Move = Player.IsValid() ? Player->GetCharacterMovement() : nullptr;
	const UCapsuleComponent* Capsule = Player.IsValid() ? Player->GetCapsuleComponent() : nullptr;
	if (!Move || !Capsule || !Traversal.IsValid())
	{
		return TEXT("player/movement/capsule/traversal missing");
	}

	const FHitResult& Floor = Move->CurrentFloor.HitResult;
	return FString::Printf(TEXT("doing_traversal=%d on_ladder=%d on_ground=%d walkable=%d floor_actor=%s floor_component=%s start_penetrating=%d floor_dist=%.2f line=%d impact=%s capsule=%s ignored_components=%d landing=%s sole_z=%.2f landing_z=%.2f tol=%.2f"),
		Traversal->IsDoingTraversal(), Traversal->IsOnLadder(), Move->IsMovingOnGround(), Move->CurrentFloor.IsWalkableFloor(),
		*GetNameSafe(Floor.GetActor()), *GetNameSafe(Floor.GetComponent()), Floor.bStartPenetrating, Move->CurrentFloor.FloorDist,
		Move->CurrentFloor.bLineTrace, *Floor.ImpactPoint.ToString(), *Capsule->GetComponentLocation().ToString(),
		Capsule->GetMoveIgnoreComponents().Num(), *GetNameSafe(Landing.Get()),
		Capsule->GetComponentLocation().Z - Capsule->GetScaledCapsuleHalfHeight(), LandingZ, FloorTolerance);
}

void FVBPlayBotScenario_Ladder::Sample(FVBPlayBotContext& Ctx)
{
	using namespace VBPlayBotLadder;
	const UVBClimbAnimSet* Set = Traversal->GetClimbAnimSetForIK();
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	const double Now = Ctx.World->GetTimeSeconds();
	const double Position = Montage ? Anim->Montage_GetPosition(Montage) : 0;
	const bool bUp = Set && Montage && Montage == Set->LadderUpMontage;
	const bool bDown = Set && Montage && Montage == Set->LadderDownMontage;
	if (OnExpectedLadder() && UVBLocomotionIntentComponent::IsSprintingFor(Player.Get()) == bUseSprint && Montage
		&& Montage == PreviousMontage.Get()
		&& Now > PreviousTime && Position > PreviousPosition && (Phase == EPhase::Up || Phase == EPhase::Resume
			|| Phase == EPhase::Down || Phase == EPhase::Top))
	{
		// 불연속은 기울기 묶음에서 빼되, Montage 재시작, 되감기를 속도로 치지 않는다.
		const double Dt = Now - PreviousTime;
		const double Delta = Position - PreviousPosition;
		if (Dt <= RateMaxSampleSeconds && Delta <= RateMaxPhaseDelta)
		{
			if (bUp)
			{
				UpRates.Add(Delta / Dt);
			}

			if (bDown)
			{
				DownRates.Add(Delta / Dt);
			}
		}
	}

	if (bDown && Set && PreviousMontage.Get() == Set->LadderUpMontage)
	{
		bSawDirectReverse = true;
	}

	PreviousMontage = Montage;
	PreviousPosition = Position;
	PreviousTime = Now;
	Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"ladder_sample\",\"f\":%lld,\"engine_frame\":%llu,\"gt\":%.6f,\"phase\":\"%s\",\"on_ladder\":%s,\"height\":%.6f,\"mesh_origin\":%s,\"capsule\":%s,\"montage\":%s,\"pos\":%.6f,\"montage_rate\":%.6f,\"asset_rate_scale\":%.6f,\"sprinting\":%s}"),
		Ctx.Frame, GFrameCounter, Now, PhaseName(), OnExpectedLadder() ? TEXT("true") : TEXT("false"),
		Height(), *Vec(Mesh->GetComponentLocation()), *Vec(Player->GetActorLocation()), *Quoted(GetPathNameSafe(Montage)),
		Position, Montage ? Anim->Montage_GetPlayRate(Montage) : 0, Montage ? Montage->RateScale : 0,
		UVBLocomotionIntentComponent::IsSprintingFor(Player.Get()) ? TEXT("true") : TEXT("false")));
}

EVBPlayBotState FVBPlayBotScenario_Ladder::Tick(FVBPlayBotContext& Ctx)
{
	if (bFinished)
	{
		return EVBPlayBotState::Succeeded;
	}

	if (!Player.IsValid() || !Traversal.IsValid() || !Mesh.IsValid() || !Ladder.IsValid() || !Landing.IsValid())
	{
		return Finish(Ctx, TEXT("required pawn/mesh/traversal/fixture lost"));
	}

	if (!ObservationError.IsEmpty())
	{
		return Finish(Ctx, ObservationError);
	}

	if (FirstRunFrame < 0)
	{
		FirstRunFrame = Ctx.Frame;
		bObserve = true;
	}

	Sample(Ctx);
	const int64 Elapsed = Ctx.Frame - PhaseFrame;
	const bool bReady = Ctx.Frame >= ActionReadyFrame;
	const double Magnitude = Ctx.Human->GetProfile().MoveMagnitude;
	auto Move = [&](double Axis) { if (bReady) Ctx.Input->SetStickWant(FVector2D(0, Axis * Magnitude)); };
	if (Phase >= EPhase::Idle && Phase <= EPhase::Top && !OnExpectedLadder())
	{
		const UVBClimbAnimSet* Set = Traversal->GetClimbAnimSetForIK();
		UAnimInstance* Anim = Mesh->GetAnimInstance();
		bSawTopOut = Phase == EPhase::Top && Set && Anim && Traversal->IsDoingTraversal()
			&& Anim->GetCurrentActiveMontage() == Set->HangClimbUpMontage;
		if (!bSawTopOut)
		{
			return Finish(Ctx, TEXT("left the expected ladder without the measured top-out transition"));
		}

		SetPhase(Ctx, EPhase::Land);
		return EVBPlayBotState::Running;
	}

	switch (Phase)
	{
	case EPhase::Walk:
	{
		const EVBPlayBotState State = Walk.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			return Finish(Ctx, Walk.GetFailure());
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			SetPhase(Ctx, EPhase::Face);
		}
		break;
	}
	case EPhase::Face:
	{
		const bool bCameraAligned = VBPlayBotSkills::TurnCameraToward(Ctx, Ladder.Get(), FaceAngle);
		const bool bBodyAligned = FVector::DotProduct(Player->GetActorForwardVector(), -Ladder->GetFacingNormal()) >= FaceDot;
		if (bCameraAligned && bBodyAligned && FVBPlayBotContext::IsGrounded(Player.Get()))
		{
			SetPhase(Ctx, EPhase::Enter);
		}
		else if (bReady)
		{
			Ctx.Input->SetStickWant(Ctx.ToStick(-Ladder->GetFacingNormal()) * FaceMagnitude);
		}

		if (Elapsed > Ctx.SecondsToFrames(StateTimeout))
		{
			return Finish(Ctx, TEXT("normal input could not face the ladder"));
		}
		break;
	}
	case EPhase::Enter:
	{
		const EVBPlayBotState State = Enter.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			return Finish(Ctx, Enter.GetFailure());
		}

		if (State == EVBPlayBotState::Succeeded && bJumpInjected && OnExpectedLadder())
		{ bEntered = true; SetPhase(Ctx, EPhase::Idle); }
		else if (Elapsed > Ctx.SecondsToFrames(StateTimeout))
		{
			return Finish(Ctx, TEXT("normal traversal press did not enter the expected ladder"));
		}
		break;
	}
	case EPhase::Idle:
		if (Elapsed >= Ctx.SecondsToFrames(IdleSeconds))
		{
			SetPhase(Ctx, EPhase::Sprint);
		}
		break;
	case EPhase::Sprint:
		if (!bUseSprint)
		{
			SetPhase(Ctx, EPhase::Up);
			break;
		}

		if (SprintPressId == INDEX_NONE)
		{
			SprintPressId = VBPlayBotSkills::ReactPress(Ctx, SprintAction, FKey(), TEXT("ladder fast-path comparison"), Ctx.Frame,
				Ctx.Config->MaxRunSeconds);
			if (SprintPressId == INDEX_NONE)
			{
				return Finish(Ctx, TEXT("sprint input could not be scheduled"));
			}
		}

		if (bSprintInjected && UVBLocomotionIntentComponent::IsSprintingFor(Player.Get()))
		{
			SetPhase(Ctx, EPhase::Up);
		}
		else if (Elapsed > Ctx.SecondsToFrames(StateTimeout))
		{
			return Finish(Ctx, TEXT("held normal sprint input did not activate sprint"));
		}
		break;
	case EPhase::Up:
		Move(1);
		if (Height() >= FirstHeight)
		{
			bAscended = true;
			SetPhase(Ctx, EPhase::Stop);
		}
		break;
	case EPhase::Stop:
		if (Elapsed >= Ctx.SecondsToFrames(StopSeconds - StopMeasureSeconds))
		{ StopMin = FMath::Min(StopMin, Height()); StopMax = FMath::Max(StopMax, Height()); }
		if (Elapsed >= Ctx.SecondsToFrames(StopSeconds))
		{
			const UVBClimbAnimSet* Set = Traversal->GetClimbAnimSetForIK();
			const UAnimInstance* Anim = Mesh->GetAnimInstance();
			const UAnimMontage* Idle = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
			const bool bIdle = Set && Idle && (Idle == Set->LadderIdleMontage || Idle == Set->LadderIdleMirrorMontage);
			bStopped = bIdle && StopMax >= StopMin && StopMax - StopMin <= StopTolerance && Ctx.Input->GetStickOut().IsNearlyZero();
			SetPhase(Ctx, EPhase::Resume);
		}
		break;
	case EPhase::Resume:
		Move(1);
		if (Height() >= ResumeHeight)
		{
			bResumed = true;
			SetPhase(Ctx, EPhase::Down);
			if (bDirectReverse)
			{
				ActionReadyFrame = Ctx.Frame;
				Ctx.Input->SnapStick(FVector2D(0, -Magnitude));
			}
		}
		break;
	case EPhase::Down:
		Move(-1);
		if (Height() <= LowHeight)
		{
			bDescended = true;
			SetPhase(Ctx, EPhase::LowStop);
		}
		break;
	case EPhase::LowStop:
		if (Elapsed >= Ctx.SecondsToFrames(StopSeconds))
		{
			SetPhase(Ctx, EPhase::Top);
		}
		break;
	case EPhase::Top:
		Move(1);
		break;
	case EPhase::Land:
		StableLandingFrames = CheckLanding() ? StableLandingFrames + 1 : 0;
		if (StableLandingFrames >= RequiredLandingFrames)
		{
			bLanded = true;
			return Finish(Ctx, FString());
		}

		if (Elapsed > Ctx.SecondsToFrames(StateTimeout))
		{
			return Finish(Ctx, FString(TEXT("top-out did not reach stable Walking on the measured landing: ")) + DescribeLanding());
		}
		break;
	default: break;
	}

	if ((Phase == EPhase::Up || Phase == EPhase::Resume || Phase == EPhase::Down || Phase == EPhase::Top)
		&& Elapsed > Ctx.SecondsToFrames(MoveTimeout))
	{
		return Finish(Ctx, TEXT("ladder movement stage timed out"));
	}

	return EVBPlayBotState::Running;
}

void FVBPlayBotScenario_Ladder::WritePose(const FString& Line)
{
	if (!PoseFile)
	{
		return;
	}

	FTCHARToUTF8 Bytes(*Line);
	PoseFile->Serialize(const_cast<ANSICHAR*>(Bytes.Get()), Bytes.Length());
	if (PoseFile->IsError())
	{
		ObservationError = TEXT("ladder_pose.csv write failed");
	}
}

void FVBPlayBotScenario_Ladder::OnFinalized()
{
	using namespace VBPlayBotLadder;
	if (!bObserve || bFinished || !Mesh.IsValid() || !Player.IsValid() || !Traversal.IsValid() || !Ladder.IsValid())
	{
		return;
	}

	if (!IsInGameThread()) { ObservationError = TEXT("finalized observer was not on the game thread"); return; }
	const UVBClimbAnimSet* Set = Traversal->GetClimbAnimSetForIK();
	if (!Set) { ObservationError = TEXT("active climb data lost during pose observation"); return; }
	++FinalizeSerial;
	const FName Bones[] = {TEXT("hand_l"), TEXT("hand_r"), TEXT("ball_l"), TEXT("ball_r"), TEXT("root")};
	UAnimInstance* Anim = Mesh->GetAnimInstance();
	UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
	bool bAllValid = true;
	for (int32 Limb = 0; Limb < UE_ARRAY_COUNT(Bones); ++Limb)
	{
		const int32 BoneIndex = Mesh->GetBoneIndex(Bones[Limb]);
		const bool bValid = BoneIndex != INDEX_NONE;
		bAllValid &= bValid;
		const FTransform Bone = bValid ? Mesh->GetBoneTransform(BoneIndex) : FTransform::Identity;
		const FQuat Q = Bone.GetRotation();
		FVector Contact = Bone.GetLocation();
		if (Limb == 0)
		{
			Contact = Bone.TransformPosition(Set->LadderLeftHandGripOffset);
		}

		if (Limb == 1)
		{
			Contact = Bone.TransformPosition(Set->LadderRightHandGripOffset);
		}

		WritePose(FString::Printf(TEXT("%llu,%.6f,%lld,%s,%s,%d,%s,%.8f,%.8f,%.8f,%.8f,%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d\n"),
			GFrameCounter, Player->GetWorld()->GetTimeSeconds(), FinalizeSerial, PhaseName(), *Bones[Limb].ToString(), bValid,
			*CsvVector(Bone.GetLocation()), Q.X, Q.Y, Q.Z, Q.W, *CsvVector(Bone.GetScale3D()), *CsvVector(Contact),
			*CsvVector(Mesh->GetComponentLocation()), *CsvVector(Capsule ? Capsule->GetComponentLocation() : Player->GetActorLocation()),
			Capsule ? Capsule->GetScaledCapsuleRadius() : 0, Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0, Height(),
			Montage ? Anim->Montage_GetPosition(Montage) : 0, Montage ? Anim->Montage_GetPlayRate(Montage) : 0,
			OnExpectedLadder(), UVBLocomotionIntentComponent::IsSprintingFor(Player.Get())));
		++PoseRows;
	}

	if (OnExpectedLadder() && bAllValid)
	{
		++ClimbingPoses;
	}

	if (!bAllValid)
	{
		ObservationError = TEXT("required hand/ball/root bones missing; invalid CSV rows are marked bone_valid=0");
	}
}

EVBPlayBotState FVBPlayBotScenario_Ladder::Finish(FVBPlayBotContext& Ctx, const FString& Reason)
{
	using namespace VBPlayBotLadder;
	bFinished = true;
	bObserve = false;
	Ctx.Input->ReleaseAll(Ctx.Frame);
	if (PoseFile)
	{
		PoseFile->Flush();
	}

	if (Telemetry && bChangedTelemetry)
	{
		Telemetry->Set(OldTelemetry, ECVF_SetByConsole);
		bChangedTelemetry = false;
	}

	if (PlacementTelemetry && bChangedPlacementTelemetry)
	{
		PlacementTelemetry->Set(OldPlacementTelemetry, ECVF_SetByConsole);
		bChangedPlacementTelemetry = false;
	}

	Ctx.Checks->Expect(TEXT("LadderContact.TelemetryRestored"), Telemetry && Telemetry->GetInt() == OldTelemetry
		&& PlacementTelemetry && PlacementTelemetry->GetInt() == OldPlacementTelemetry,
		true, Ctx.Frame, FString::Printf(TEXT("{\"original\":%d,\"readback\":%d,\"placement_original\":%d,\"placement_readback\":%d}"),
			OldTelemetry, Telemetry ? Telemetry->GetInt() : -1, OldPlacementTelemetry, PlacementTelemetry ? PlacementTelemetry->GetInt() : -1));
	const double Up = Median(UpRates), Down = Median(DownRates);
	Ctx.Checks->Expect(TEXT("LadderContact.Entry"), bEntered && bJumpInjected, true, Ctx.Frame);
	Ctx.Checks->Expect(TEXT("LadderContact.Up"), bAscended, true, Ctx.Frame);
	Ctx.Checks->Expect(TEXT("LadderContact.Stop"), bStopped, true, Ctx.Frame,
		StopMax >= StopMin ? FString::Printf(TEXT("{\"height_range\":%.6f}"), StopMax - StopMin) : TEXT("{}"));
	Ctx.Checks->Expect(TEXT("LadderContact.Resume"), bResumed, true, Ctx.Frame);
	Ctx.Checks->Expect(TEXT("LadderContact.Down"), bDescended, true, Ctx.Frame);
	Ctx.Checks->Expect(TEXT("LadderContact.TopOut"), bSawTopOut && bLanded, true, Ctx.Frame);
	Ctx.Checks->Expect(TEXT("LadderContact.EffectiveRate"), (bSprintInjected || !bUseSprint) && UpRates.Num() >= RequiredRateSamples
		&& DownRates.Num() >= RequiredRateSamples && FMath::Abs(Up - Rate) <= RateTolerance && FMath::Abs(Down - Rate) <= RateTolerance,
		true, Ctx.Frame, FString::Printf(TEXT("{\"expected\":%.6f,\"up_median\":%.6f,\"down_median\":%.6f,\"up_samples\":%d,\"down_samples\":%d}"), Rate, Up, Down, UpRates.Num(), DownRates.Num()));
	Ctx.Checks->Expect(TEXT("LadderContact.PoseStream"), PoseRows > 0 && ClimbingPoses > 0 && PoseFile
		&& !PoseFile->IsError() && ObservationError.IsEmpty(), true, Ctx.Frame,
		FString::Printf(TEXT("{\"rows\":%lld,\"climbing_finalizations\":%lld,\"error\":%s}"), PoseRows, ClimbingPoses, *Quoted(ObservationError)));
	Ctx.Checks->Expect(TEXT("LadderContact.Course"), Reason.IsEmpty() && bEntered && bAscended && bStopped
		&& bResumed && bDescended && bSawTopOut && bLanded, true, Ctx.Frame,
		FString::Printf(TEXT("{\"reason\":%s,\"variant\":%s}"), *Quoted(Reason), *Quoted(ActiveVariant)));
	if (bDirectReverse)
	{
		Ctx.Checks->Expect(TEXT("LadderContact.DirectReverse"), bSawDirectReverse, true, Ctx.Frame, TEXT("{}"));
	}

	Ctx.Checks->Unavailable(TEXT("LadderContact.FixedViews"), Ctx.Frame,
		TEXT("recorder captures player viewport only; independent side/front observer capture is not implemented"));
	Ctx.Checks->Unavailable(TEXT("LadderContact.SkinContact"), Ctx.Frame,
		TEXT("final bones/grip proxies are not rendered finger/shoe surfaces or planted-state evidence"));
	Ctx.Checks->Unavailable(TEXT("LadderContact.IKNodeEvaluation"), Ctx.Frame,
		TEXT("CVar and finalized bone stream do not prove ladder node execution; validate LogVBLadderIK samples in game.log"));
	Ctx.Checks->Unavailable(TEXT("LadderContact.FullCycleCoverage"), Ctx.Frame,
		TEXT("4m ladder reaches its root-height top clamp before a complete uninterrupted ascent cycle; inspect actual montage phase coverage"));
	Ctx.Checks->Look(TEXT("LadderContact.ContactAndTopOut"), FirstRunFrame >= 0 ? FirstRunFrame : Ctx.Frame, Ctx.Frame,
		TEXT("player-view contact/top-out diagnostic; match finalized CSV to engine-frame marker, then judge actual grip/sole surfaces; side/front coverage missing"));
	Phase = EPhase::Done;
	// 정상 마무리는 recorder 정렬을 유지하고, 실패한 검사가 실행 상태를 정하게 둔다.
	return EVBPlayBotState::Succeeded;
}

void FVBPlayBotScenario_Ladder::StopObserver()
{
	bObserve = false;
	if (Mesh.IsValid() && FinalizedHandle.IsValid())
	{
		Mesh->UnregisterOnBoneTransformsFinalizedDelegate(FinalizedHandle);
	}

	FinalizedHandle.Reset();
	if (PoseFile)
	{
		PoseFile->Flush();
		PoseFile.Reset();
	}

	if (Telemetry && bChangedTelemetry)
	{
		Telemetry->Set(OldTelemetry, ECVF_SetByConsole);
		bChangedTelemetry = false;
	}

	if (PlacementTelemetry && bChangedPlacementTelemetry)
	{
		PlacementTelemetry->Set(OldPlacementTelemetry, ECVF_SetByConsole);
		bChangedPlacementTelemetry = false;
	}
}

void FVBPlayBotScenario_Ladder::Teardown(FVBPlayBotContext& Ctx)
{
	StopObserver();
	Ctx.OnPressInjected = nullptr;
	if (Ctx.Input)
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
	}
}
