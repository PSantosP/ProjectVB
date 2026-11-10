// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "PlayBot/Scenarios/VBPlayBotScenario_PowerPanel.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Queue.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPhysicalDoor.h"
#include "Interaction/VBPowerPanel.h"
#include "Interaction/VBPowerSwitch.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/Archive.h"
#include "UObject/Package.h"

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_PowerPanel, "PowerPanel")

struct FVBPowerExposureSample
{
	uint64 EngineFrame = 0;
	uint32 RendererFrame = 0;
	double WorldSeconds = 0;
	int32 PlayerIndex = INDEX_NONE, Method = 0;
	float CachedExposure = 0, CachedLuminance = 0;
	float Min = 0, Max = 0, Bias = 0, SpeedUp = 0, SpeedDown = 0, Bloom = 0;
	bool bBuffer = false, bPhysical = false;
};

struct FVBPowerPanelExposureState
{
	TAtomic<bool> bActive{true};
	TQueue<FVBPowerExposureSample, EQueueMode::Mpsc> Queue;
};

// 게임의 실제 view 를 읽기만 한다 - view 나 GPU readback 자원을 만들거나 바꾸지 않는다
class FVBPowerPanelExposureView final : public FWorldSceneViewExtension
{
public:
	FVBPowerPanelExposureView(const FAutoRegister& Auto, UWorld* World,
		TSharedRef<FVBPowerPanelExposureState, ESPMode::ThreadSafe> InState)
		: FWorldSceneViewExtension(Auto, World), State(MoveTemp(InState)) {}
	virtual void PreRenderView_RenderThread(FRDGBuilder&, FSceneView& View) override
	{
		if (!State->bActive.Load() || !View.Family || View.Family->Views.Num() != 1
			|| View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection)
		{
			return;
		}

		FVBPowerExposureSample S;
		S.EngineFrame = View.Family->FrameCounter;
		S.RendererFrame = View.Family->FrameNumber;
		S.WorldSeconds = View.Family->Time.GetWorldTimeSeconds();
		S.PlayerIndex = View.PlayerIndex;
		S.bBuffer = View.HasValidEyeAdaptationBuffer();
		S.CachedExposure = View.GetLastEyeAdaptationExposure();
		S.CachedLuminance = View.GetLastAverageSceneLuminance();
		const FPostProcessSettings& PP = View.FinalPostProcessSettings;
		S.Method = static_cast<int32>(PP.AutoExposureMethod);
		S.Min = PP.AutoExposureMinBrightness; S.Max = PP.AutoExposureMaxBrightness;
		S.Bias = PP.AutoExposureBias; S.SpeedUp = PP.AutoExposureSpeedUp; S.SpeedDown = PP.AutoExposureSpeedDown;
		S.bPhysical = PP.AutoExposureApplyPhysicalCameraExposure; S.Bloom = PP.BloomIntensity;
		State->Queue.Enqueue(S);
	}
private:
	TSharedRef<FVBPowerPanelExposureState, ESPMode::ThreadSafe> State;
};

namespace VBPlayBotPowerPanel
{
	FString Q(const FString& Text) { return TEXT("\"") + Text.ReplaceCharWithEscapedChar() + TEXT("\""); }
	FString V(const FVector& P) { return FString::Printf(TEXT("[%.5f,%.5f,%.5f]"), P.X, P.Y, P.Z); }
	AActor* Find(UWorld* World, const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}

		AActor* Found = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName() != Name && It->GetActorNameOrLabel() != Name)
			{
				continue;
			}

			if (Found)
			{
				return nullptr;
			}

			Found = *It;
		}

		return Found;
	}

	bool Point(const FVBPlayBotRunConfig& C, const TCHAR* Key, FVector& P, FString& Error)
	{
		FString Text;
		if (C.RequireString(Key, Text, Error) && P.InitFromString(Text) && !P.ContainsNaN())
		{
			return true;
		}

		Error = FString::Printf(TEXT("invalid required point %s"), Key); return false;
	}
}

FVBPlayBotScenario_PowerPanel::~FVBPlayBotScenario_PowerPanel() { StopObservation(); }

