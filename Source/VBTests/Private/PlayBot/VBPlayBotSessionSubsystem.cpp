// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "PlayBot/VBPlayBotSessionSubsystem.h"
#include "Character/VBHeroComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

namespace
{
	FString RouteOption()
	{
		FString Name;
		FParse::Value(FCommandLine::Get(), TEXT("VBHospitalRoute="), Name);
		return Name;
	}
	bool NormalSettings(FString& Error)
	{
		const IConsoleVariable* NoDeath = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Combat.PlayerNoDeath"));
		const IConsoleVariable* EnemyScale = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Combat.EnemyHealthScale"));
		if (!NoDeath || !EnemyScale || NoDeath->GetInt() != 0 || !FMath::IsNearlyEqual(EnemyScale->GetFloat(), 1.f)
			|| FApp::UseFixedTimeStep() || (GEngine && GEngine->bUseFixedFrameRate)
			|| FParse::Param(FCommandLine::Get(), TEXT("DeterministicAudio")))
		{
			Error = TEXT("native run requires authored health settings, variable time and normal audio renderer");
			return false;
		}
		return true;
	}
}

bool UVBPlayBotSessionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !GIsEditor && !IsRunningCommandlet() && !RouteOption().IsEmpty();
}

void UVBPlayBotSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Clock.Advance(FPlatformTime::Seconds());
	FString Error;
	FString OutDir;
	FString Legacy;
	FParse::Value(FCommandLine::Get(), TEXT("VBPlayBotOut="), OutDir);
	FParse::Value(FCommandLine::Get(), TEXT("VBPlayBot="), Legacy);
	if (!Legacy.IsEmpty() || OutDir.IsEmpty() || !Config.Load(RouteOption(), Error)
		|| Config.ClassName != FName(TEXT("HospitalOpening")) || !NormalSettings(Error)
		|| !FVBPlayBotOpeningStep::Parse(Config.GetArray(TEXT("Step")), Steps, Error))
	{
		Finish(Error.IsEmpty() ? TEXT("invalid native opening flags/configuration") : Error, false);
		return;
	}
	for (const FVBPlayBotOpeningStep& Step : Steps)
	{
		if (Step.Kind != EVBPlayBotOpeningAction::Fight || FightSpecs.Contains(Step.FightGroup)) { continue; }
		FVBPlayBotHospitalFightSpec Spec;
		if (!Spec.Load(Config,Step.FightGroup,Error)) { Finish(Error,false); return; }
		FightSpecs.Add(Step.FightGroup,MoveTemp(Spec));
	}
	ReadyTimeout = Config.GetDouble(TEXT("ReadyTimeoutSeconds"), -1.0);
	Actions.EffectTimeout = Config.GetDouble(TEXT("EffectTimeoutSeconds"), -1.0);
	FVBPlayBotNativeCapture Limits;
	Limits.StartSeconds = Clock.StartSeconds();
	Limits.IntervalSeconds = Config.GetDouble(TEXT("CaptureIntervalSeconds"), -1.0);
	Limits.MaxGapSeconds = Config.GetDouble(TEXT("CaptureMaxGapSeconds"), -1.0);
	Limits.MaxFrames = Config.GetInt(TEXT("CaptureMaxFrames"), -1);
	const double MaxBytes = Config.GetDouble(TEXT("CaptureMaxBytes"), -1.0);
	if (!FMath::IsFinite(MaxBytes) || MaxBytes <= 0 || MaxBytes > 1073741824.0
		|| !FMath::IsFinite(ReadyTimeout) || ReadyTimeout <= 0
		|| !FMath::IsFinite(Actions.EffectTimeout) || Actions.EffectTimeout <= 0
		|| !FMath::IsFinite(Config.MaxRunSeconds) || Config.MaxRunSeconds > 600.0)
	{
		Finish(TEXT("native opening budgets are missing or outside the short calibration limit"), false);
		return;
	}
	Limits.MaxBytes = static_cast<int64>(MaxBytes);
	if (!Recorder.OpenNative(OutDir, Limits, Error)) { Finish(Error, false); return; }
	Human.Configure(Config.Profile, Config.Seed);
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_run\",\"profile\":\"%s\",\"scope\":\"PartialHospitalRoute\",\"clock\":\"monotonic_seconds\",\"health_defaults\":true,\"audio\":\"Unavailable\",\"config\":%s}"), *Config.Name, *Config.ToJson()));
	Input.OnButton = [this](const FVBPlayBotPress& Press, bool bDown)
	{
		if (bDown) { Actions.NotifyInjected(Press.Id); CombatActions.NotifyInjected(Press.Id,Press.InjectedSeconds); }
		Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_input\",\"id\":%lld,\"f\":%llu,\"rt\":%.9f,\"press\":%d,\"action\":\"%s\",\"key\":\"%s\",\"cue\":\"%s\",\"down\":%s,\"due\":%.9f,\"injected\":%.9f,\"released\":%.9f,\"cancelled\":%s}"),
			++EventId,GFrameCounter,FPlatformTime::Seconds()-Clock.StartSeconds(),Press.Id,
			*Press.Action.ToString().ReplaceCharWithEscapedChar(),*Press.Key.ToString().ReplaceCharWithEscapedChar(),*Press.Cue.ReplaceCharWithEscapedChar(),
			bDown ? TEXT("true") : TEXT("false"),Press.DueSeconds,Press.InjectedSeconds,Press.ReleasedSeconds,Press.Cancel.IsEmpty() ? TEXT("false") : TEXT("true")));

	};
	Recorder.StartCapture();
	WorldTickHandle = FWorldDelegates::OnWorldTickStart.AddUObject(this, &ThisClass::BeforeWorldTick);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::WorldCleanup);
	Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickSession));
	UE_LOG(LogVBPlayBot, Log, TEXT("Native hospital opening started; no fixture setup, full route incomplete"));
}

