// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotSubsystem.h"
#include "PlayBot/VBPlayBotDamageEvidence.h"

#include "PlayBot/VBPlayBotFrame.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBCombatFeedbackModifier.h"
#include "CommonInputSubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Particles/ParticleSystem.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UObjectIterator.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace
{
	// 준비 한도 - 콜드 스타트와 World Partition streaming 에는 넉넉하고, 깨진 경로는 빨리 멈출 만큼 짧다
	constexpr int64 BootSettleFrames = 30;
	constexpr int64 BootTimeoutFrames = 480;
	constexpr int64 GroundTimeoutFrames = 60 * 60;
	constexpr int64 GroundStableFrames = 30;
	constexpr int64 WakeTimeoutFrames = 120;
	constexpr int64 PadContextFrames = 3;

	FString Vec(const FVector& V)
	{
		return FString::Printf(TEXT("[%.2f,%.2f,%.2f]"), V.X, V.Y, V.Z);
	}

	FString Option(const TCHAR* Key)
	{
		FString Value;
		// 구분자에서 멈추지 않는다 - 쉼표 목록(ForceWrong 이름들)이 값 하나다
		FParse::Value(FCommandLine::Get(), Key, Value, /*bShouldStopOnSeparator=*/false);
		return Value;
	}
}

bool UVBPlayBotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->WorldType == EWorldType::Game && !GIsEditor
		&& !Option(TEXT("VBPlayBot=")).IsEmpty();
}

void UVBPlayBotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ScenarioName = Option(TEXT("VBPlayBot="));
	const FString OutDir = Option(TEXT("VBPlayBotOut="));
	const FString FFmpeg = Option(TEXT("VBPlayBotFFmpeg="));
	bActive = true;

	FString Error;
	if (OutDir.IsEmpty() || FFmpeg.IsEmpty())
	{
		Abort(TEXT("-VBPlayBotOut and -VBPlayBotFFmpeg are required"));
		return;
	}
	// 옵션 적용, 빠른 이동이 세이브를 쓰므로 실행 전용 user dir 이 있어야 한다 - -userdir 없이 띄우면 실제 Saved 를 쓰니 거절한다
	const FString UserDir = FPaths::ConvertRelativePathToFull(Option(TEXT("userdir=")));
	const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
	if (UserDir.IsEmpty() || !FPaths::IsUnderDirectory(SavedDir, UserDir))
	{
		Abort(FString::Printf(TEXT("saves are not isolated: ProjectSavedDir %s is not under -userdir '%s'"), *SavedDir, *UserDir));
		return;
	}

	if (!Recorder.Open(OutDir, FFmpeg, Error) || !Config.Load(ScenarioName, Error))
	{
		Abort(Error);
		return;
	}

	Scenario = FVBPlayBotScenarioRegistry::Create(Config.ClassName);
	if (!Scenario)
	{
		Abort(FString::Printf(TEXT("no scenario class '%s' is registered"), *Config.ClassName.ToString()));
		return;
	}

	Human.Configure(Config.Profile, Config.Seed);
	Checks.Emit = [this](const FString& Json) { Emit(Json); };
	Checks.SetForceWrong(Option(TEXT("VBPlayBotForceWrong=")));
	Checks.SetClock(&Frame);
	Input.OnButton = [this](const FVBPlayBotPress& Press, bool bDown) { HandleButton(Press, bDown); };
	Ctx.World = &InWorld;
	Ctx.Human = &Human;
	Ctx.Input = &Input;
	Ctx.Checks = &Checks;
	Ctx.Overrides = &Overrides;
	Ctx.Config = &Config;
	Ctx.Emit = [this](const FString& Json) { Emit(Json); };
	Ctx.Watch = [this](AActor* Actor) { Watch(Actor); };
	UE_LOG(LogVBPlayBot, Log, TEXT("PlayBot %s (%s) started, output %s"), *ScenarioName, *Config.ClassName.ToString(), *OutDir);
}

void UVBPlayBotSubsystem::Deinitialize()
{
	if (bActive && !bEnded)
	{
		Abort(TEXT("world ended before the run finished"));
	}

	UnbindEvents();
	if (bInputPolicyCaptured && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(bPreviousInputWhenInactive);
		bInputPolicyCaptured = false;
	}
	Super::Deinitialize();
}

TStatId UVBPlayBotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVBPlayBotSubsystem, STATGROUP_Tickables);
}

void UVBPlayBotSubsystem::Emit(const FString& Json)
{
	Recorder.WriteLine(Json);
}

void UVBPlayBotSubsystem::RefreshContext()
{
	Ctx.Frame = Frame;
	Ctx.Player = Player.Get();
	Ctx.PC = Player.IsValid() ? Cast<APlayerController>(Player->GetController()) : nullptr;
	Ctx.PlayerASC = PlayerASC.Get();
}

void UVBPlayBotSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bActive || bEnded)
	{
		return;
	}
	// 앞 프레임 이미지는 이미 도착했다 (FinishRun 의 drain 이 기대는 순서와 같다)
	if (!PendingAbort.IsEmpty())
	{
		Abort(PendingAbort);
		return;
	}

	// 프레임 N - 월드가 N 을 돌린 뒤 기록하고, 판단하고, N+1 에 읽힐 입력을 보내고, N 을 찍는다
	if (Frame > Ctx.SecondsToFrames(Config.MaxRunSeconds))
	{
		Abort(TEXT("MaxRunSeconds exceeded"));
		return;
	}

	RefreshContext();
	// CSV drain 은 이 프레임 표본 전에 끝나므로 마지막 telemetry 프레임에는 늘 영상 프레임이 있다
	if (Stage == EStage::Draining && ((CsvFile.IsValid() && CsvFile.IsReady()) || Frame - StageFrame > 180))
	{
		FinishRun();
		if (bEnded)
		{
			return;
		}
	}
	// 표본 줄이 있는 프레임만 찍어 영상 프레임 N 과 telemetry 줄 N 을 1:1 로 둔다
	bSampledThisFrame = false;
	if (Stage == EStage::Run || Stage == EStage::Draining)
	{
		SampleFrame();
		bSampledThisFrame = true;
	}

	switch (Stage)
	{
	case EStage::Boot: if (!StepBoot()) return; break;
	case EStage::Ground: if (!StepGround()) return; break;
	case EStage::Wake: if (!StepWake()) return; break;
	case EStage::Draining:
		// CSV 파일을 최대 3초 기다린다 (Tick 머리에서 확인) - 결과는 어느 쪽이든 로그에 남는다
		break;
	case EStage::Run:
	{
		if (!Ctx.Player || !Ctx.PC)
		{
			AbortAfterCapture(TEXT("player pawn or controller lost during the run"));
			break;
		}

		const EVBPlayBotState State = Scenario->Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			AbortAfterCapture(Scenario->GetFailure());
			break;
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			// CSV drain 으로 들어가거나 끝난다 - 실행이 이어지면 이 프레임은 아래에서 찍힌다
			FinishRun();
			if (bEnded)
			{
				return;
			}
		}
		break;
	}
	default:
		break;
	}

	if (bEnded)
	{
		return;
	}

	// 중단 대기 중에는 입력을 더 보내지 않지만 표본 프레임은 찍는다
	if (PendingAbort.IsEmpty() && !Input.Tick(Frame, VBPlayBot::FrameMs, Human.GetProfile().MotorRampMs))
	{
		AbortAfterCapture(TEXT("Slate application is not available"));
	}

	if (bSampledThisFrame)
	{
		Recorder.RequestFrame();
		++RequestedFrames;
	}

	Recorder.Flush();
	if (Recorder.HasFailed())
	{
		Abort(Recorder.GetFailure());
		return;
	}

	++Frame;
}

bool UVBPlayBotSubsystem::StepBoot()
{
	APlayerController* PC = Ctx.World->GetFirstPlayerController();
	ACharacter* Character = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !PC->GetLocalPlayer())
	{
		if (Frame > BootTimeoutFrames)
		{
			Abort(TEXT("no local player character spawned"));
			return false;
		}

		StageFrame = Frame;
		return true;
	}

	if (Frame - StageFrame < BootSettleFrames)
	{
		return true;
	}

	Player = Character;
	PlayerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character);
	if (!PlayerASC.IsValid())
	{
		Abort(TEXT("player has no ability system component"));
		return false;
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	Slate.SetAllUserFocusToGameViewport();
	// 검사 중 창 포커스가 바뀌어도 입력을 받고 종료 시 원래 정책을 복원한다.
	const bool bWasActive = Slate.IsActive();
	bPreviousInputWhenInactive = Slate.GetHandleDeviceInputWhenApplicationNotActive();
	bInputPolicyCaptured = true;
	Slate.SetHandleDeviceInputWhenApplicationNotActive(true);

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"focus\",\"f\":%lld,\"appActive\":%s,\"inputWhenInactive\":%s}"),
		Frame, bWasActive ? TEXT("true") : TEXT("false"), Slate.GetHandleDeviceInputWhenApplicationNotActive() ? TEXT("true") : TEXT("false")));
	Stage = EStage::Ground;
	StageFrame = Frame;
	return true;
}

bool UVBPlayBotSubsystem::StepGround()
{
	// World Partition 맵은 플레이어 주변을 streaming 하므로 바닥 cell 이 오기 전에 놓은 몸은 떨어진다
	// streaming 을 기다린 뒤 플레이어가 바닥에 가만히 설 때까지 기다린다
	if (!Player.IsValid())
	{
		Abort(TEXT("player pawn lost before the run"));
		return false;
	}

	const UWorldPartitionSubsystem* Partition = Ctx.World->GetSubsystem<UWorldPartitionSubsystem>();
	const bool bStreamed = !Partition || Partition->IsStreamingCompleted();
	const bool bGrounded = FVBPlayBotContext::IsGrounded(Player.Get());
	if (!bStreamed || !bGrounded)
	{
		PadReadyFrame = Frame;
	}

	if (bStreamed && bGrounded && Frame - PadReadyFrame >= GroundStableFrames)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"grounded\",\"f\":%lld,\"pos\":%s,\"partition\":%s}"),
			Frame, *Vec(Player->GetActorLocation()), Partition ? TEXT("true") : TEXT("false")));
		PadReadyFrame = -1;
		Stage = EStage::Wake;
		StageFrame = Frame;
		return true;
	}

	if (Frame - StageFrame > GroundTimeoutFrames)
	{
		const UCharacterMovementComponent* Move = Player->GetCharacterMovement();
		Abort(FString::Printf(TEXT("player never stood on a floor (streamed %s, mode %d, z %.1f) - map/start has no floor"),
			bStreamed ? TEXT("yes") : TEXT("no"), Move ? static_cast<int32>(Move->MovementMode) : -1, Player->GetActorLocation().Z));
		return false;
	}

	return true;
}