bool FVBPlayBotScenario_PowerPanel::Setup(FVBPlayBotContext& Ctx, FString& Error)
{
	using namespace VBPlayBotPowerPanel;
	const auto& C = *Ctx.Config;
	const auto* Save = UVBSaveGameSubsystem::Get(Ctx.Player);
	if (!Ctx.World || !Ctx.Player || !Ctx.PC || !Ctx.PC->PlayerCameraManager || !Ctx.PlayerASC
		|| !Ctx.Player->HasAuthority() || Ctx.World->WorldType != EWorldType::Game
		|| Ctx.World->GetNetMode() != NM_Standalone || Ctx.World->GetOutermost()->GetName() != C.Map
		|| !Ctx.World->GetGameInstance() || Ctx.World->GetGameInstance()->GetNumLocalPlayers() != 1
		|| !Save || !Save->GetGameStartContext().AttemptId.IsValid() || Save->GetGameStartContext().Intent != EVBGameStartIntent::NewGame)
	{ Error = TEXT("requires isolated fresh direct-start hospital -game, one local authority player and a NewGame receipt"); return false; }
	Interactor = Ctx.Player->FindComponentByClass<UVBInteractorComponent>();
	Story = UVBStoryStateComponent::Get(Ctx.World);
	Panel = Cast<AVBPowerPanel>(Find(Ctx.World, C.GetString(TEXT("PanelName"))));
	L = Cast<AVBPowerSwitch>(Find(Ctx.World, C.GetString(TEXT("WorkSwitchName"))));
	D = Cast<AVBPowerSwitch>(Find(Ctx.World, C.GetString(TEXT("DoorSwitchName"))));
	Door = Cast<AVBPhysicalDoor>(Find(Ctx.World, C.GetString(TEXT("DoorName"))));
	AActor* StartDoor = Find(Ctx.World, C.GetString(TEXT("StartDoorName")));
	AActor* Sill = Find(Ctx.World, C.GetString(TEXT("WindowSillName")));
	AActor* WorkLightActor = Find(Ctx.World, C.GetString(TEXT("WorkLightName")));
	AActor* ExitLightActor = Find(Ctx.World, C.GetString(TEXT("ExitLightName")));
	WorkLight = WorkLightActor ? WorkLightActor->FindComponentByClass<ULightComponent>() : nullptr;
	ExitLight = ExitLightActor ? ExitLightActor->FindComponentByClass<ULightComponent>() : nullptr;
	if (!Interactor.IsValid() || !Story.IsValid() || !Panel.IsValid() || !L.IsValid() || !D.IsValid() || !Door.IsValid()
		|| !StartDoor || !Sill || !WorkLight.IsValid() || !ExitLight.IsValid()
		|| L->GetPanel() != Panel.Get() || D->GetPanel() != Panel.Get()
		|| L->GetCircuitId() != FName(TEXT("L")) || D->GetCircuitId() != FName(TEXT("D")))
	{ Error = TEXT("measured hospital actors/links missing or ambiguous; no nearest-actor or spawn fallback"); return false; }
	const double FixtureTolerance = C.GetDouble(TEXT("FixtureToleranceCm"), -1);
	const TPair<AActor*, const TCHAR*> Guards[] = {{Panel.Get(),TEXT("PanelLocation")},{L.Get(),TEXT("WorkSwitchLocation")},
		{D.Get(),TEXT("DoorSwitchLocation")},{Door.Get(),TEXT("DoorLocation")}};
	for (const auto& Guard : Guards)
	{
		FVector P;
		if (!Point(C, Guard.Value, P, Error))
		{
			return false;
		}

		if (FixtureTolerance <= 0 || !Guard.Key->GetActorLocation().Equals(P, FixtureTolerance))
		{ Error = TEXT("fixture actor moved; refresh measured manifest before running"); return false; }
		const bool IsSwitch=Guard.Key==L.Get()||Guard.Key==D.Get();
		const FVector Scale=IsSwitch?FVector(.15,.25,.35):FVector::OneVector;
		const FQuat Rotation=Guard.Key==Door.Get()?FQuat::Identity:FRotator(0,180,0).Quaternion();
		if(!Guard.Key->GetActorScale3D().Equals(Scale,.001)||!Guard.Key->GetActorQuat().Equals(Rotation,.001))
		{Error=TEXT("fixture rotation/scale differs from placement receipt");return false;}
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_fixture_actor\",\"f\":%lld,\"actor\":%s,\"class\":%s,\"position\":%s,\"rotation\":%s,\"scale\":%s}"),Ctx.Frame,*Q(Guard.Key->GetPathName()),*Q(Guard.Key->GetClass()->GetName()),*V(Guard.Key->GetActorLocation()),*Q(Guard.Key->GetActorRotation().ToString()),*V(Guard.Key->GetActorScale3D())));
	}

	FixtureFact = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("FixtureFact"))), false);
	AllocationTag = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("AllocationCounter"))), false);
	OpenedTag = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("OpenedFact"))), false);
	if (!FixtureFact.IsValid() || FixtureFact.ToString()!=TEXT("Story.Prologue.FirstEncounterDone") || !AllocationTag.IsValid()
		|| !OpenedTag.IsValid()
		|| Panel->HasFault() || Door->HasFault() || Panel->IsOpened() || Door->IsOpen() || Door->IsMoving()
		|| Story->HasFact(OpenedTag) || Story->GetCounter(AllocationTag) != 0)
	{ Error = TEXT("invalid tags or nonfresh/faulted panel/door; no allocation/opened reset"); return false; }
	if (!FParse::Value(FCommandLine::Get(), TEXT("VBPowerPanelView="), ViewVariant)
		|| (ViewVariant != TEXT("Stationary") && ViewVariant != TEXT("Oblique") && ViewVariant != TEXT("Inspect")))
	{ Error = TEXT("select -VBPowerPanelView=Stationary, Oblique or Inspect for a fresh process"); return false; }
	struct FField {const TCHAR* Key; double* Value;};
	const FField Fields[] = {{TEXT("WalkToleranceCm"),&ApproachTolerance},{TEXT("WalkTimeoutSeconds"),&WalkTimeout},
		{TEXT("StateTimeoutSeconds"),&StateTimeout},{TEXT("NeutralSeconds"),&NeutralSeconds},{TEXT("ViewSeconds"),&ViewSeconds},
		{TEXT("PreSeconds"),&PreSeconds},{TEXT("PostSeconds"),&PostSeconds},{TEXT("CameraAlignedDegrees"),&AlignedDegrees},
		{TEXT("PitchToleranceDegrees"),&PitchTolerance},{TEXT("PitchStickMagnitude"),&PitchMagnitude},
		{TEXT("FramingMarginPixels"),&FramingMargin},{TEXT("UpperFloorZ"),&FloorZ},{TEXT("FloorToleranceCm"),&FloorTolerance}};
	for (const auto& Field : Fields)
	{
		FString Text;
		if (!C.RequireString(Field.Key,Text,Error) || !FDefaultValueHelper::ParseDouble(Text,*Field.Value)
			|| !FMath::IsFinite(*Field.Value) || *Field.Value <= 0) { Error = FString::Printf(TEXT("invalid %s"),Field.Key); return false; }
	}

	PitchInputSign = C.GetDouble(TEXT("PitchInputSign"), 0);
	for (const FString& Text : C.GetArray(TEXT("InspectYawOffsetDegrees")))
	{
		double Offset = 0;
		if (!FDefaultValueHelper::ParseDouble(Text, Offset) || FMath::Abs(Offset) <= AlignedDegrees || FMath::Abs(Offset) >= 60)
		{ Error = TEXT("Inspect yaw offsets must lie between the alignment tolerance and 60 degrees"); return false; }
		InspectYawOffsets.Add(Offset);
	}

	if (ViewVariant == TEXT("Inspect") && InspectYawOffsets.IsEmpty())
	{ Error = TEXT("Inspect needs at least one InspectYawOffsetDegrees entry"); return false; }
	if (FMath::Abs(PitchInputSign) != 1 || PitchMagnitude > 1 || PreSeconds < 2 || PostSeconds < 8 || ViewSeconds < 8)
	{ Error = TEXT("invalid look sign/magnitude or capture windows shorter than the spec"); return false; }
	InteractAction = FName(*C.GetString(TEXT("InteractAction"))); JumpAction = FName(*C.GetString(TEXT("JumpAction")));
	if (!VBPlayBotSkills::EnsureAction(Ctx, InteractAction, Error) || !VBPlayBotSkills::EnsureAction(Ctx, JumpAction, Error))
	{
		return false;
	}

	const double Brightness = C.GetDouble(TEXT("ExpectedBrightness"), -1);
	TArray<FString> LabelNames = C.GetArray(TEXT("PanelLabel"));
	if (LabelNames.Num() != 4 || Brightness != 80) { Error = TEXT("four panel label names and fixed80 baseline required"); return false; }
	LabelNames.Add(C.GetString(TEXT("ExitLabelName")));
	for (const FString& LabelName : LabelNames)
	{
		AStaticMeshActor* Label = Cast<AStaticMeshActor>(Find(Ctx.World,LabelName));
		UMaterialInterface* Material = Label ? Label->GetStaticMeshComponent()->GetMaterial(0) : nullptr;
		float Actual = 0;
		if (!Material || !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(TEXT("Brightness"))), Actual)
			|| !FMath::IsNearlyEqual(static_cast<double>(Actual),Brightness))
		{ Error = TEXT("fixed label/material/effective brightness differs; do not tune during capture"); return false; }
		Labels.Add(Label);
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_label\",\"f\":%lld,\"actor\":%s,\"material\":%s,\"brightness\":%.6f}"),Ctx.Frame,*Q(Label->GetPathName()),*Q(Material->GetPathName()),Actual));
	}

	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(),TEXT("VBPlayBotOut="),OutDir) || OutDir.IsEmpty())
	{ Error=TEXT("missing isolated runner output directory"); return false; }
	ExposureFile.Reset(IFileManager::Get().CreateFileWriter(*FPaths::Combine(OutDir,TEXT("power_exposure.csv")),
		FILEWRITE_NoReplaceExisting));
	if (!ExposureFile) { Error=TEXT("cannot create fresh power_exposure.csv"); return false; }
	WriteExposure(TEXT("engine_frame,renderer_frame,view_world_seconds,player_index,buffer_exists,cached_available,cached_exposure,cached_minus_log2_exposure,cached_scene_luminance,method,min_brightness,max_brightness,bias,speed_up,speed_down,physical_camera,bloom,source_frame_known\n"));
	ExposureState=MakeShared<FVBPowerPanelExposureState,ESPMode::ThreadSafe>();
	ExposureObserver=FSceneViewExtensions::NewExtension<FVBPowerPanelExposureView>(Ctx.World,ExposureState.ToSharedRef());
	DoorHandle=Door->OnDoorMoveFinished.AddLambda([this](uint32 Serial,EVBDoorMoveResult Result)
	{
		if (InjectionFrames.Contains(TEXT("panel:D")) && Result==EVBDoorMoveResult::Reached)
		{
			ReachedSerial=Serial;
		}

		PendingDoorEvents.Add(FString::Printf(TEXT("\"engine_frame\":%llu,\"world_seconds\":%.6f,\"serial\":%u,\"result\":%d"),
			GFrameCounter,Door.IsValid()?Door->GetWorld()->GetTimeSeconds():0,Serial,static_cast<int32>(Result)));
	});
	RefusedHandle=Interactor->OnInteractionRefused.AddLambda([this](const FText& Text){ ObservationError=TEXT("normal interaction refused: ")+Text.ToString(); });
	Ctx.OnPressInjected=[this,Emit=Ctx.Emit](const FVBPlayBotPress& Press)
	{
		if (Press.Action!=InteractAction)
		{
			return;
		}

		const bool Correct=UseTarget.IsValid() && Interactor.IsValid() && Interactor->FindBestInteractable()==UseTarget.Get() && Interactor->CanReach(UseTarget.Get());
		bInputTargets &= Correct; InjectionFrames.Add(Press.Cue,Press.InjectedFrame);
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_interact_injected\",\"f\":%lld,\"engine_frame\":%llu,\"cue\":%s,\"correct_selected_target\":%s}"),Press.InjectedFrame,GFrameCounter,*Q(Press.Cue),Correct?TEXT("true"):TEXT("false")));
		if (!Correct)
		{
			ObservationError=TEXT("selected target changed before the actual interaction injection");
		}
	};
	const double Radius=C.GetDouble(TEXT("LocalLightRadiusCm"),-1);
	if (Radius<=0) { Error=TEXT("invalid local light inventory radius"); return false; }
	for (TActorIterator<AActor> It(Ctx.World);It;++It)
	{
		TArray<ULightComponent*> Components; It->GetComponents(Components);
		for (ULightComponent* Light:Components) if (FVector::Dist(Light->GetComponentLocation(),Panel->GetActorLocation())<=Radius)
		{
			Lights.Add(Light);
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_light_inventory\",\"f\":%lld,\"component\":%s,\"type\":%s,\"position\":%s,\"intensity\":%.6f,\"mobility\":%d,\"visible\":%s}"),Ctx.Frame,*Q(Light->GetPathName()),*Q(Light->GetClass()->GetName()),*V(Light->GetComponentLocation()),Light->Intensity,static_cast<int32>(Light->Mobility),Light->IsVisible()?TEXT("true"):TEXT("false")));
		}
	}

	const bool Before=Story->HasFact(FixtureFact);
	Story->SetFact(FixtureFact,true); bPrepared=Story->HasFact(FixtureFact);
	Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"fixture_preparation\",\"f\":%lld,\"engine_frame\":%llu,\"world_seconds\":%.6f,\"actor\":%s,\"world\":%s,\"fixture_only\":true,\"fact\":%s,\"before\":%s,\"after\":%s,\"narrative_progression_proof\":false,\"view_variant\":%s}"),Ctx.Frame,GFrameCounter,Ctx.World->GetTimeSeconds(),*Q(Story->GetPathName()),*Q(Ctx.World->GetPathName()),*Q(FixtureFact.ToString()),Before?TEXT("true"):TEXT("false"),bPrepared?TEXT("true"):TEXT("false"),*Q(ViewVariant)));
	if (!bPrepared) { Error=TEXT("fixture story precondition refused"); return false; }
	Ctx.Human->BeginTrial(0);
	Flow.Then<FVBSkillWaitUntil>(TEXT("actual readiness and panel startup"),[this](FVBPlayBotContext& X)
	{
		return UVBPlayerReadinessComponent::QueryForPawn(X.Player).State==EVBPlayerPreparationState::Ready
			&& L->GetInteractable()->IsActivated() && !D->GetInteractable()->IsActivated() && Panel->GetAllocationIndex()==0;
	},StateTimeout);
	FVector P;
	if (!Point(C,TEXT("StartDoorApproach"),P,Error))
	{
		return false;
	}

	AddWalk(TEXT("start door approach"),P); AddUse(TEXT("route:start_door"),StartDoor);
	Flow.Then<FVBSkillWaitUntil>(TEXT("start door opened normally"),[Start=TWeakObjectPtr<AActor>(StartDoor)](FVBPlayBotContext&){ const auto* I=Start.IsValid()?Start->FindComponentByClass<UVBInteractableComponent>():nullptr;return I&&I->IsActivated();},StateTimeout);
	for (const FString& Text:C.GetArray(TEXT("BeforeWindow")))
	{ if(!P.InitFromString(Text)||P.ContainsNaN()){Error=TEXT("invalid BeforeWindow point");return false;} AddWalk(TEXT("lower corridor/treatment"),P); }
	FVector Direction;
	if (!Point(C,TEXT("WindowDirection"),Direction,Error) || Direction.IsNearlyZero())
	{
		return false;
	}

	Direction=Direction.GetSafeNormal2D();
	const double WindowDot=C.GetDouble(TEXT("WindowFacingDot"),-1), WindowMagnitude=C.GetDouble(TEXT("WindowFacingMagnitude"),
		-1), WindowX=C.GetDouble(TEXT("WindowExitX"),0);
	if(WindowDot<=0||WindowDot>=1||WindowMagnitude<=0||WindowMagnitude>1||WindowX<=0){Error=TEXT("invalid window traversal measurement parameters");return false;}
	Flow.Then<FVBSkillWaitUntil>(TEXT("face sill through real movement"),
		[this,Direction,WindowDot,WindowMagnitude,S=TWeakObjectPtr<AActor>(Sill)](FVBPlayBotContext& X)
	{
		if(!S.IsValid())
		{
			return false;
		}

		const bool Facing=VBPlayBotSkills::TurnCameraToward(X,S.Get(),
			AlignedDegrees)&&FVector::DotProduct(X.Player->GetActorForwardVector(),Direction)>=WindowDot;
		X.Input->SetStickWant(Facing?FVector2D::ZeroVector:X.ToStick(Direction)*WindowMagnitude); return Facing;
	},StateTimeout);
	Flow.Then<FVBSkillPress>(TEXT("route:window_jump"),JumpAction);
	Flow.Then<FVBSkillWaitUntil>(TEXT("normal sill traversal and upper floor"),[this,Direction,WindowX](FVBPlayBotContext& X)
	{
		const double Foot=X.Player->GetActorLocation().Z-X.Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const bool Done=FVBPlayBotContext::IsGrounded(X.Player)&&X.Player->GetActorLocation().X>=WindowX&&FMath::Abs(Foot-FloorZ)<=FloorTolerance;
		X.Input->SetStickWant(Done?FVector2D::ZeroVector:X.ToStick(Direction)*X.Human->GetProfile().MoveMagnitude);return Done;
	},StateTimeout);
	for (const FString& Text:C.GetArray(TEXT("AfterWindow")))
	{ if(!P.InitFromString(Text)||P.ContainsNaN()){Error=TEXT("invalid AfterWindow point");return false;} AddWalk(TEXT("upper ward to P05"),P); }
	Flow.Then<FVBSkillDo>(TEXT("route complete"),[this](FVBPlayBotContext& X,FString&){bRoute=true;Mark(X,TEXT("panel_route_complete"));return true;});
	if(ViewVariant==TEXT("Inspect"))
	{
		// 같은 상태, 입력, 다른 view - P0 와 adapted P1 에서 패널 판 넷, adapted P1 과 real P3 에서 Exit D
		FVector Work,DoorPoint,Gate;
		if(!Point(C,TEXT("WorkOperatingPoint"),Work,Error)||!Point(C,TEXT("DoorOperatingPoint"),DoorPoint,Error)
			||!Point(C,TEXT("GatePoint"),Gate,Error))
		{
			return false;
		}
		// 장면마다 물러설 점(보통 걷기)은 선택이다 - 조작 점에서는 카메라 arm 이 pawn 머리 뒤로 밀려 들 수 있다.
		//   키가 없으면 조작 점에서 찍는다.
		const auto StandBack=[this,&C](const TCHAR* Key,FString& Err){FVector Q;if(!C.GetString(Key).IsEmpty()){if(!Point(C,Key,Q,Err))return false;AddWalk(Key,Q);}return true;};
		AddWalk(TEXT("L operating point"),Work);
		if(!StandBack(TEXT("InspectPointP0Panel"),Error))
		{
			return false;
		}

		AddInspect(TEXT("P0_inspect_panel"),true,ViewSeconds,0);
		AddWalk(TEXT("L operating point"),Work);
		AddUse(TEXT("panel:L"),L.Get());AddHold(TEXT("P1_adapt"),PostSeconds,1,TEXT("panel:L"));
		if(!StandBack(TEXT("InspectPointP1Panel"),Error))
		{
			return false;
		}

		AddInspect(TEXT("P1_inspect_panel"),true,ViewSeconds,1);
		AddWalk(TEXT("D operating point"),DoorPoint);
		if(!StandBack(TEXT("InspectPointP1Exit"),Error))
		{
			return false;
		}

		AddInspect(TEXT("P1_inspect_exit"),false,ViewSeconds,1);
		AddWalk(TEXT("D operating point"),DoorPoint);
		AddUse(TEXT("panel:D"),D.Get());
		Flow.Then<FVBSkillWaitUntil>(TEXT("real P3 latch and reached event"),[this](FVBPlayBotContext&){return ReachedSerial!=0&&Panel->IsOpened()&&Door->IsOpen()&&!Door->IsMoving();},StateTimeout);
		AddWalk(TEXT("P3 gate approach"),Gate);
		if(!StandBack(TEXT("InspectPointP3Exit"),Error))
		{
			return false;
		}

		AddInspect(TEXT("P3_inspect_exit"),false,ViewSeconds,2);
		return true;
	}

	if(!Point(C,TEXT("WorkOperatingPoint"),P,Error))
	{
		return false;
	}

	AddWalk(TEXT("L operating point"),P); AddAim(TEXT("frame L baseline"),L.Get(),true); AddHold(TEXT("P0_fixed"),PreSeconds,0);
	AddUse(TEXT("panel:L"),L.Get()); AddHold(TEXT("P1_close"),PostSeconds,1,TEXT("panel:L"));
	if(!Point(C,TEXT("ObliquePoint"),ObliquePoint,Error))
	{
		return false;
	}

	AddWalk(TEXT("P1 oblique approach"),ObliquePoint); AddAim(TEXT("frame P1 oblique"),Panel.Get(),
		true);AddHold(TEXT("P1_oblique"),ViewSeconds,1);
	if(!Point(C,TEXT("DoorOperatingPoint"),P,Error))
	{
		return false;
	}

	AddWalk(TEXT("D operating point"),P);AddAim(TEXT("frame D baseline"),D.Get(),true);AddHold(TEXT("P1_pre_D"),PreSeconds,1);
	AddUse(TEXT("panel:D"),D.Get());
	if(ViewVariant==TEXT("Oblique")){AddWalk(TEXT("P2 oblique attempt"),ObliquePoint);AddAim(TEXT("frame P2 oblique"),Panel.Get(),true);}
	AddHold(TEXT("D_window"),PostSeconds,-1,TEXT("panel:D"));
	Flow.Then<FVBSkillWaitUntil>(TEXT("real P3 latch and reached event"),[this](FVBPlayBotContext&){return ReachedSerial!=0&&Panel->IsOpened()&&Door->IsOpen()&&!Door->IsMoving();},StateTimeout);
	if(!Point(C,TEXT("GatePoint"),P,Error))
	{
		return false;
	}

	AddWalk(TEXT("P3 gate approach"),P);AddAim(TEXT("frame real exit"),Labels.Last().Get(),false);AddHold(TEXT("P3_exit"),ViewSeconds,2);
	return true;
}

