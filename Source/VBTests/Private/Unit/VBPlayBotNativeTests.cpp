// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "PlayBot/VBPlayBotRecorder.h"
#include "PlayBot/VBPlayBotClock.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotHospitalOpening.h"
#include "Unit/VBPlayBotInputFixture.h"
#include "GameFramework/Character.h"
#include "Interaction/VBPowerSwitch.h"
#include "Interaction/VBInteractableComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeClockTest, "VB.Unit.PlayBot.Clock.NativeMonotonic",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeClockTest::RunTest(const FString& Parameters)
{
	FVBPlayBotClock Clock;
	TestTrue(TEXT("clock begins at supplied monotonic origin"), Clock.Advance(100.0));
	TestEqual(TEXT("elapsed origin"), Clock.Seconds(), 0.0);
	TestTrue(TEXT("irregular tick accepted"), Clock.Advance(100.037));
	TestTrue(TEXT("milliseconds use actual delta"), FMath::IsNearlyEqual(Clock.DeltaMilliseconds(), 37.0, 1.e-6));
	TestTrue(TEXT("long pause remains elapsed real time"), Clock.Advance(103.0));
	TestEqual(TEXT("pause is not a synthetic frame count"), Clock.Seconds(), 3.0);
	TestFalse(TEXT("backward clock rejected"), Clock.Advance(102.0));
	TestEqual(TEXT("failure preserves last timestamp"), Clock.Seconds(), 3.0);
	TestFalse(TEXT("negative clock rejected"), Clock.Advance(-1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeInputTest, "VB.Unit.PlayBot.InputDriver.NativeDeadlines",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeInputTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate exists for actual input dispatch"), FSlateApplication::IsInitialized())) { return false; }
	FVBPlayBotInputDriver Native(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotInputDriver Legacy;
	FVBPlayBotPress Press;
	Press.Key = EKeys::F12;
	Press.DueFrame = 2;
	TestEqual(TEXT("native refuses frame-only press"), Native.Schedule(Press), INDEX_NONE);
	Press.DueSeconds = 0.25;
	Press.HoldSeconds = 0.15;
	TestEqual(TEXT("legacy refuses native press"), Legacy.Schedule(Press), INDEX_NONE);
	int32 Downs = 0;
	int32 Ups = 0;
	int32 Cancels = 0;
	double ActualDown = -1.0;
	double ActualUp = -1.0;
	Native.OnButton = [&](const FVBPlayBotPress& Event, bool bDown)
	{
		if (!Event.Cancel.IsEmpty()) { ++Cancels; }
		else if (bDown) { ++Downs; ActualDown = Event.InjectedSeconds; }
		else { ++Ups; ActualUp = Event.ReleasedSeconds; }
	};
	const int32 Id = Native.Schedule(Press);
	TestTrue(TEXT("native schedule accepted"), Id != INDEX_NONE);
	TestTrue(TEXT("first irregular sample"), Native.TickNative(100, 0.1, 100.0, 50.0));
	TestTrue(TEXT("large frame identity cannot accelerate time"), Native.TickNative(9000, 0.2, 100.0, 50.0));
	TestEqual(TEXT("not injected before seconds deadline"), Downs, 0);
	TestTrue(TEXT("late dispatch records actual time"), Native.TickNative(9001, 0.27, 70.0, 50.0));
	TestEqual(TEXT("one injected edge"), Downs, 1);
	TestEqual(TEXT("actual delivery timestamp"), ActualDown, 0.27);
	TestTrue(TEXT("held by seconds despite frame jump"), Native.TickNative(900000, 0.3, 30.0, 50.0));
	TestEqual(TEXT("not released by synthetic frame age"), Ups, 0);
	TestTrue(TEXT("hold expires at actual injection plus duration"), Native.TickNative(900001, 0.43, 130.0, 50.0));
	TestEqual(TEXT("one released edge"), Ups, 1);
	TestEqual(TEXT("actual release timestamp"), ActualUp, 0.43);
	TestFalse(TEXT("backward native input clock rejected"), Native.TickNative(900002, 0.4, 0.0, 50.0));
	TestFalse(TEXT("fixed Tick cannot drive native mode"), Native.Tick(900003, 16.0, 50.0));
	Press.DueSeconds = 0.5;
	const int32 HeldId = Native.Schedule(Press);
	Native.TickNative(900004, 0.51, 80.0, 50.0);
	Press.DueSeconds = 5.0;
	const int32 PendingId = Native.Schedule(Press);
	Native.ReleaseAll(900005);
	TestFalse(TEXT("world teardown drops held press"), Native.IsActive(HeldId));
	TestFalse(TEXT("world teardown cancels future press"), Native.IsActive(PendingId));
	TestEqual(TEXT("pending cancellation reported"), Cancels, 1);
	TestEqual(TEXT("held key is released during teardown"), Ups, 2);
	Press = FVBPlayBotPress(); Press.Key = EKeys::F12; Press.DueFrame = 12; Press.HoldFrames = 2;
	const int32 LegacyId = Legacy.Schedule(Press);
	TestTrue(TEXT("legacy schedule unchanged"), LegacyId != INDEX_NONE);
	TestTrue(TEXT("legacy before deadline"), Legacy.Tick(11, 16.0, 50.0));
	TestTrue(TEXT("legacy deadline"), Legacy.Tick(12, 16.0, 50.0));
	TestTrue(TEXT("legacy hold"), Legacy.IsActive(LegacyId));
	Legacy.Tick(14, 16.0, 50.0);
	TestFalse(TEXT("legacy frame release unchanged"), Legacy.IsActive(LegacyId));
	Legacy.ReleaseAll(15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotOpeningOracleTest, "VB.Unit.PlayBot.Hospital.OpeningOracle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotOpeningOracleTest::RunTest(const FString& Parameters)
{
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(), 177);
	FString Error;
	FVBPlayBotHospitalActions Actions;
	FVBPlayBotOpeningView View;
	View.bActivated = true; View.bTargetComplete = true;
	TestTrue(TEXT("already-open target cannot prove an input route"),
		Actions.Tick(View, 1.0, Input, Human, Error) == EVBPlayBotOpeningResult::Failed);
	FVBPlayBotHospitalActions MissingMapping;
	View.bActivated = false; View.bTargetComplete = false; View.bReady = true; View.bFocused = true; View.bGrounded = true;
	TestTrue(TEXT("missing input mapping stops instead of completing"),
		MissingMapping.Tick(View, 1.0, Input, Human, Error) == EVBPlayBotOpeningResult::Failed);
	FVBPlayBotHospitalActions Waiting;
	View.bReady = false; View.bFocused = false;
	TestTrue(TEXT("time alone never completes an unopened target"),
		Waiting.Tick(View, 1000.0, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotRouteConfigTest, "VB.Unit.PlayBot.Hospital.RouteConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotRouteConfigTest::RunTest(const FString& Parameters)
{
	TArray<FVBPlayBotOpeningStep> Steps;
	FString Error;
	const TArray<FString> Valid = {
		TEXT("Door|Interact|Prologue.StartDoor|/Script/VB.VBToggleProp|Activated|20"),
		TEXT("Passed|Walk|779,280,10|20"),
		TEXT("Ledge|Jump|1770,1960,90|20"),
		TEXT("Weapon|Interact|Prologue.OldBladePickup|/Script/VB.VBWeaponPickup|WeaponAcquired|20"),
		TEXT("Draw|Draw|OneHandSword|Input.Attack.Light|20"),TEXT("Pair|Fight|FirstEncounter|120"),
		TEXT("Complete|Observe|Story.Prologue.FirstEncounterDone|20"),
		TEXT("PowerOff|InteractActor|/Game/Test.Test:PersistentLevel.Switch|/Script/VB.VBPowerSwitch|Deactivated|20") };
	TestTrue(TEXT("declared input route parses"), FVBPlayBotOpeningStep::Parse(Valid, Steps, Error));
	if (!TestEqual(TEXT("all declared step kinds retained"), Steps.Num(), 8)) { return false; }
	TestTrue(TEXT("weapon requires acquisition evidence"), Steps[3].bWeaponAcquisition);
	TestTrue(TEXT("stateless target retains exact actor path and off result"),Steps[7].SaveKey.IsNone() && !Steps[7].TargetPath.IsEmpty() && !Steps[7].bTargetActivated);
	TestEqual(TEXT("draw retains its declared normal input"),Steps[4].DrawAction,FName(TEXT("Input.Attack.Light")));
	TestTrue(TEXT("jump keeps explicit ground goal"), Steps[2].Kind == EVBPlayBotOpeningAction::Jump && Steps[2].Point.Z == 90.0);
	for (const FString& Invalid : { FString(TEXT("A|Teleport|0,0,0|20")), FString(TEXT("A|Walk|1,2|20")),
		FString(TEXT("A|Walk|1,2,3|0")), FString(TEXT("A|Walk|1,2,3|121")),
		FString(TEXT("A|Interact||/Script/VB.VBToggleProp|Activated|20")),
		FString(TEXT("A|Interact|Key|Class|AssumeSuccess|20")),
		FString(TEXT("A|Draw|OneHandSword|20")), FString(TEXT("A|Draw|OneHandSword||20")),
		FString(TEXT("A|Draw|OneHandSword|Story.Prologue.Woke|20")),
		FString(TEXT("A|Draw|OneHandSword|Input.UnknownNativeDraw|20")),
		FString(TEXT("A|InteractActor|None|/Script/VB.VBPowerSwitch|Deactivated|20")),
		FString(TEXT("A|InteractActor|Switch|/Script/VB.VBPowerSwitch|Activated|20")) })
	{
		TestFalse(TEXT("invalid route fails before replacing output"), FVBPlayBotOpeningStep::Parse({Invalid},Steps,Error));
		TestEqual(TEXT("previous valid route remains intact"), Steps.Num(),8);
	}
	TestFalse(TEXT("empty route fails"), FVBPlayBotOpeningStep::Parse({},Steps,Error));
	TestFalse(TEXT("duplicate step identities fail"), FVBPlayBotOpeningStep::Parse({Valid[0],Valid[0]},Steps,Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotRouteArrivalTest, "VB.Unit.PlayBot.Hospital.RouteArrival",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotRouteArrivalTest::RunTest(const FString& Parameters)
{
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(),177);
	FVBPlayBotHospitalActions Walk;
	Walk.Step.Kind = EVBPlayBotOpeningAction::Walk;
	Walk.Step.Point = FVector(100,100,90);
	FVBPlayBotOpeningView View;
	View.bReady = true; View.bGrounded = true; View.Feet = FVector(100,100,10);
	FString Error;
	TestTrue(TEXT("same XY on lower floor is not arrival"), Walk.Tick(View,1.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("time alone cannot pass lower floor"), Walk.Tick(View,10.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	View.Feet.Z = 90; View.bGrounded = false;
	TestTrue(TEXT("airborne crossing is not landing"), Walk.Tick(View,11.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	View.bGrounded = true;
	TestTrue(TEXT("ground arrival begins settle"), Walk.Tick(View,12.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("ground arrival must remain stable"), Walk.Tick(View,12.2,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("stable ground arrival completes"), Walk.Tick(View,12.4,Input,Human,Error) == EVBPlayBotOpeningResult::PartialComplete);
	FVBPlayBotHospitalActions Jump;
	Jump.Step.Kind = EVBPlayBotOpeningAction::Jump; Jump.Step.Point = View.Feet;
	View.FacingYaw = 90;
	TestTrue(TEXT("position alone cannot prove a jump"), Jump.Tick(View,20.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("time alone cannot prove an injected jump and landing"), Jump.Tick(View,25.0,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeFinalizeTest, "VB.Unit.PlayBot.Recorder.NativeFinalize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeFinalizeTest::RunTest(const FString& Parameters)
{
	if (!TestFalse(TEXT("no unrelated screenshot request is owned"), FScreenshotRequest::IsScreenshotRequested())) { return false; }
	FVBPlayBotRecorder Recorder;
	FVBPlayBotNativeCapture Limits;
	Limits.StartSeconds = FPlatformTime::Seconds() - 0.5;
	Limits.IntervalSeconds = 0.01; Limits.MaxGapSeconds = 3.0;
	Limits.MaxFrames = 4; Limits.MaxBytes = 1024 * 1024;
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/PlayBotNative"), FGuid::NewGuid().ToString());
	FString Error;
	if (!TestTrue(TEXT("native recorder opens unique evidence directory"), Recorder.OpenNative(Dir,Limits,Error))) { AddError(Error); return false; }
	Recorder.StartCapture();
	Recorder.RequestNativeFrame(1);
	TArray<FColor> Pixels; Pixels.Init(FColor::Black,272*8);
	UGameViewportClient::OnScreenshotCaptured().Broadcast(272,8,Pixels);
	FScreenshotRequest::Reset();
	TestTrue(TEXT("callback leaves owned worker evidence pending"), Recorder.HasPendingNativeFrame());
	TestEqual(TEXT("terminal counter cannot precede worker completion"), Recorder.GetCapturedFrames(), int64(0));
	TestTrue(TEXT("finalizer drains the actual encoder worker"), Recorder.FinalizeNativeCapture());
	TestFalse(TEXT("no capture work remains"), Recorder.HasPendingNativeFrame());
	TestEqual(TEXT("terminal count includes last async PNG"), Recorder.GetCapturedFrames(), int64(1));
	TestTrue(TEXT("finalization is idempotent"), Recorder.FinalizeNativeCapture());
	Recorder.WriteLine(TEXT("{\"k\":\"test_terminal\",\"captures\":1}"));
	Recorder.Close();
	FString Telemetry;
	TestTrue(TEXT("closed telemetry can be read"), FFileHelper::LoadFileToString(Telemetry,*FPaths::Combine(Dir,TEXT("telemetry.jsonl"))));
	TestTrue(TEXT("frame evidence precedes terminal record"), Telemetry.Find(TEXT("native_cap")) >= 0
		&& Telemetry.Find(TEXT("native_cap")) < Telemetry.Find(TEXT("test_terminal")));
	TestTrue(TEXT("async worker produced its owned PNG"), FPaths::FileExists(FPaths::Combine(Dir,TEXT("native/frame000000.png"))));
	FVBPlayBotRecorder Undelivered;
	if (!TestTrue(TEXT("undelivered-request recorder opens"), Undelivered.OpenNative(Dir+TEXT("_undelivered"),Limits,Error))) { return false; }
	Undelivered.StartCapture(); Undelivered.RequestNativeFrame(1);
	TestFalse(TEXT("an undelivered screenshot cannot finalize successfully"), Undelivered.FinalizeNativeCapture());
	TestTrue(TEXT("missing screenshot has an explicit cause"), Undelivered.GetFailure().Contains(TEXT("before its requested screenshot arrived")));
	TestFalse(TEXT("owned pending screenshot is canceled"), FScreenshotRequest::IsScreenshotRequested());
	Undelivered.Close();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotDrawObserveTest,"VB.Unit.PlayBot.Hospital.DrawObserveOracle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotDrawObserveTest::RunTest(const FString& Parameters)
{
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds); FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(),177); FString Error;
	FVBPlayBotHospitalActions Draw; Draw.Step.Kind = EVBPlayBotOpeningAction::Draw; Draw.EffectTimeout = 8;
	FVBPlayBotOpeningView V; V.bReady = V.bGrounded = V.bWeaponOwned = V.bWeaponDrawn = true;
	TestTrue(TEXT("already drawn cannot prove a normal draw input"), Draw.Tick(V,1,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
	VBPlayBotInputDriverTest::FMappingFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Attack = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>();
	Pad->MapKey(Attack,EKeys::Gamepad_RightShoulder); Keyboard->MapKey(Attack,EKeys::LeftMouseButton);
	if (!Fixture.Configure(*this,Pad,Keyboard)) { return false; }
	Fixture.Activate(Pad,ECommonInputType::Gamepad);
	FVBPlayBotHospitalActions DeclaredDraw;
	DeclaredDraw.Step.Kind = EVBPlayBotOpeningAction::Draw;
	DeclaredDraw.Step.DrawAction = TEXT("Input.Attack.Light"); DeclaredDraw.EffectTimeout = 8;
	if (!TestTrue(TEXT("actual pad attack mapping resolves for draw"),Input.ResolveGamepadAction(Fixture.Player.Controller,DeclaredDraw.Step.DrawAction,Attack,Error))) { AddError(Error); return false; }
	int32 Edges = 0;
	Input.OnButton = [&](const FVBPlayBotPress& Press,bool bDown) {
		if (bDown) { ++Edges; TestEqual(TEXT("draw uses declared attack action"),Press.Action,DeclaredDraw.Step.DrawAction); DeclaredDraw.NotifyInjected(Press.Id); }
	};
	V.bWeaponDrawn = false;
	TestTrue(TEXT("owned sheathed sword schedules normal draw"),DeclaredDraw.Tick(V,2,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("native clock delivers actual Slate edge"),Input.TickNative(1,3,1000,50));
	TestEqual(TEXT("exactly one real declared press"),Edges,1);
	V.bWeaponDrawn = true;
	TestTrue(TEXT("actual draw begins settle"),DeclaredDraw.Tick(V,3.1,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("actual draw stable result passes"),DeclaredDraw.Tick(V,3.5,Input,Human,Error) == EVBPlayBotOpeningResult::PartialComplete);
	Input.ReleaseAll(2,3.6);
	FVBPlayBotHospitalActions Observe; Observe.Step.Kind = EVBPlayBotOpeningAction::Observe;
	V.bObservedFact = false;
	TestTrue(TEXT("time cannot manufacture the completion fact"), Observe.Tick(V,1000,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	V.bObservedFact = true; V.bReady = false;
	TestTrue(TEXT("observed fact does not skip startup readiness"), Observe.Tick(V,1001,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	V.bReady = true;
	TestTrue(TEXT("actual observed fact can complete its wait"), Observe.Tick(V,1002,Input,Human,Error) == EVBPlayBotOpeningResult::PartialComplete);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotInteractionTransitionTest,"VB.Unit.PlayBot.Hospital.InteractionTransition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotInteractionTransitionTest::RunTest(const FString& Parameters)
{
	VBPlayBotInputDriverTest::FMappingFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Use = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>();
	Pad->MapKey(Use,EKeys::Gamepad_DPad_Up);
	if (!Fixture.Configure(*this,Pad,Keyboard)) { return false; }
	Fixture.Activate(Pad,ECommonInputType::Gamepad);
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(),177);
	FString Error;
	if (!Input.ResolveGamepadAction(Fixture.Player.Controller,TEXT("Input.Interact"),Use,Error)) { AddError(Error); return false; }
	ACharacter* Character = Fixture.Player.World->SpawnActor<ACharacter>();
	AVBPowerSwitch* Switch = Fixture.Player.World->SpawnActor<AVBPowerSwitch>();
	if (!TestNotNull(TEXT("transient character"),Character) || !TestNotNull(TEXT("transient stateless switch"),Switch)) { return false; }
	AController* FixtureController = Fixture.Player.Controller;
	FixtureController->SetPawn(Character);
	for (const bool Desired : {false,true})
	{
		FVBPlayBotOpeningStep Target; Target.TargetPath = Switch->GetPathName();
		Target.ClassPath = AVBPowerSwitch::StaticClass()->GetPathName(); Target.bTargetActivated = Desired;
		TestTrue(TEXT("real switch intentionally has no save key"),Switch->GetInteractable()->GetSaveKey().IsNone());
		Switch->GetInteractable()->SetActivated(!Desired);
		FVBPlayBotHospitalObservation Observation;
		TestTrue(TEXT("exact stateless target binds from either opposite state"),Observation.Bind(Fixture.Player.World,Target,Error));
		Target.SaveKey = TEXT("Unrelated");
		TestFalse(TEXT("ambiguous identity is rejected"),Observation.Bind(Fixture.Player.World,Target,Error));
		Target.SaveKey = NAME_None;
		Switch->GetInteractable()->SetActivated(Desired);
		TestFalse(TEXT("binding rejects an already satisfied target"),Observation.Bind(Fixture.Player.World,Target,Error));
		FVBPlayBotOpeningView V; V.bActivated = Desired; V.bTargetComplete = true;
		V.bReady = V.bFocused = V.bGrounded = true;
		FVBPlayBotHospitalActions Initial; Initial.Step.bTargetActivated = Desired;
		TestTrue(TEXT("initial desired state cannot prove normal input"),Initial.Tick(V,0,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
		FVBPlayBotHospitalActions Action; Action.Step.bTargetActivated = Desired; Action.EffectTimeout = 8;
		int32 Edges = 0;
		Input.OnButton = [&](const FVBPlayBotPress& Press,bool bDown) { if (bDown) { ++Edges; Action.NotifyInjected(Press.Id); } };
		const double Origin = Desired ? 10 : 1;
		V.bActivated = !Desired; V.bTargetComplete = false;
		TestTrue(TEXT("opposite state schedules focus-confirmed use"),Action.Tick(V,Origin,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
		TestTrue(TEXT("actual Slate press delivers"),Input.TickNative(10,Origin+1,1000,50));
		TestEqual(TEXT("one use edge delivered"),Edges,1);
		V.bActivated = Desired; V.bTargetComplete = true;
		TestTrue(TEXT("result starts settle"),Action.Tick(V,Origin+1.1,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
		V.bActivated = !Desired; V.bTargetComplete = false;
		TestTrue(TEXT("lost result breaks settle"),Action.Tick(V,Origin+1.5,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
		V.bActivated = Desired; V.bTargetComplete = true;
		TestTrue(TEXT("old settle cannot pass transient activation"),Action.Tick(V,Origin+2.2,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
		TestTrue(TEXT("stable injected transition passes"),Action.Tick(V,Origin+3.3,Input,Human,Error) == EVBPlayBotOpeningResult::PartialComplete);
		Input.ReleaseAll(11,Origin+3.4); Input.OnButton = nullptr;
	}
	return true;
}

// Jump 단계는 떴다가 목표에 서기만 하면 통과해서, 넘기 대신 그냥 점프로 창턱에 올라도 합격이었다.
// Traverse/Ladder 는 게임의 넘기, 사다리 상태를 봐야 통과한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotTraversalEvidenceTest, "VB.Unit.PlayBot.Hospital.TraversalEvidence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotTraversalEvidenceTest::RunTest(const FString& Parameters)
{
	TArray<FVBPlayBotOpeningStep> Steps; FString Error;
	TestTrue(TEXT("traverse, ladder and push rows parse"), FVBPlayBotOpeningStep::Parse({
		TEXT("Window|Traverse|1850,1250,80|20"), TEXT("Up|Ladder|3730,3700,400|60"),
		TEXT("Bed|Push|Prologue.TreatmentBed|/Script/VB.VBLevelBlock_Traversable|100|30"),
		TEXT("BedByPath|PushActor|/Game/Test.Test:PersistentLevel.TRV_Bed|/Script/VB.VBLevelBlock_Traversable|80|30") }, Steps, Error));
	if (!TestEqual(TEXT("four steps"), Steps.Num(), 4)) { return false; }
	TestTrue(TEXT("actor push keeps the exact path and no save key"), Steps[3].Kind == EVBPlayBotOpeningAction::Push && Steps[3].SaveKey.IsNone() && Steps[3].TargetPath.EndsWith(TEXT("TRV_Bed")));
	TestTrue(TEXT("traverse is a jump input that needs traversal evidence"), Steps[0].Kind == EVBPlayBotOpeningAction::Jump && Steps[0].JumpEvidence == EVBPlayBotJumpEvidence::TraversalMove);
	TestTrue(TEXT("ladder is a jump input that needs ladder evidence"), Steps[1].Kind == EVBPlayBotOpeningAction::Jump && Steps[1].JumpEvidence == EVBPlayBotJumpEvidence::Ladder);
	TestTrue(TEXT("push keeps identity and distance"), Steps[2].Kind == EVBPlayBotOpeningAction::Push && Steps[2].SaveKey == FName(TEXT("Prologue.TreatmentBed")) && Steps[2].PushMinDistance == 100.0);
	for (const FString& Invalid : { FString(TEXT("A|Traverse|1,2|20")), FString(TEXT("A|Push|Key|Class|0|20")), FString(TEXT("A|Push|Key|Class|1001|20")),
		FString(TEXT("A|Push|Key|Class|far|20")), FString(TEXT("A|Push||Class|100|20")), FString(TEXT("A|Push|Key||100|20")),
		FString(TEXT("A|PushActor|TRV_Bed|Class|100|20")) })
	{
		TestFalse(TEXT("invalid traversal/push row fails"), FVBPlayBotOpeningStep::Parse({Invalid}, Steps, Error));
	}

	VBPlayBotInputDriverTest::FMappingFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* JumpAction = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>();
	Pad->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	if (!Fixture.Configure(*this, Pad, Keyboard)) { return false; }
	Fixture.Activate(Pad, ECommonInputType::Gamepad);
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(), 177);
	if (!Input.ResolveGamepadAction(Fixture.Player.Controller, TEXT("Input.Jump"), JumpAction, Error)) { AddError(Error); return false; }

	// 같은 입력과 같은 궤적에서 증거 값만 바꿔 네 경우를 돌린다.
	struct FCase { const TCHAR* Name; EVBPlayBotJumpEvidence Evidence; bool bMove; bool bLadder; EVBPlayBotOpeningResult Want; };
	const FCase Cases[] = {
		{ TEXT("plain jump onto the sill fails a traverse step"), EVBPlayBotJumpEvidence::TraversalMove, false, false, EVBPlayBotOpeningResult::Failed },
		{ TEXT("observed traversal montage passes a traverse step"), EVBPlayBotJumpEvidence::TraversalMove, true, false, EVBPlayBotOpeningResult::PartialComplete },
		{ TEXT("observed ladder attach passes a ladder step"), EVBPlayBotJumpEvidence::Ladder, false, true, EVBPlayBotOpeningResult::PartialComplete },
		{ TEXT("plain jump still passes a jump step"), EVBPlayBotJumpEvidence::Airborne, false, false, EVBPlayBotOpeningResult::PartialComplete } };
	double T = 10.0; int64 Frame = 1;
	for (const FCase& C : Cases)
	{
		FVBPlayBotHospitalActions Jump;
		Jump.Step.Kind = EVBPlayBotOpeningAction::Jump; Jump.Step.JumpEvidence = C.Evidence; Jump.Step.Point = FVector(100, 0, 80);
		Input.OnButton = [&Jump](const FVBPlayBotPress& Press, bool bDown) { if (bDown) { Jump.NotifyInjected(Press.Id); } };
		FVBPlayBotOpeningView V; V.bReady = V.bGrounded = true; V.Feet = FVector(0, 0, 0); V.FacingYaw = 0;
		TestTrue(TEXT("facing the goal schedules the jump"), Jump.Tick(V, T, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
		Input.TickNative(Frame++, T + 1.0, 1000, 50);
		V.bGrounded = false; V.Feet = FVector(50, 0, 60); V.bTraversalMove = C.bMove; V.bOnLadder = C.bLadder;
		TestTrue(TEXT("airborne is not yet a result"), Jump.Tick(V, T + 1.1, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
		V.bGrounded = true; V.Feet = FVector(100, 0, 80); V.bTraversalMove = false; V.bOnLadder = false;
		EVBPlayBotOpeningResult R = Jump.Tick(V, T + 1.5, Input, Human, Error);
		if (C.Want == EVBPlayBotOpeningResult::PartialComplete && R == EVBPlayBotOpeningResult::Running) { R = Jump.Tick(V, T + 1.9, Input, Human, Error); }
		TestTrue(C.Name, R == C.Want);
		Input.ReleaseAll(Frame++, T + 2.0); T += 10.0;
	}
	Input.OnButton = nullptr;
	return true;
}

// 밀기 단계 - 잡기, 밀기, 놓기가 모두 실제 입력이어야 하고, 대상이 정한 거리만큼 움직여야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotPushOracleTest, "VB.Unit.PlayBot.Hospital.PushOracle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotPushOracleTest::RunTest(const FString& Parameters)
{
	VBPlayBotInputDriverTest::FMappingFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Use = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>();
	Pad->MapKey(Use, EKeys::Gamepad_FaceButton_Left);
	if (!Fixture.Configure(*this, Pad, Keyboard)) { return false; }
	Fixture.Activate(Pad, ECommonInputType::Gamepad);
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(), 177);
	FString Error;
	if (!Input.ResolveGamepadAction(Fixture.Player.Controller, TEXT("Input.Interact"), Use, Error)) { AddError(Error); return false; }

	auto MakePush = [](FVBPlayBotHospitalActions& A) { A.Step.Kind = EVBPlayBotOpeningAction::Push; A.Step.PushMinDistance = 100.0; A.EffectTimeout = 3.0; };
	FVBPlayBotOpeningView V; V.bReady = V.bGrounded = V.bFocused = true;

	FVBPlayBotHospitalActions Cheat; MakePush(Cheat);
	V.bHolding = true;
	TestTrue(TEXT("held before any input cannot prove a grab"), Cheat.Tick(V, 1.0, Input, Human, Error) == EVBPlayBotOpeningResult::Failed);

	FVBPlayBotHospitalActions Stall; MakePush(Stall);
	Input.OnButton = [&Stall](const FVBPlayBotPress& Press, bool bDown) { if (bDown) { Stall.NotifyInjected(Press.Id); } };
	V.bHolding = false;
	TestTrue(TEXT("focused pushable schedules the grab"), Stall.Tick(V, 10.0, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	Input.TickNative(1, 11.0, 1000, 50);
	V.bHolding = true; V.TargetDisplacement = 0.0;
	TestTrue(TEXT("observed grab starts pushing"), Stall.Tick(V, 11.1, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	V.TargetDisplacement = 30.0;
	TestTrue(TEXT("moving target keeps pushing"), Stall.Tick(V, 12.0, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("a target that stops short fails"), Stall.Tick(V, 15.5, Input, Human, Error) == EVBPlayBotOpeningResult::Failed);
	Input.ReleaseAll(2, 16.0);

	FVBPlayBotHospitalActions Push; MakePush(Push);
	Input.OnButton = [&Push](const FVBPlayBotPress& Press, bool bDown) { if (bDown) { Push.NotifyInjected(Press.Id); } };
	V.bHolding = false; V.TargetDisplacement = 0.0;
	TestTrue(TEXT("grab scheduled"), Push.Tick(V, 20.0, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	Input.TickNative(3, 21.0, 1000, 50);
	Input.TickNative(4, 21.5, 500, 50);   // 잡기 버튼의 뗌까지 보내 두어야 다음 누름이 새 입력이 된다
	V.bHolding = true;
	TestTrue(TEXT("grab observed"), Push.Tick(V, 21.6, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	V.TargetDisplacement = 120.0;
	TestTrue(TEXT("declared distance reached moves to release"), Push.Tick(V, 22.0, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("release scheduled"), Push.Tick(V, 22.1, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	Input.TickNative(5, 23.2, 1000, 50);
	V.bHolding = false;
	TestTrue(TEXT("release begins settle"), Push.Tick(V, 23.3, Input, Human, Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("grab, push and release by input pass"), Push.Tick(V, 23.7, Input, Human, Error) == EVBPlayBotOpeningResult::PartialComplete);
	Input.OnButton = nullptr;
	return true;
}
#endif