bool UVBPlayBotSessionSubsystem::Bind(UWorld* World, FString& Error)
{
	if (World->GetOutermost()->GetName() != Config.Map) { Error = TEXT("native opening map differs from the declared map"); return false; }
	const UVBSaveGameSubsystem* Save = GetGameInstance()->GetSubsystem<UVBSaveGameSubsystem>();
	if (!Save || Save->GetGameStartContext().Intent != EVBGameStartIntent::NewGame || !Save->GetGameStartContext().AttemptId.IsValid())
	{
		Error = TEXT("production startup did not declare a fresh game attempt");
		return false;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(PC->GetPawn());
	const FGameplayTag InputTag = FGameplayTag::RequestGameplayTag(Actions.Action, false);
	if (!Hero || !Input.ResolveGamepadAction(PC, Actions.Action, Hero->FindInputAction(InputTag), Error)) { return false; }
	if (!Input.ValidateStickActions(PC, Hero->FindInputAction(FGameplayTag::RequestGameplayTag(TEXT("Input.Move"),false)),
		Hero->FindInputAction(FGameplayTag::RequestGameplayTag(TEXT("Input.Look"),false)), Error)) { return false; }
	if (Steps.ContainsByPredicate([](const FVBPlayBotOpeningStep& Step) { return Step.Kind == EVBPlayBotOpeningAction::Jump; }))
	{
		const FName Jump = TEXT("Input.Jump");
		if (!Input.ResolveGamepadAction(PC,Jump,Hero->FindInputAction(FGameplayTag::RequestGameplayTag(Jump,false)),Error)) { return false; }
	}
	for (const FVBPlayBotOpeningStep& Step : Steps)
	{
		if (Step.Kind != EVBPlayBotOpeningAction::Draw) { continue; }
		const FName Draw = Step.DrawAction;
		if (!Input.ResolveGamepadAction(PC,Draw,Hero->FindInputAction(FGameplayTag::RequestGameplayTag(Draw,false)),Error)) { return false; }
	}
	for (const TPair<FName,FVBPlayBotHospitalFightSpec>& Pair : FightSpecs)
	{
		const FName Attack = Pair.Value.Policy.AttackAction;
		if (!Input.ResolveGamepadAction(PC,Attack,Hero->FindInputAction(FGameplayTag::RequestGameplayTag(Attack,false)),Error)) { return false; }
	}
	BoundPawn = PC->GetPawn();
	if (!FSlateApplication::IsInitialized()) { Error = TEXT("Slate input is unavailable"); return false; }
	bOldInputWhenInactive = FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
	bPolicyCaptured = true;
	FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
	BoundWorld = World;
	BoundSeconds = Clock.Seconds();
	bBound = true;
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_bind\",\"rt\":%.9f,\"epoch\":1,\"map\":\"%s\",\"keys\":%s}"), Clock.Seconds(), *Config.Map, *Input.DescribeKeys()));
	return BeginStep(World, Error);
}