void FVBPlayBotScenario_PowerPanel::AddWalk(FString Name, FVector Point)
{
	Flow.Then<FVBSkillDo>(Name,[this,Name](FVBPlayBotContext& X,FString&){X.Input->SetLookWant(FVector2D::ZeroVector);Mark(X,Name);return true;});
	Flow.Then<FVBSkillApproachPoint>(Name,[Point](FVBPlayBotContext&){return Point;},ApproachTolerance,WalkTimeout);
}

void FVBPlayBotScenario_PowerPanel::AddAim(FString Name, AActor* Target, bool bFramePanel)
{
	Flow.Then<FVBSkillDo>(Name,[this,Name](FVBPlayBotContext& X,FString&){X.Input->SetStickWant(FVector2D::ZeroVector);Mark(X,Name);return true;});
	Flow.Then<FVBSkillWaitUntil>(Name,[this,T=TWeakObjectPtr<AActor>(Target),bFramePanel](FVBPlayBotContext& X)
	{
		if (!T.IsValid())
		{
			return false;
		}

		if (!VBPlayBotSkills::TurnCameraToward(X,T.Get(),AlignedDegrees))
		{
			return false;
		}

		double Delta=0;
		if (!PitchDelta(X,bFramePanel,Delta))
		{
			return false;
		}

		const bool Done=FMath::Abs(Delta)<=PitchTolerance && Framed(X,bFramePanel);
		X.Input->SetLookWant(Done?FVector2D::ZeroVector:FVector2D(0,FMath::Sign(Delta)*PitchMagnitude*PitchInputSign));
		return Done;
	},StateTimeout);
	Flow.Then<FVBSkillWait>(TEXT("settle normal look input"),NeutralSeconds);
}