bool UVBPlayBotSubsystem::StepWake()
{
	UCommonInputSubsystem* CommonInput = Ctx.PC ? UCommonInputSubsystem::Get(Ctx.PC->GetLocalPlayer()) : nullptr;
	if (!Player.IsValid() || !CommonInput)
	{
		Abort(TEXT("no CommonInput subsystem on the local player"));
		return false;
	}

	if (CommonInput->GetCurrentInputType() != ECommonInputType::Gamepad)
	{
		if (Frame - StageFrame > WakeTimeoutFrames)
		{
			Abort(TEXT("pad events sent through Slate did not switch CommonInput to Gamepad (focus or route)"));
			return false;
		}

		if ((Frame - StageFrame) % 10 == 0)
		{
			Input.SendWakeTap();
		}

		return true;
	}
	// 입력 방식이 바뀌면 device manager 가 mapping context 를 바꾸므로 몇 프레임 준다
	if (PadReadyFrame < 0)
	{
		PadReadyFrame = Frame;
	}

	if (Frame - PadReadyFrame < PadContextFrames)
	{
		return true;
	}

	if (UAbilitySystemComponent* ASC = PlayerASC.Get())
	{
		WindowOpenHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_WindowOpen)
			.AddUObject(this, &ThisClass::HandleWindowEvent, true);
		WindowCloseHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_WindowClose)
			.AddUObject(this, &ThisClass::HandleWindowEvent, false);
	}

	Watch(Player.Get());
	ClearHostiles();
	// telemetry, 입력, brain 의 프레임 수는 모두 한 프레임 = VBPlayBot::FrameMs 를 가정하므로 -FPS 를 믿지 않고 잰다
	if (!FApp::UseFixedTimeStep() || !FMath::IsNearlyEqual(FApp::GetFixedDeltaTime(), VBPlayBot::FrameMs / 1000.0, 1e-6))
	{
		Abort(FString::Printf(TEXT("fixed time step is %s at %.6f s; the bot needs %.6f s"),
			FApp::UseFixedTimeStep() ? TEXT("on") : TEXT("off"), FApp::GetFixedDeltaTime(), VBPlayBot::FrameMs / 1000.0));
		return false;
	}

	FString Error;
	if (!ParseImpactFxRules(Error))
	{
		Abort(Error);
		return false;
	}

	if (!Scenario->Setup(Ctx, Error))
	{
		Abort(Error.IsEmpty() ? TEXT("scenario setup failed") : Error);
		return false;
	}

	Emit(FString::Printf(TEXT("{\"k\":\"run\",\"f\":%llu,\"uf\":%lld,\"config\":%s,\"path\":\"%s\",\"keys\":%s,\"player\":\"%s\",\"playerStart\":%s,\"fps\":60,")
		TEXT("\"forceWrong\":\"%s\",\"clock\":\"uf = undilated fixed-step frame; gt = dilated world seconds; rt = wall seconds\"}"),
		GFrameCounter, Frame, *Config.ToJson(), *FVBPlayBotInputDriver::PathName(), *Input.DescribeKeys(),
		*Player->GetClass()->GetName(), *Vec(Player->GetActorLocation()), *Option(TEXT("VBPlayBotForceWrong=")).ReplaceCharWithEscapedChar()));
	// 적용 중인 frame cap 을 잰다 (bot 은 바꾸지 않는다) - 제한 없음 + 고정 step 이면 GPU 가 허락하는 만큼 빠르게 그린다
	auto CVarText = [](const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetString() : FString(TEXT("missing"));
	};
	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"frame_cap\",\"f\":%lld,\"t.MaxFPS\":\"%s\",\"r.VSync\":\"%s\",\"fixedFrameRate\":%s,\"fixedFrameRateValue\":%.2f,\"smoothFrameRate\":%s,\"fixedTimeStep\":%s}"),
		Frame, *CVarText(TEXT("t.MaxFPS")), *CVarText(TEXT("r.VSync")),
		GEngine && GEngine->bUseFixedFrameRate ? TEXT("true") : TEXT("false"), GEngine ? GEngine->FixedFrameRate : 0.f,
		GEngine && GEngine->bSmoothFrameRate ? TEXT("true") : TEXT("false"), FApp::UseFixedTimeStep() ? TEXT("true") : TEXT("false")));
	Recorder.StartCapture();
	Stage = EStage::Run;
	StageFrame = Frame;
	RunStartFrame = Frame;