bool UVBPlayBotSessionSubsystem::BeginStep(UWorld* World, FString& Error)
{
	CombatObservation.Unbind();
	Actions = FVBPlayBotHospitalActions();
	Actions.Step = Steps[StepIndex];
	if (Actions.Step.Kind == EVBPlayBotOpeningAction::Fight)
	{
		const FVBPlayBotHospitalFightSpec* Spec = FightSpecs.Find(Actions.Step.FightGroup);
		if (!Spec) { Error = TEXT("native fight specification is missing"); return false; }
		if (!CombatActions.Configure(Spec->Policy,Error) || !CombatObservation.Bind(World,*Spec,Error)) { return false; }
	}
	else if (!Observation.Bind(World,Actions.Step,Error)) { return false; }
	Actions.EffectTimeout = Config.GetDouble(TEXT("EffectTimeoutSeconds"), -1.0);
	StepSeconds = Clock.Seconds();
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_step\",\"rt\":%.9f,\"index\":%d,\"id\":\"%s\",\"point\":[%.3f,%.3f,%.3f]}"),
		StepSeconds, StepIndex, *Actions.Step.Id.ToString(), Actions.Step.Point.X, Actions.Step.Point.Y, Actions.Step.Point.Z));
	return true;
}

void UVBPlayBotSessionSubsystem::BeforeWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (bFinished || !PendingFinish.IsEmpty() || !World || World->WorldType != EWorldType::Game || World->GetGameInstance() != GetGameInstance()) { return; }
	FString Error;
	if (!Clock.Advance(FPlatformTime::Seconds()) || !NormalSettings(Error)) { Finish(Error.IsEmpty() ? TEXT("native clock regressed") : Error, false); return; }
	if (!bBound)
	{
		if (!World->GetFirstPlayerController() || !World->GetFirstPlayerController()->GetPawn()) { return; }
		if (!Bind(World, Error)) { Finish(Error, false); return; }
	}
	if (BoundWorld.Get() != World || !BoundPawn.IsValid() || !World->GetFirstPlayerController() || World->GetFirstPlayerController()->GetPawn() != BoundPawn.Get())
	{ Finish(TEXT("unsupported world or pawn transition in partial opening profile"), false); return; }
	FVBPlayBotOpeningView View;
	FVBPlayBotHospitalCombatView CombatView;
	const bool bFight = Actions.Step.Kind == EVBPlayBotOpeningAction::Fight;
	if (bFight)
	{
		if (!CombatObservation.Read(World,CombatView,Error)) { Finish(Error,false); return; }
		View = CombatView.Movement;
		TArray<FString> Targets;
		for (const FVBPlayBotCombatTargetView& Target : CombatView.Targets)
		{
			Targets.Add(FString::Printf(TEXT("{\"id\":\"%s\",\"pos\":[%.3f,%.3f,%.3f],\"hp\":%.3f,\"max_hp\":%.3f,\"dead\":%s,\"fact\":%s,\"visible\":%s,\"player_hits\":%d}"),
				*Target.Id.ToString(),Target.Position.X,Target.Position.Y,Target.Position.Z,Target.Health,Target.MaxHealth,
				Target.bDead ? TEXT("true") : TEXT("false"),Target.bDefeatedFact ? TEXT("true") : TEXT("false"),Target.bVisible ? TEXT("true") : TEXT("false"),Target.PlayerDamageHits));
		}
		Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_combat\",\"f\":%llu,\"rt\":%.9f,\"step\":%d,\"hp\":%.3f,\"max_hp\":%.3f,\"stamina\":%.3f,\"max_stamina\":%.3f,\"targets\":[%s]}"),
			GFrameCounter,Clock.Seconds(),StepIndex,CombatView.PlayerHealth,CombatView.PlayerMaxHealth,CombatView.Stamina,CombatView.MaxStamina,*FString::Join(Targets,TEXT(","))));
	}
	else if (!Observation.Read(World,View,Error)) { Finish(Error,false); return; }
	if (View.bReady && Clock.Seconds()-StepSeconds > Actions.Step.Timeout)
	{ Finish(FString::Printf(TEXT("step %s exceeded its declared budget"), *Actions.Step.Id.ToString()),false); return; }
	if (!View.bReady) { StepSeconds = Clock.Seconds(); }
	if (Actions.Step.Kind == EVBPlayBotOpeningAction::Observe)
	{
		Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_fact\",\"f\":%llu,\"rt\":%.9f,\"fact\":\"%s\",\"value\":%s}"),
			GFrameCounter,Clock.Seconds(),*Actions.Step.ObserveFact.ToString(),View.bObservedFact ? TEXT("true") : TEXT("false")));
	}
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_weapon\",\"f\":%llu,\"rt\":%.9f,\"owned\":%s,\"drawn\":%s}"),
		GFrameCounter,Clock.Seconds(),View.bWeaponOwned ? TEXT("true") : TEXT("false"),View.bWeaponDrawn ? TEXT("true") : TEXT("false")));
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_route_sample\",\"f\":%llu,\"rt\":%.9f,\"step\":%d,\"feet\":[%.3f,%.3f,%.3f],\"movement_mode\":%d,\"owned_sword\":%s,\"weapon_fact\":%s}"),
		GFrameCounter,Clock.Seconds(),StepIndex,View.Feet.X,View.Feet.Y,View.Feet.Z,View.MovementMode,
		View.bWeaponOwned ? TEXT("true") : TEXT("false"),View.bWeaponFact ? TEXT("true") : TEXT("false")));
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_sample\",\"id\":%lld,\"f\":%llu,\"rt\":%.9f,\"gt\":%.6f,\"epoch\":1,\"pos\":[%.3f,%.3f,%.3f],\"vel\":[%.3f,%.3f,%.3f],\"camera\":[%.3f,%.3f,%.3f],\"yaw\":%.3f,\"ready\":%s,\"focused\":%s,\"activated\":%s,\"grounded\":%s}"),
		++EventId, GFrameCounter, Clock.Seconds(), World->GetTimeSeconds(), View.Position.X, View.Position.Y, View.Position.Z,
		View.Velocity.X, View.Velocity.Y, View.Velocity.Z, View.Camera.X, View.Camera.Y, View.Camera.Z, View.CameraYaw,
		View.bReady ? TEXT("true") : TEXT("false"), View.bFocused ? TEXT("true") : TEXT("false"), View.bActivated ? TEXT("true") : TEXT("false"), View.bGrounded ? TEXT("true") : TEXT("false")));
	if (Clock.Seconds() - BoundSeconds > ReadyTimeout && !View.bReady) { Finish(TEXT("authored opening never became ready"), false); return; }
	if (View.bReady && !bWakeSent)
	{
		Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_device_wake\",\"rt\":%.9f,\"key\":\"Gamepad_LeftStick_Right\",\"down_then_up\":true}"), Clock.Seconds()));
		Input.SendWakeTap(); bWakeSent = true;
	}
	const EVBPlayBotOpeningResult Result = bFight ? CombatActions.Tick(CombatView,Clock.Seconds(),Input,Human,Error) : Actions.Tick(View,Clock.Seconds(),Input,Human,Error);
	if (!Input.TickNative(static_cast<int64>(GFrameCounter), Clock.Seconds(), Clock.DeltaMilliseconds(), Config.Profile.MotorRampMs))
	{ Finish(TEXT("native input tick failed"), false); return; }
	const FVector2D MoveOut = Input.GetStickOut();
	const FVector2D LookOut = Input.GetLookOut();
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_axes\",\"f\":%llu,\"rt\":%.9f,\"step\":%d,\"move\":[%.6f,%.6f],\"look\":[%.6f,%.6f]}"),
		GFrameCounter,Clock.Seconds(),StepIndex,MoveOut.X,MoveOut.Y,LookOut.X,LookOut.Y));
	Recorder.Flush();
	if (Result == EVBPlayBotOpeningResult::Failed) { PendingFinish = Error; bPendingPartialComplete = false; }
	else if (Result == EVBPlayBotOpeningResult::PartialComplete)
	{
		Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_step_complete\",\"rt\":%.9f,\"index\":%d,\"id\":\"%s\"}"),Clock.Seconds(),StepIndex,*Actions.Step.Id.ToString()));
		Input.ReleaseAll(static_cast<int64>(GFrameCounter),Clock.Seconds());
		++StepIndex;
		if (StepIndex == Steps.Num()) { PendingFinish = TEXT("PartialOpeningComplete"); bPendingPartialComplete = true; }
		else if (!BeginStep(World,Error)) { PendingFinish = Error; bPendingPartialComplete = false; }
	}
}