bool FVBPlayBotScenario_PowerPanel::PitchDelta(FVBPlayBotContext& X, bool bPanel, double& OutDelta) const
{
	FVector Aim=FVector::ZeroVector;
	const int32 Begin=bPanel?0:4, End=bPanel?4:5;
	for (int32 I=Begin;I<End;++I)
	{
		if(!Labels.IsValidIndex(I)||!Labels[I].IsValid())
		{
			return false;
		}

		FVector Origin,Extent;Labels[I]->GetActorBounds(false,Origin,Extent);Aim+=Origin;
	}

	Aim/=End-Begin;
	const auto& POV=X.PC->PlayerCameraManager->GetCameraCacheView();
	OutDelta=FMath::FindDeltaAngleDegrees(POV.Rotation.Pitch,(Aim-POV.Location).Rotation().Pitch);
	return true;
}

void FVBPlayBotScenario_PowerPanel::AddInspect(FString Name, bool bPanel, double Seconds, int32 ExpectedIndex)
{
	Flow.Then<FVBSkillDo>(Name,[this,Name](FVBPlayBotContext& X,FString&)
	{
		X.Input->SetStickWant(FVector2D::ZeroVector);InspectAimStart=X.Frame;Mark(X,Name+TEXT("_aim"));return true;
	});
	Flow.Then<FVBSkillWaitUntil>(Name,[this,bPanel](FVBPlayBotContext& X)
	{
		const AActor* Group=bPanel?static_cast<const AActor*>(Panel.Get()):(Labels.IsValidIndex(4)?Labels[4].Get():nullptr);
		if(!Group)
		{
			return false;
		}
		// 설정한 offset 마다 state timeout 을 똑같이 나눠 시도한다(양쪽, 다음엔 더 넓게) - 벽, 문틀이 한쪽을 막을 수 있고
		//   가까운 조작 점에서는 작은 offset 으로는 pawn 이 여전히 판을 가린다.
		const int64 Share=FMath::Max<int64>(1,X.SecondsToFrames(StateTimeout/InspectYawOffsets.Num()));
		const double Side=InspectYawOffsets[FMath::Min<int64>((X.Frame-InspectAimStart)/Share,InspectYawOffsets.Num()-1)];
		const bool bYaw=VBPlayBotSkills::TurnCameraToward(X,Group,AlignedDegrees,Side);
		double Delta=0;
		const bool bPitchKnown=PitchDelta(X,bPanel,Delta);
		const bool bFramed=Framed(X,bPanel),bClear=ClearOfPawn(X,bPanel);
		// 살펴보기 겨눔이 왜 아직 안 끝났는지 초당 두 번 남긴다(timeout 때 어느 조건인지 알 수 있게)
		if((X.Frame-InspectAimStart)%30==0)
		{
			// capsule 축에 대한 카메라 위치 - 벽에 밀린 spring arm 은 눈을 pawn 안에 두어, 모든 시선이 capsule 안에서 시작한다
			const UCapsuleComponent* Capsule=X.Player->GetCapsuleComponent();
			const FVector Eye=X.PC->PlayerCameraManager->GetCameraCacheView().Location;
			const FVector Centre=Capsule->GetComponentLocation();
			const double Axis=Capsule->GetScaledCapsuleHalfHeight()-Capsule->GetScaledCapsuleRadius();
			const double EyeToAxis=FMath::PointDistToSegment(Eye,Centre-FVector(0,0,Axis),Centre+FVector(0,0,Axis));
			X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_inspect_aim\",\"f\":%lld,\"yaw_offset\":%.1f,\"yaw_aligned\":%s,\"pitch_delta\":%.2f,\"framed\":%s,\"clear_of_pawn\":%s,\"control_yaw\":%.2f,\"eye_to_capsule_axis_cm\":%.1f,\"capsule_radius_cm\":%.1f,\"pawn\":%s,\"eye\":%s}"),
				X.Frame,Side,bYaw?TEXT("true"):TEXT("false"),bPitchKnown?Delta:999.0,bFramed?TEXT("true"):TEXT("false"),bClear?TEXT("true"):TEXT("false"),
				X.PC->GetControlRotation().Yaw,EyeToAxis,Capsule->GetScaledCapsuleRadius(),*VBPlayBotPowerPanel::V(Centre),*VBPlayBotPowerPanel::V(Eye)));
		}

		if(!bYaw||!bPitchKnown)
		{
			return false;
		}

		const bool Done=FMath::Abs(Delta)<=PitchTolerance&&bFramed&&bClear;
		X.Input->SetLookWant(Done?FVector2D::ZeroVector:FVector2D(0,FMath::Sign(Delta)*PitchMagnitude*PitchInputSign));
		return Done;
	},StateTimeout);
	Flow.Then<FVBSkillWait>(TEXT("settle normal look input"),NeutralSeconds);
	InspectFrames.Add(Name,0);
	InspectClear.Add(Name,true);
	AddHold(Name,Seconds,ExpectedIndex);
}