#if CSV_PROFILER
	// 엔진 CSV profiler 의 프레임별 thread/GPU 시간(영상 옆 frames.csv) - bot 은 여기서 아무것도 읽지 않는다
	if (FCsvProfiler* Csv = FCsvProfiler::Get())
	{
		Csv->BeginCapture(-1, Option(TEXT("VBPlayBotOut=")), TEXT("frames.csv"));
		bCsvCapturing = true;
	}
#endif
	return true;
}

void UVBPlayBotSubsystem::ClearHostiles()
{
	// 시작 지점 근처 적을 이 게임 프로세스에서만 지운다 (맵 asset 은 그대로)
	// ClearHostilesRadius=0 이면 다 남기고, +ClearHostilesKeep=<class> 는 그 클래스를 남긴다
	const double Radius = Config.GetDouble(TEXT("ClearHostilesRadius"), 0.0);
	if (Radius <= 0.0 || !Player.IsValid())
	{
		return;
	}

	UClass* EnemyClass = FSoftClassPath(TEXT("/Script/VB.VBEnemyBase")).TryLoadClass<AActor>();
	if (!EnemyClass)
	{
		return;
	}

	TArray<UClass*> Keep;
	for (const FString& Path : Config.GetArray(TEXT("ClearHostilesKeep")))
	{
		if (UClass* Class = FSoftClassPath(Path).TryLoadClass<AActor>())
		{
			Keep.Add(Class);
		}
	}

	const FVector Start = Player->GetActorLocation();
	TArray<AActor*> Doomed;
	for (TActorIterator<AActor> It(Ctx.World, EnemyClass); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || FVector::Dist(Start, Actor->GetActorLocation()) > Radius)
		{
			continue;
		}

		if (Keep.ContainsByPredicate([Actor](const UClass* Class) { return Actor->IsA(Class); })) continue;
		Doomed.Add(Actor);
	}

	for (AActor* Actor : Doomed)
	{
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"removed\",\"f\":%lld,\"actor\":\"%s\",\"class\":\"%s\",\"cm\":%.0f}"),
			Frame, *Actor->GetName(), *Actor->GetClass()->GetName(), FVector::Dist(Start, Actor->GetActorLocation())));
		Actor->Destroy();
	}
}

void UVBPlayBotSubsystem::Watch(AActor* Actor)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	if (!ASC || Watched.ContainsByPredicate([Actor](const FWatched& W) { return W.Actor.Get() == Actor; })) return;
	FWatched& Entry = Watched.AddDefaulted_GetRef();
	Entry.Actor = Actor;
	Entry.ASC = ASC;
	Entry.Hit = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit)
		.AddUObject(this, &ThisClass::HandleHitEvent, TWeakObjectPtr<AActor>(Actor));
	for (const FGameplayTag& Tag : Config.TrackTags)
	{
		Entry.Tags.Add(Tag, ASC->RegisterGameplayTagEvent(Tag, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleTag, TWeakObjectPtr<AActor>(Actor)));
	}

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"watch\",\"f\":%lld,\"actor\":\"%s\",\"class\":\"%s\"}"),
		Frame, *Actor->GetName(), *Actor->GetClass()->GetName()));
}

void UVBPlayBotSubsystem::UnbindEvents()
{
	if (UAbilitySystemComponent* ASC = PlayerASC.Get())
	{
		if (FGameplayEventMulticastDelegate* D = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_WindowOpen))
		{
			D->Remove(WindowOpenHandle);
		}

		if (FGameplayEventMulticastDelegate* D = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_WindowClose))
		{
			D->Remove(WindowCloseHandle);
		}
	}

	for (FWatched& Entry : Watched)
	{
		UAbilitySystemComponent* ASC = Entry.ASC.Get();
		if (!ASC)
		{
			continue;
		}

		if (FGameplayEventMulticastDelegate* D = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_Hit))
		{
			D->Remove(Entry.Hit);
		}

		for (const TPair<FGameplayTag, FDelegateHandle>& Pair : Entry.Tags)
		{
			ASC->RegisterGameplayTagEvent(Pair.Key, EGameplayTagEventType::NewOrRemoved).Remove(Pair.Value);
		}
	}

	Watched.Reset();
}

int32 UVBPlayBotSubsystem::CurrentHitIndex() const
{
	const ACharacter* P = Player.Get();
	UAnimInstance* Anim = P && P->GetMesh() ? P->GetMesh()->GetAnimInstance() : nullptr;
	const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	if (!Montage)
	{
		return INDEX_NONE;
	}

	const int32 Section = Montage->GetSectionIndex(Anim->Montage_GetCurrentSection(Montage));
	return Section == INDEX_NONE ? INDEX_NONE : Section + 1;
}

void UVBPlayBotSubsystem::HandleWindowEvent(const FGameplayEventData* Payload, bool bOpen)
{
	if (Stage != EStage::Run)
	{
		return;
	}

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"%s\",\"f\":%lld,\"natural\":%.1f,\"hit\":%d}"),
		bOpen ? TEXT("window_open") : TEXT("window_close"), Frame, Payload ? Payload->EventMagnitude : -1.f, CurrentHitIndex()));
}