bool UVBPlayBotSessionSubsystem::TickSession(float DeltaSeconds)
{
	if (bFinished) { return false; }
	if (FPlatformTime::Seconds() - Clock.StartSeconds() > Config.MaxRunSeconds) { Finish(TEXT("native opening hard deadline"), false); return false; }
	if (!PendingFinish.IsEmpty())
	{
		if (Recorder.HasPendingNativeFrame())
		{
			Recorder.RequestNativeFrame(1, false);
			if (Recorder.HasFailed()) { Finish(Recorder.GetFailure(), false); return false; }
			return true;
		}
		Finish(PendingFinish, bPendingPartialComplete);
		return false;
	}
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{ Recorder.RequestNativeFrame(bBound ? 1 : 0); }
	if (Recorder.HasFailed()) { Finish(Recorder.GetFailure(), false); return false; }
	return true;
}

void UVBPlayBotSessionSubsystem::WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!bFinished && BoundWorld.Get() == World) { Finish(TEXT("world cleanup ended the partial opening attempt"), false); }
}

void UVBPlayBotSessionSubsystem::Finish(const FString& Reason, bool bPartialComplete, bool bRequestExit)
{
	if (bFinished) { return; }
	bFinished = true;
	CombatObservation.Unbind();
	const bool bCaptureFinalized = Recorder.FinalizeNativeCapture();
	bPartialComplete = bPartialComplete && bCaptureFinalized && Recorder.GetCapturedFrames() >= 2;
	const FString FinalReason = Recorder.HasFailed() ? Reason + TEXT("; capture: ") + Recorder.GetFailure() : Reason;
	Input.ReleaseAll(static_cast<int64>(GFrameCounter), FPlatformTime::Seconds() - Clock.StartSeconds());
	FWorldDelegates::OnWorldTickStart.Remove(WorldTickHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	if (bPolicyCaptured && FSlateApplication::IsInitialized())
	{ FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(bOldInputWhenInactive); bPolicyCaptured = false; }
	Recorder.WriteLine(FString::Printf(TEXT("{\"k\":\"native_end\",\"rt\":%.9f,\"partial_complete\":%s,\"full_route_complete\":false,\"reason\":\"%s\",\"captures\":%lld}"),
		FPlatformTime::Seconds() - Clock.StartSeconds(), bPartialComplete ? TEXT("true") : TEXT("false"), *FinalReason.ReplaceCharWithEscapedChar(), Recorder.GetCapturedFrames()));
	Recorder.Close();
	UE_LOG(LogVBPlayBot, Log, TEXT("Native hospital opening ended: %s"), *FinalReason);
	if (bRequestExit) { FPlatformMisc::RequestExitWithStatus(false, bPartialComplete && !Recorder.HasFailed() ? 0 : 2); }
}

void UVBPlayBotSessionSubsystem::Deinitialize()
{
	Finish(TEXT("session deinitialized"), false, false);
	if (Ticker.IsValid()) { FTSTicker::RemoveTicker(Ticker); Ticker.Reset(); }
	Super::Deinitialize();
}