bool FVBPlayBotScenario_PowerPanel::ClearOfPawn(FVBPlayBotContext& X, bool bPanel) const
{
	// 카메라에서 판마다 중심과 bounds 모서리 여덟까지의 시선을 pawn capsule 에만 대 본다.
	//   화면 사각형은 너무 거칠다 - 가까운 조작 점에서는 투영한 capsule 상자가 화면 거의 전체를 덮는다.
	UCapsuleComponent* Body=X.Player?X.Player->GetCapsuleComponent():nullptr;
	if(!Body||!X.PC||!X.PC->PlayerCameraManager)
	{
		return false;
	}

	const FVector Eye=X.PC->PlayerCameraManager->GetCameraCacheView().Location;
	for(int32 I=bPanel?0:4;I<(bPanel?4:5);++I)
	{
		if(!Labels.IsValidIndex(I)||!Labels[I].IsValid())
		{
			return false;
		}

		FVector Origin,Extent;Labels[I]->GetActorBounds(false,Origin,Extent);
		for(int32 Corner=0;Corner<9;++Corner)
		{
			const FVector Point=Corner==8?Origin:Origin+Extent*FVector(Corner&1?1:-1,Corner&2?1:-1,Corner&4?1:-1);
			FHitResult Hit;
			if(Body->LineTraceComponent(Hit,Eye,Point,FCollisionQueryParams(SCENE_QUERY_STAT(VBPlayBotInspect),false)))
			{
				return false;
			}
		}
	}

	return true;
}