void UVBPlayBotSubsystem::HandleHitEvent(const FGameplayEventData* Payload, TWeakObjectPtr<AActor> Victim)
{
	if (Stage != EStage::Run)
	{
		return;
	}

	FVBPlayBotHit Hit;
	Hit.Victim = Victim;
	Hit.Instigator = Payload ? const_cast<AActor*>(Payload->Instigator.Get()) : nullptr;
	const ACharacter* P = Player.Get();
	const AActor* Instigator = Hit.Instigator.Get();
	Hit.bFromPlayer = VBPlayBotDamageEvidence::IsFromPlayer(Instigator,Victim.Get(),P,P ? P->GetPlayerState() : nullptr);
	Hit.SectionHit = Hit.bFromPlayer ? CurrentHitIndex() : INDEX_NONE;
	Hit.Damage = Payload ? Payload->EventMagnitude : 0.f;
	Hit.Frame = Frame;
	if (Payload)
	{
		for (const FGameplayTag& Tag : Payload->InstigatorTags)
		{
			Hit.Impact = Tag.ToString();
		}
	}

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"hit\",\"f\":%lld,\"victim\":\"%s\",\"instigator\":\"%s\",\"fromPlayer\":%s,\"hit\":%d,\"impact\":\"%s\",\"damage\":%.3f}"),
		Frame, *Ctx.Label(Victim.Get()), *Ctx.Label(Instigator), Hit.bFromPlayer ? TEXT("true") : TEXT("false"), Hit.SectionHit, *Hit.Impact, Hit.Damage));
	// 시나리오 대상이 아닌 공격자가 끼면 실행이 깨끗하지 않다고 표시한다
	if (Victim.Get() == Player.Get() && Instigator && Instigator != Ctx.Target.Get())
	{
		++NonTargetHits;
		if (FirstNonTarget.IsEmpty())
		{
			FirstNonTarget = FString::Printf(TEXT("%s at %lld"), *Instigator->GetName(), Frame);
		}
	}

	if (Hit.bFromPlayer)
	{
		++Ctx.HitsByPlayer.FindOrAdd(Victim);
		Ctx.LastPlayerHitFrame = Frame;
		for (int32 RuleIndex = 0; RuleIndex < ImpactFxRules.Num(); ++RuleIndex)
		{
			if (ImpactFxRules[RuleIndex].Impact != Hit.Impact)
			{
				continue;
			}

			++ImpactFxRules[RuleIndex].Hits;
			PendingImpactFx.Emplace(RuleIndex, Frame);
		}
	}

	if (Ctx.OnHit)
	{
		Ctx.OnHit(Hit);
	}
}

void UVBPlayBotSubsystem::HandleTag(const FGameplayTag Tag, int32 Count, TWeakObjectPtr<AActor> Owner)
{
	if (Stage != EStage::Run)
	{
		return;
	}

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"tag\",\"f\":%lld,\"actor\":\"%s\",\"tag\":\"%s\",\"count\":%d}"),
		Frame, Owner.Get() == Player.Get() ? TEXT("p") : *Ctx.Label(Owner.Get()), *Tag.ToString(), Count));
}

void UVBPlayBotSubsystem::HandleButton(const FVBPlayBotPress& Press, bool bDown)
{
	Emit(FString::Printf(
		TEXT("{\"k\":\"in\",\"f\":%lld,\"id\":%d,\"key\":\"%s\",\"action\":\"%s\",\"dir\":\"%s\",\"cue\":\"%s\",\"cue_f\":%lld,\"obs_f\":%lld,")
		TEXT("\"pred_f\":%lld,\"pred_cue_f\":%lld,\"delay_ms\":%.3f,\"err_ms\":%.3f,\"due_f\":%lld,\"inj_f\":%lld,\"rel_f\":%lld,")
		TEXT("\"hold_f\":%d,\"late\":%s,\"path\":\"%s\",\"cancel\":\"%s\"}"),
		Frame, Press.Id, *Press.Key.ToString(), *Press.Action.ToString(), bDown ? TEXT("down") : TEXT("up"), *Press.Cue.ReplaceCharWithEscapedChar(),
		Press.CueFrame, Press.ObservedFrame, Press.PredictionFrame, Press.PredictedCueFrame, Press.DelayMs, Press.ErrorMs,
		Press.DueFrame, Press.InjectedFrame, Press.ReleasedFrame, Press.HoldFrames, Press.bLate ? TEXT("true") : TEXT("false"),
		Input.IsKeyboardFallback(Press.Action) ? TEXT("keyboard_fallback") : *FVBPlayBotInputDriver::PathName(), *Press.Cancel));
	if (bDown && Ctx.OnPressInjected)
	{
		Ctx.OnPressInjected(Press);
	}
}