bool FVBPlayBotScenario_PowerPanel::Framed(FVBPlayBotContext& X, bool bPanel) const
{
	int32 Width=0,Height=0;X.PC->GetViewportSize(Width,Height);
	if(Width<=0||Height<=0)
	{
		return false;
	}

	for(int32 I=bPanel?0:4;I<(bPanel?4:5);++I)
	{
		if(!Labels.IsValidIndex(I)||!Labels[I].IsValid())
		{
			return false;
		}

		FVector Origin,Extent;Labels[I]->GetActorBounds(false,Origin,Extent);
		for(int32 Corner=0;Corner<8;++Corner)
		{
			const FVector P=Origin+Extent*FVector(Corner&1?1:-1,Corner&2?1:-1,Corner&4?1:-1);
			FVector2D Pixel;
			if(!X.PC->ProjectWorldLocationToScreen(P,Pixel)||Pixel.X<FramingMargin||Pixel.Y<FramingMargin
				||Pixel.X>Width-FramingMargin||Pixel.Y>Height-FramingMargin)
			{
				return false;
			}
		}
	}

	return true;
}

void FVBPlayBotScenario_PowerPanel::AddUse(FString Cue, AActor* Target)
{
	Flow.Then<FVBSkillDo>(Cue,[this,T=TWeakObjectPtr<AActor>(Target),Cue](FVBPlayBotContext& X,FString& Error)
	{
		UseTarget=T.IsValid()?T->FindComponentByClass<UVBInteractableComponent>():nullptr;
		if(!UseTarget.IsValid()||!UseTarget->IsInteractionEnabled()||UseTarget->GetConfig()->Mode!=EVBInteractMode::Tap
			||!UseTarget->PassesUseConditions(X.Player)){Error=TEXT("tap target disabled or use condition refused");return false;}
		bBeforeUse=UseTarget->IsActivated();X.Input->SetStickWant(FVector2D::ZeroVector);Mark(X,Cue);return true;
	});
	Flow.Then<FVBSkillWaitUntil>(TEXT("select actual tap target"),[this,T=TWeakObjectPtr<AActor>(Target)](FVBPlayBotContext& X)
	{
		const bool bSelected=T.IsValid()&&VBPlayBotSkills::TurnCameraToward(X,T.Get(),AlignedDegrees)
			&&UseTarget.IsValid()&&Interactor->CanReach(UseTarget.Get())&&Interactor->FindBestInteractable()==UseTarget.Get();
		if(bSelected)
		{
			X.Input->SetLookWant(FVector2D::ZeroVector);
		}

		return bSelected;
	},StateTimeout);
	Flow.Then<FVBSkillWait>(TEXT("neutral before tap"),NeutralSeconds);
	Flow.Then<FVBSkillPress>(Cue,InteractAction);
	Flow.Then<FVBSkillWaitUntil>(TEXT("tap acknowledged by real state change"),[this](FVBPlayBotContext&)
	{return UseTarget.IsValid()&&UseTarget->IsActivated()!=bBeforeUse;},StateTimeout);
}

void FVBPlayBotScenario_PowerPanel::AddHold(FString Name, double Seconds, int32 ExpectedIndex, FString FromCue)
{
	Flow.Then<FVBSkillDo>(Name,[this,Name,FromCue](FVBPlayBotContext& X,FString& Error)
	{
		HoldStart=X.Frame;
		if(!FromCue.IsEmpty())
		{
			const int64* Injection=InjectionFrames.Find(FromCue);
			if(!Injection){Error=TEXT("missing actual input injection anchor");return false;}HoldStart=*Injection;
		}

		X.Input->SetStickWant(FVector2D::ZeroVector);X.Input->SetLookWant(FVector2D::ZeroVector);Mark(X,Name);return true;
	});
	Flow.Then<FVBSkillWaitUntil>(Name,[this,Seconds,ExpectedIndex](FVBPlayBotContext& X)
	{
		const bool Valid=ExpectedIndex<0?Panel->GetAllocationIndex()==2:Panel->GetAllocationIndex()==ExpectedIndex;
		if(!Valid||Door->IsBlocked()||Door->HasFault()||Panel->HasFault())
		{ObservationError=TEXT("unexpected panel/door state during requested hold");return false;}
		return X.Frame-HoldStart>=X.SecondsToFrames(Seconds);
	},Seconds+StateTimeout);
}

void FVBPlayBotScenario_PowerPanel::Mark(FVBPlayBotContext& X, const FString& Name)
{
	Phase=Name;
	X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_phase\",\"f\":%lld,\"engine_frame\":%llu,\"world_seconds\":%.6f,\"name\":%s}"),X.Frame,GFrameCounter,X.World->GetTimeSeconds(),*VBPlayBotPowerPanel::Q(Name)));
}

void FVBPlayBotScenario_PowerPanel::Sample(FVBPlayBotContext& X)
{
	using namespace VBPlayBotPowerPanel;
	const int32 Index=Panel->GetAllocationIndex();
	const bool Work=L->GetInteractable()->IsActivated(),Exit=D->GetInteractable()->IsActivated();
	const bool WorkVisible=WorkLight->IsVisible(),ExitVisible=ExitLight->IsVisible();
	const bool Opened=Panel->IsOpened(),Moving=Door->IsMoving(),Open=Door->IsOpen();
	const bool P0=Index==0&&Work&&!Exit&&WorkVisible&&!ExitVisible&&!Opened&&!Moving&&!Open;
	const bool P1=Index==1&&!Work&&!Exit&&!WorkVisible&&!ExitVisible&&!Opened&&!Moving&&!Open;
	const bool P2=Index==2&&!Work&&Exit&&!WorkVisible&&ExitVisible&&!Opened&&Moving;
	const bool P3=Index==2&&!Work&&Exit&&!WorkVisible&&ExitVisible&&Opened&&!Moving&&Open;
	if(bRoute)
	{
		bP0|=P0;
		bP1|=P1;
		bP2|=P2;
		bP3|=P3;
	}

	const bool PanelHold=Phase==TEXT("P0_fixed")||Phase==TEXT("P1_close")||Phase==TEXT("P1_oblique")||Phase==TEXT("P1_pre_D")
		||(Phase==TEXT("D_window")&&ViewVariant==TEXT("Stationary"));
	const bool PanelInFrame=Framed(X,true),ExitInFrame=Framed(X,false);
	if(PanelHold)
	{
		++PanelHoldFrames;
		bPanelFramed&=PanelInFrame;
	}

	if(Phase==TEXT("P3_exit"))
	{
		++ExitHoldFrames;
		bExitFramed=ExitHoldFrames==1?ExitInFrame:bExitFramed&&ExitInFrame;
	}

	bObliqueP2|=bRoute&&P2&&FVector::Dist2D(X.Player->GetActorLocation(),ObliquePoint)<=ApproachTolerance&&PanelInFrame;
	if(int64* Frames=InspectFrames.Find(Phase))
	{
		const bool bPanelGroup=Phase.Contains(TEXT("panel"));
		const bool bInFrame=bPanelGroup?PanelInFrame:ExitInFrame, bClear=ClearOfPawn(X,bPanelGroup);
		++*Frames;
		InspectClear[Phase]&=bInFrame&&bClear;
		X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_inspect\",\"f\":%lld,\"engine_frame\":%llu,\"phase\":%s,\"framed\":%s,\"clear_of_pawn\":%s}"),
			X.Frame,GFrameCounter,*Q(Phase),bInFrame?TEXT("true"):TEXT("false"),bClear?TEXT("true"):TEXT("false")));
	}

	if(Panel->HasFault()||Door->HasFault()||Door->IsBlocked())
	{
		ObservationError=TEXT("real panel/door fault or obstruction");
	}

	if(Story->GetCounter(AllocationTag)!=Index||Story->HasFact(OpenedTag)!=Opened)
	{
		ObservationError=TEXT("story/panel state disagrees");
	}

	FString Leaf=TEXT("null");
	TArray<UStaticMeshComponent*> Meshes;Door->GetComponents(Meshes);
	for(const auto* Mesh:Meshes)if(Mesh->GetFName()==FName(TEXT("Leaf")))
		Leaf=FString::Printf(TEXT("{\"position\":%s,\"rotation\":%s,\"scale\":%s}"),*V(Mesh->GetComponentLocation()),*Q(Mesh->GetComponentRotation().ToString()),*V(Mesh->GetComponentScale()));
	X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_sample\",\"f\":%lld,\"engine_frame\":%llu,\"world_seconds\":%.6f,\"phase\":%s,\"index\":%d,\"L\":%s,\"D\":%s,\"work_light\":%s,\"exit_light\":%s,\"moving\":%s,\"door_open\":%s,\"latched\":%s,\"panel_framed\":%s,\"exit_framed\":%s,\"leaf\":%s}"),
		X.Frame,GFrameCounter,X.World->GetTimeSeconds(),*Q(Phase),Index,Work?TEXT("true"):TEXT("false"),Exit?TEXT("true"):TEXT("false"),WorkVisible?TEXT("true"):TEXT("false"),ExitVisible?TEXT("true"):TEXT("false"),Moving?TEXT("true"):TEXT("false"),Open?TEXT("true"):TEXT("false"),Opened?TEXT("true"):TEXT("false"),PanelInFrame?TEXT("true"):TEXT("false"),ExitInFrame?TEXT("true"):TEXT("false"),*Leaf));
	for(const FString& Payload:PendingDoorEvents)
		X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_door_finished\",\"f\":%lld,%s}"),X.Frame,*Payload));
	PendingDoorEvents.Reset();
	for(auto Weak:Lights)if(Weak.IsValid())
	{
		const bool Visible=Weak->IsVisible();const bool* Previous=LightVisible.Find(Weak);
		if(!Previous||*Previous!=Visible)
			X.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"power_light_visibility\",\"f\":%lld,\"engine_frame\":%llu,\"component\":%s,\"visible\":%s}"),X.Frame,GFrameCounter,*Q(Weak->GetPathName()),Visible?TEXT("true"):TEXT("false")));
		LightVisible.Add(Weak,Visible);
	}
}

void FVBPlayBotScenario_PowerPanel::WriteExposure(const FString& Line)
{
	if(!ExposureFile)
	{
		return;
	}

	FTCHARToUTF8 Bytes(*Line);ExposureFile->Serialize(const_cast<ANSICHAR*>(Bytes.Get()),Bytes.Length());
	if(ExposureFile->IsError())
	{
		ObservationError=TEXT("power_exposure.csv write failed");
	}
}