FString UVBPlayBotSubsystem::ActorJson(ACharacter* Character, bool bPlayer) const
{
	if (!Character)
	{
		return TEXT("null");
	}

	const UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	UAnimInstance* Anim = Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
	const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	FString SectionName;
	double SectionPos = -1.0;
	if (Montage)
	{
		const FName Section = Anim->Montage_GetCurrentSection(Montage);
		const int32 Index = Montage->GetSectionIndex(Section);
		float Start = 0.f;
		float End = 0.f;
		if (Index != INDEX_NONE)
		{
			Montage->GetSectionStartAndEndTime(Index, Start, End);
		}

		SectionName = Section.ToString();
		SectionPos = Anim->Montage_GetPosition(Montage) - Start;
	}

	TArray<FString> Tags;
	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Character))
	{
		for (const FGameplayTag& Tag : Config.TrackTags)
		{
			if (ASC->HasMatchingGameplayTag(Tag))
			{
				Tags.Add(FString::Printf(TEXT("\"%s\""), *Tag.ToString()));
			}
		}
	}

	const bool bRootMotion = Move && (Move->HasAnimRootMotion() || Move->CurrentRootMotion.HasActiveRootMotionSources());
	FString Extra;
	if (bPlayer)
	{
		const AController* Controller = Character->GetController();
		Extra = FString::Printf(TEXT(",\"acc\":%s,\"ctrlYaw\":%.3f"), Move ? *Vec(Move->GetCurrentAcceleration()) : TEXT("null"),
			Controller ? Controller->GetControlRotation().Yaw : 0.0);
	}

	return FString::Printf(TEXT("{\"name\":\"%s\",\"pos\":%s,\"vel\":%s,\"yaw\":%.3f,\"mm\":%d,\"rm\":%s,\"mon\":\"%s\",\"sec\":\"%s\",\"sp\":%.4f,\"ad\":%.4f,\"tags\":[%s]%s}"),
		*Character->GetName(), *Vec(Character->GetActorLocation()), *Vec(Character->GetVelocity()), Character->GetActorRotation().Yaw,
		Move ? static_cast<int32>(Move->MovementMode) : -1, bRootMotion ? TEXT("true") : TEXT("false"),
		Montage ? *Montage->GetName() : TEXT(""), *SectionName, SectionPos, Character->CustomTimeDilation,
		*FString::Join(Tags, TEXT(",")), *Extra);
}

void UVBPlayBotSubsystem::SampleFrame()
{
	UWorld* World = Ctx.World;
	ACharacter* P = Player.Get();
	const APlayerCameraManager* Camera = Ctx.PC ? Ctx.PC->PlayerCameraManager.Get() : nullptr;

	TArray<FString> NewFX;
	int32 LiveFX = 0;
	TSet<TWeakObjectPtr<UFXSystemComponent>> Live;
	for (TObjectIterator<UFXSystemComponent> It; It; ++It)
	{
		UFXSystemComponent* FX = *It;
		if (!IsValid(FX) || FX->GetWorld() != World || !FX->IsActive())
		{
			continue;
		}

		++LiveFX;
		Live.Add(FX);
		if (!KnownFX.Contains(FX))
		{
			const UFXSystemAsset* Asset = FX->GetFXSystemAsset();
			NewFX.Add(FString::Printf(TEXT("\"%s\""), Asset ? *Asset->GetName() : *FX->GetName()));
		}
	}

	KnownFX = MoveTemp(Live);
	int32 Decals = 0;
	for (TObjectIterator<UDecalComponent> It; It; ++It)
	{
		if (IsValid(*It) && It->GetWorld() == World && It->IsRegistered() && It->IsVisible())
		{
			++Decals;
		}
	}

	Ctx.NewFX.Reset();
	for (const FString& Quoted : NewFX)
	{
		Ctx.NewFX.Add(Quoted.Mid(1, Quoted.Len() - 2));
	}

	Ctx.DecalCount = Decals;
	SettleImpactFx(false);

	TArray<FString> Buttons;
	for (const FString& Key : Input.GetDownKeys())
	{
		Buttons.Add(FString::Printf(TEXT("\"%s\""), *Key));
	}

	const FVector2D& Want = Input.GetStickWant();
	const FVector2D& Out = Input.GetStickOut();
	const FVector2D& Look = Input.GetLookOut();
	const FString CameraJson = Camera
		? FString::Printf(TEXT("{\"loc\":%s,\"rot\":[%.4f,%.4f,%.4f],\"fov\":%.4f,\"view\":\"%s\"}"), *Vec(Camera->GetCameraLocation()),
			Camera->GetCameraRotation().Pitch, Camera->GetCameraRotation().Yaw, Camera->GetCameraRotation().Roll, Camera->GetFOVAngle(),
			*Ctx.Label(Camera->GetViewTarget()))
		: FString(TEXT("null"));
	// modifier 가 알리는 화면 피드백 상태 - null 은 이 카메라에 modifier 가 없다는 뜻(꺼짐이 아니라 알 수 없음)
	const UVBCombatFeedbackModifier* Feedback = Camera
		? Cast<UVBCombatFeedbackModifier>(const_cast<APlayerCameraManager*>(Camera)->FindCameraModifierByClass(UVBCombatFeedbackModifier::StaticClass()))
		: nullptr;
	const TCHAR* FeedbackJson = Feedback ? (Feedback->IsFeedbackActive() ? TEXT("true") : TEXT("false")) : TEXT("null");
	Emit(FString::Printf(
		TEXT("{\"k\":\"f\",\"f\":%llu,\"uf\":%lld,\"gt\":%.5f,\"rt\":%.5f,\"gd\":%.4f,\"fb\":%s,\"p\":%s,\"t\":%s,\"cam\":%s,\"fxN\":%d,\"fxNew\":[%s],\"decN\":%d,")
		TEXT("\"stick\":{\"want\":[%.4f,%.4f],\"out\":[%.4f,%.4f],\"look\":[%.4f,%.4f]},\"btn\":[%s]}"),
		GFrameCounter, Frame, World->GetTimeSeconds(), FPlatformTime::Seconds(),
		World->GetWorldSettings() ? World->GetWorldSettings()->TimeDilation : 1.f, FeedbackJson,
		*ActorJson(P, true), *ActorJson(Ctx.Target.Get(), false), *CameraJson, LiveFX, *FString::Join(NewFX, TEXT(",")), Decals,
		Want.X, Want.Y, Out.X, Out.Y, Look.X, Look.Y, *FString::Join(Buttons, TEXT(","))));
}

bool UVBPlayBotSubsystem::ParseImpactFxRules(FString& OutError)
{
	for (const FString& Line : Config.GetArray(TEXT("ImpactFx")))
	{
		FString Impact, Fx;
		if (!Line.Split(TEXT("|"), &Impact, &Fx) || Impact.TrimStartAndEnd().IsEmpty() || Fx.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("[%s] ImpactFx '%s' must be <impact tag>|<effect asset name>"), *Config.Section, *Line);
			return false;
		}

		FImpactFxRule& Rule = ImpactFxRules.AddDefaulted_GetRef();
		Rule.Impact = Impact.TrimStartAndEnd();
		Rule.Fx = Fx.TrimStartAndEnd();
	}
	// 잰 타격 -> 이펙트 간격은 0프레임이고, 기본값은 표본 한 프레임을 더 허용한다
	ImpactFxGraceFrames = Config.GetInt(TEXT("ImpactFxGraceFrames"), 1);
	if (ImpactFxGraceFrames < 0)
	{
		OutError = FString::Printf(TEXT("[%s] ImpactFxGraceFrames must not be negative"), *Config.Section);
		return false;
	}

	return true;
}

void UVBPlayBotSubsystem::SettleImpactFx(bool bRunEnded)
{
	// 생긴 이펙트 하나가 오래된 타격 하나를 채운다 - 한 프레임에 두 대상이면 이펙트도 둘이어야 한다
	TArray<FString> Unclaimed = Ctx.NewFX;
	for (int32 Index = 0; Index < PendingImpactFx.Num();)
	{
		const int32 RuleIndex = PendingImpactFx[Index].Key;
		const int64 HitFrame = PendingImpactFx[Index].Value;
		FImpactFxRule& Rule = ImpactFxRules[RuleIndex];
		if (!bRunEnded && Unclaimed.RemoveSingle(Rule.Fx) > 0)
		{
			++Rule.Found;
		}
		else if (bRunEnded || Frame - HitFrame >= ImpactFxGraceFrames)
		{
			// 실행이 끝날 때 남은 타격도 놓친 것으로 본다 - 모름은 통과가 아니다
			if (Rule.FirstMissFrame < 0 || HitFrame < Rule.FirstMissFrame)
			{
				Rule.FirstMissFrame = HitFrame;
			}
		}
		else
		{
			++Index;
			continue;
		}

		PendingImpactFx.RemoveAt(Index);
	}
}

void UVBPlayBotSubsystem::ReportImpactFx()
{
	for (const FImpactFxRule& Rule : ImpactFxRules)
	{
		const FString Name = TEXT("impact_fx_") + Rule.Fx;
		if (Rule.Hits == 0)
		{
			Checks.Unavailable(Name, Frame, FString::Printf(TEXT("no player hit with %s"), *Rule.Impact));
			continue;
		}

		Checks.Expect(Name, Rule.Found == Rule.Hits, true, Rule.FirstMissFrame >= 0 ? Rule.FirstMissFrame : Frame,
			FString::Printf(TEXT("{\"impact\":\"%s\",\"hits\":%d,\"found\":%d,\"firstMiss\":%lld}"), *Rule.Impact, Rule.Hits, Rule.Found, Rule.FirstMissFrame));
	}
}

void UVBPlayBotSubsystem::FinishRun()
{
	if (bEnded)
	{
		return;
	}
#if CSV_PROFILER
	// CSV profiler 는 프레임 경계에서 캡처를 끝내고 비동기로 쓰므로 몇 프레임 더 tick 한다
	if (bCsvCapturing && Stage != EStage::Draining)
	{
		CsvFile = FCsvProfiler::Get()->EndCapture();
		Stage = EStage::Draining;
		StageFrame = Frame;
		return;
	}

	if (bCsvCapturing)
	{
		const bool bWritten = CsvFile.IsValid() && CsvFile.IsReady();
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"csv\",\"f\":%lld,\"written\":%s,\"file\":\"%s\"}"), Frame,
			bWritten ? TEXT("true") : TEXT("false"), bWritten ? *CsvFile.Get().ReplaceCharWithEscapedChar() : TEXT("")));
		bCsvCapturing = false;
	}