void FVBPlayBotScenario_PowerPanel::DrainExposure(FVBPlayBotContext&)
{
	if(!ExposureState)
	{
		return;
	}

	FVBPowerExposureSample S;
	while(ExposureState->Queue.Dequeue(S))
	{
		const bool Valid=S.bBuffer&&FMath::IsFinite(S.CachedExposure)&&S.CachedExposure>0;
		const auto Safe=[](float Value){return FMath::IsFinite(Value)?Value:0.f;};
		WriteExposure(FString::Printf(TEXT("%llu,%u,%.6f,%d,%d,%d,%.9f,%.9f,%.9f,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%.6f,0\n"),
			S.EngineFrame,S.RendererFrame,S.WorldSeconds,S.PlayerIndex,S.bBuffer,Valid,Safe(S.CachedExposure),Valid?-FMath::Log2(S.CachedExposure):0.f,
			Safe(S.CachedLuminance),S.Method,Safe(S.Min),Safe(S.Max),Safe(S.Bias),Safe(S.SpeedUp),Safe(S.SpeedDown),S.bPhysical,Safe(S.Bloom)));
		++ExposureRows;if(Valid)++ValidExposureRows;
	}
}

EVBPlayBotState FVBPlayBotScenario_PowerPanel::Tick(FVBPlayBotContext& X)
{
	if(bFinished)
	{
		return EVBPlayBotState::Succeeded;
	}

	if(FirstFrame<0)
	{
		FirstFrame=X.Frame;
	}

	DrainExposure(X);
	if(!Panel.IsValid()||!L.IsValid()||!D.IsValid()||!Door.IsValid()||!Story.IsValid()||!Interactor.IsValid()
		||!WorkLight.IsValid()||!ExitLight.IsValid()||!X.Player||!X.PC||!X.PC->PlayerCameraManager)
	{
		return Finish(X,TEXT("required fixture/player lost"));
	}

	Sample(X);
	if(!ObservationError.IsEmpty())
	{
		return Finish(X,ObservationError);
	}

	const auto Result=Flow.Tick(X);
	if(Result==EVBPlayBotState::Failed)
	{
		return Finish(X,Flow.GetFailure());
	}

	if(Result==EVBPlayBotState::Succeeded)
	{
		return Finish(X,FString());
	}

	return EVBPlayBotState::Running;
}

EVBPlayBotState FVBPlayBotScenario_PowerPanel::Finish(FVBPlayBotContext& X, FString Reason)
{
	using namespace VBPlayBotPowerPanel;
	DrainExposure(X);
	if(ExposureFile)
	{
		ExposureFile->Flush();
	}

	if(Reason.IsEmpty()&&!ObservationError.IsEmpty())
	{
		Reason=ObservationError;
	}

	bFinished=true;X.Input->ReleaseAll(X.Frame);
	X.Checks->Expect(TEXT("PowerPanelExposure.Course"),Reason.IsEmpty()&&bPrepared&&bRoute,true,X.Frame,
		FString::Printf(TEXT("{\"reason\":%s,\"variant\":%s,\"fixture_only\":true}"),*Q(Reason),*Q(ViewVariant)));
	X.Checks->Expect(TEXT("PowerPanelExposure.P0P1P2P3"),bP0&&bP1&&bP2&&bP3&&ReachedSerial!=0,true,X.Frame);
	X.Checks->Expect(TEXT("PowerPanelExposure.InputTargets"),
		bInputTargets&&InjectionFrames.Contains(TEXT("panel:L"))&&InjectionFrames.Contains(TEXT("panel:D")),true,X.Frame);
	if(ViewVariant==TEXT("Inspect"))
	{
		bool bAll=InspectFrames.Num()==4;
		FString Detail;
		for(const auto& Pair:InspectFrames)
		{
			const bool bClear=InspectClear.FindRef(Pair.Key);
			bAll&=Pair.Value>0&&bClear;
			Detail+=FString::Printf(TEXT("%s%s:{\"frames\":%lld,\"framed_and_clear\":%s}"),Detail.IsEmpty()?TEXT(""):TEXT(","),*Q(Pair.Key),Pair.Value,bClear?TEXT("true"):TEXT("false"));
		}

		X.Checks->Expect(TEXT("PowerPanelExposure.InspectClearOfPawn"),bAll,true,X.Frame,TEXT("{")+Detail+TEXT("}"));
	}
	else
	{
		X.Checks->Expect(TEXT("PowerPanelExposure.PanelFraming"),PanelHoldFrames>0&&ExitHoldFrames>0&&bPanelFramed&&bExitFramed,
			true,X.Frame);
	}

	if(ViewVariant==TEXT("Oblique"))
	{
		X.Checks->Expect(TEXT("PowerPanelExposure.ObliqueP2"),bObliqueP2,true,X.Frame);
	}

	if(ValidExposureRows>0)X.Checks->Expect(TEXT("PowerPanelExposure.ExposureReadback"),ExposureFile&&!ExposureFile->IsError(),true,X.Frame,
		FString::Printf(TEXT("{\"rows\":%lld,\"cached_valid_rows\":%lld,\"source_frame_known\":false}"),ExposureRows,ValidExposureRows));
	else
	{
		X.Checks->Unavailable(TEXT("PowerPanelExposure.ExposureReadback"),X.Frame,
			TEXT("main-view API returned no valid cached adaptation exposure"));
	}

	X.Checks->Unavailable(TEXT("PowerPanelExposure.CurrentFrameEV100"),X.Frame,
		TEXT("public API is a cached multiplier, source GPU frame/absolute EV100 unavailable"));
	X.Checks->Unavailable(TEXT("PowerPanelExposure.Convergence"),X.Frame,
		TEXT("cached samples and an 8s window cannot establish adaptation convergence"));
	X.Checks->Look(TEXT("PowerPanelExposure.ReadabilityHaloRoomFill"),FirstFrame>=0?FirstFrame:X.Frame,X.Frame,
		TEXT("fixture-prepared real P0-P3 at fixed label brightness80; inspect adapted/transition imagery and full plate coverage; no matched labels-off GI control"));
	StopObservation();
	return EVBPlayBotState::Succeeded;
}

void FVBPlayBotScenario_PowerPanel::StopObservation()
{
	if(ExposureState)
	{
		ExposureState->bActive.Store(false);
	}

	ExposureObserver.Reset();
	if(ExposureFile)
	{
		ExposureFile->Flush();
		ExposureFile.Reset();
	}

	ExposureState.Reset();
}

void FVBPlayBotScenario_PowerPanel::Teardown(FVBPlayBotContext& X)
{
	if(Door.IsValid()&&DoorHandle.IsValid())
	{
		Door->OnDoorMoveFinished.Remove(DoorHandle);
	}

	if(Interactor.IsValid()&&RefusedHandle.IsValid())
	{
		Interactor->OnInteractionRefused.Remove(RefusedHandle);
	}

	DoorHandle.Reset();RefusedHandle.Reset();X.OnPressInjected=nullptr;
	if(X.Input)
	{
		X.Input->ReleaseAll(X.Frame);
	}

	StopObservation();
}