#endif
	bEnded = true;
	if (Scenario)
	{
		Scenario->Teardown(Ctx);
	}

	Overrides.RestoreAll();
	Input.ReleaseAll(Frame);
	UnbindEvents();
	// 값을 바꾼 실행만 되돌릴 것이 있고, 저작값 그대로인 실행에는 복구 검사가 없다
	const FVBPlayBotRestoreReport& Restore = Overrides.GetRestoreReport();
	if (Restore.HasEntries())
	{
		Checks.Expect(TEXT("overrides_restored"), Restore.Failures.IsEmpty(), true, Frame, Restore.ToJson());
	}
	// 표본 프레임마다 이미지 하나를 요청했으니 모두 도착해야 한다 - 영상이나 프레임이 빠지면 시각 증거를 못 쓴다 (허용 오차 없음)
	const int64 Expected = RequestedFrames;
	const bool bVideoComplete = Recorder.GetCapturedFrames() > 0 && Recorder.GetCapturedFrames() == Expected;
	Checks.Expect(TEXT("video_complete"), bVideoComplete, true, Frame,
		FString::Printf(TEXT("{\"captured\":%lld,\"requested\":%lld}"), Recorder.GetCapturedFrames(), Expected));
	if (Config.GetInt(TEXT("AllowOtherAttackers"), 0) == 0)
	{
		Checks.Expect(TEXT("no_hit_from_non_target"), NonTargetHits == 0, true, Frame,
			FString::Printf(TEXT("{\"hits\":%d,\"first\":\"%s\"}"), NonTargetHits, *FirstNonTarget.ReplaceCharWithEscapedChar()));
	}

	SettleImpactFx(true);
	ReportImpactFx();
	// 마지막 검사 뒤에 불러야 모든 검사(video_complete 포함)의 강제 이름이 쓰인 것으로 센다
	Checks.ReportUnusedForceWrong(Frame);
	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"run_end\",\"f\":%lld,\"captured\":%lld,\"pass\":%d,\"fail\":%d,\"unavailable\":%d,\"looks\":%d}"),
		Frame, Recorder.GetCapturedFrames(), Checks.GetPassed(), Checks.GetFailed(), Checks.GetUnavailable(), Checks.GetLooks()));
	Recorder.Close();
	const bool bRecorderOk = !Recorder.HasFailed();
	const FString Summary = bRecorderOk
		? FString::Printf(TEXT("%s pass=%d fail=%d unavailable=%d looks=%d captured=%lld"), *Checks.StatusWord(), Checks.GetPassed(),
			Checks.GetFailed(), Checks.GetUnavailable(), Checks.GetLooks(), Recorder.GetCapturedFrames())
		: FString::Printf(TEXT("recorder_failed %s"), *Recorder.GetFailure());
	FFileHelper::SaveStringToFile(Summary, *FPaths::Combine(Option(TEXT("VBPlayBotOut=")), TEXT("summary.txt")));
	UE_LOG(LogVBPlayBot, Log, TEXT("PlayBot finished: %s"), *Summary);
	FPlatformMisc::RequestExitWithStatus(false, bRecorderOk && Checks.StatusWord() != TEXT("fail") ? 0 : 1);
}

void UVBPlayBotSubsystem::AbortAfterCapture(const FString& Reason)
{
	if (!bSampledThisFrame)
	{
		Abort(Reason);
		return;
	}

	if (PendingAbort.IsEmpty())
	{
		PendingAbort = Reason.IsEmpty() ? TEXT("aborted without a reason") : Reason;
	}
}

void UVBPlayBotSubsystem::Abort(const FString& Reason)
{
	if (bEnded)
	{
		return;
	}

	bEnded = true;
	if (Scenario)
	{
		Scenario->Teardown(Ctx);
	}

	Overrides.RestoreAll();
	Input.ReleaseAll(Frame);
	UnbindEvents();
	const FVBPlayBotRestoreReport& Restore = Overrides.GetRestoreReport();
	if (Restore.HasEntries())
	{
		Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"restore\",\"f\":%lld,\"report\":%s}"), Frame, *Restore.ToJson()));
		UE_CLOG(!Restore.Failures.IsEmpty(), LogVBPlayBot, Error, TEXT("PlayBot could not restore overrides: %s"),
			*FString::Join(Restore.Failures, TEXT("; ")));
	}

	Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"abort\",\"f\":%lld,\"reason\":\"%s\"}"), Frame, *Reason.ReplaceCharWithEscapedChar()));
	Recorder.Close();
	const FString OutDir = Option(TEXT("VBPlayBotOut="));
	if (!OutDir.IsEmpty())
	{
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("abort %s"), *Reason), *FPaths::Combine(OutDir, TEXT("summary.txt")));
	}

	UE_LOG(LogVBPlayBot, Error, TEXT("PlayBot aborted: %s"), *Reason);
	FPlatformMisc::RequestExitWithStatus(false, 2);
}
