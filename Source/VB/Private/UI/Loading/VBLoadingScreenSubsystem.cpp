// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Loading/VBLoadingScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Data/VBGameFlowSettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Styling/CoreStyle.h"
#include "UI/Loading/SVBLoadingScreen.h"
#include "UI/Loading/VBLoadingProcess.h"
#include "UI/VBUISettings.h"
#include "VB/VB.h"

namespace VBLoadingScreen
{
	// 설정의 초 값. ClampMin 은 편집기에서만 막는다 - 손으로 고친 값, NaN 은 기본값, 하한 아래는 하한.
	float SanitizeSeconds(float Value, float Min, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Max(Min, Value) : Fallback;
	}

	// 로딩 화면이 떠 있는 동안 키, 패드, 마우스를 먹는다 - 아래 메뉴가 눌리지 않게(Lyra FLoadingScreenInputPreProcessor).
	//   에디터에서는 걸지 않는다(StartBlockingInput) - 에디터 창의 입력까지 먹는다.
	class FInputBlocker : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent&) override { return true; }
		// 뗌은 통과시킨다 - 화면이 뜰 때 누르고 있던 키, 버튼이 걷힌 뒤에도 눌린 채로 남지 않게.
		virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent&) override { return false; }
		virtual bool HandleAnalogInputEvent(FSlateApplication&, const FAnalogInputEvent&) override { return true; }
		virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent&) override { return true; }
		virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent&) override { return true; }
		virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent&) override { return false; }
		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication&, const FPointerEvent&) override { return true; }
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication&, const FPointerEvent&,
			const FPointerEvent*) override { return true; }
		virtual bool HandleMotionDetectedEvent(FSlateApplication&, const FMotionEvent&) override { return true; }
		virtual const TCHAR* GetDebugName() const override { return TEXT("VBLoadingScreenInputBlocker"); }
	};
}

UVBLoadingScreenSubsystem* UVBLoadingScreenSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UVBLoadingScreenSubsystem>() : nullptr;
}

bool UVBLoadingScreenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UVBLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this, &ThisClass::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	if (GEngine)
	{
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
	}

	MoviePort = VBLoadingMovie::MakeEnginePort();
	BindMoviePort();
}

void UVBLoadingScreenSubsystem::BindMoviePort()
{
	if (!MoviePort)
	{
		return;
	}
	// 엔진 알림은 이 서브시스템보다 오래 유지된다 - 약하게 잡고 Deinitialize 에서 해제한다.
	TWeakObjectPtr<UVBLoadingScreenSubsystem> WeakThis(this);
	MoviePort->Bind(
		[WeakThis]() { if (UVBLoadingScreenSubsystem* Self = WeakThis.Get()) { Self->HandleMovieStarted(); } },
		[WeakThis]() { if (UVBLoadingScreenSubsystem* Self = WeakThis.Get()) { Self->HandleMovieFinished(); } });
}

void UVBLoadingScreenSubsystem::Deinitialize()
{
	// 붙인 것과 같은 델리게이트에서 뗀다(Lyra 의 Deinitialize 는 다른 것을 뗀다).
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (GEngine)
	{
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	ClearMovieIfNotStarted(TEXT("로딩 주인 해체"));
	if (MoviePort)
	{
		MoviePort->Unbind();
		MoviePort.Reset();
	}

	Hide();
	StopBlockingInput();
	Processes.Reset();
	bMapLoading = false;
	bStartPhase = false;
	bEpisodeActive = false;
	Super::Deinitialize();
}

void UVBLoadingScreenSubsystem::RegisterProcess(UObject* Process)
{
	if (!Process || !Process->Implements<UVBLoadingProcess>())
	{
		VB_LOG(Warning, "[로딩] %s 는 준비 과정(IVBLoadingProcess)이 아니다 - 등록하지 않는다", *GetNameSafe(Process));
		return;
	}

	Processes.AddUnique(Process);
}

void UVBLoadingScreenSubsystem::UnregisterProcess(UObject* Process)
{
	Processes.RemoveAll([Process](const TWeakObjectPtr<UObject>& Entry) { return !Entry.IsValid() || Entry.Get() == Process; });
}

void UVBLoadingScreenSubsystem::ReevaluateNow(bool bSkipHold)
{
	Evaluate(bSkipHold);
}

double UVBLoadingScreenSubsystem::Now() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (NowForTests)
	{
		return NowForTests();
	}
#endif
	// 실시간. 일시정지 중에도 흐른다.
	return FPlatformTime::Seconds();
}

bool UVBLoadingScreenSubsystem::CollectReason(FString& OutReason) const
{
	if (bMapLoading)
	{
		OutReason = TEXT("맵 여는 중");
		return true;
	}
	// 조회 루프와 같이 살아 있는 월드의 PC 만 센다 - 옛 PC 가 새 맵의 등록 틈을 메우면 안 된다.
	const bool bHasLivePlayerProcess = Processes.ContainsByPredicate([](const TWeakObjectPtr<UObject>& Entry)
	{
		const UObject* Process = Entry.Get();
		const UWorld* World = Process ? Process->GetWorld() : nullptr;
		return Process && Process->IsA<APlayerController>() && World && !World->bIsTearingDown;
	});
	if (bStartPhase && !bHasLivePlayerProcess)
	{
		OutReason = TEXT("로컬 플레이어 기다림");
		return true;
	}
	// 판정 중에 등록이 바뀌어도 되게 복사본을 돈다.
	const TArray<TWeakObjectPtr<UObject>, TInlineAllocator<8>> Snapshot(Processes);
	for (const TWeakObjectPtr<UObject>& Entry : Snapshot)
	{
		const UObject* Process = Entry.Get();
		// 끝나는 월드의 과정은 빼고 본다 - 레벨을 옮기는 사이 옛 PC 가 준비 안 됨으로 화면을 붙잡지 않게.
		const UWorld* World = Process ? Process->GetWorld() : nullptr;
		if (!Process || (World && World->bIsTearingDown))
		{
			continue;
		}

		FString Reason;
		const IVBLoadingProcess* Asker = Cast<IVBLoadingProcess>(Process);
		if (Asker && Asker->ShouldShowLoadingScreen(Reason))
		{
			OutReason = FString::Printf(TEXT("%s: %s"), *GetNameSafe(Process), *Reason);
			return true;
		}
	}

	return false;
}

void UVBLoadingScreenSubsystem::Evaluate(bool bSkipHold)
{
	Processes.RemoveAll([](const TWeakObjectPtr<UObject>& Entry) { return !Entry.IsValid(); });

	FString Reason;
	const bool bWanted = CollectReason(Reason);
	const double Time = Now();
	if (bWanted)
	{
		if (!bEpisodeActive)
		{
			bEpisodeActive = true;
			bEpisodeShown = bShowing; // 유지 시간 중에 새 사유가 오면 그 화면을 그대로 잇는다
			bEpisodeAbandoned = false;
			EpisodeStartSeconds = Time;
		}

		HoldUntilSeconds = 0.0;
		CurrentReason = Reason;
		if (bEpisodeAbandoned)
		{
			return;
		}

		const float MaxShowSeconds = VBLoadingScreen::SanitizeSeconds(GetDefault<UVBUISettings>()->LoadingMaxShowSeconds, 1.0f, 60.0f);
		if (Time - EpisodeStartSeconds > MaxShowSeconds)
		{
			bEpisodeAbandoned = true;
			if (bShowing || bEpisodeShown)
			{
				// 영원히 막힌 화면은 게임을 끝낼 수밖에 없다 - 걷고 로그를 남긴다. 화면을 걷어도 준비, 저장 보호는 그대로다.
				VB_LOG(Error, "[로딩] %.0f초를 넘겼다 - 로딩 화면을 걷는다. 붙잡던 사유: %s", MaxShowSeconds, *Reason);
				Hide();
			}
			else
			{
				// 띄운 적 없는 에피소드는 걷을 화면이 없다(그릴 수 없는 실행 등). 준비가 안 끝난 사실만 남긴다.
				VB_LOG(Warning, "[로딩] %.0f초째 준비가 안 끝났다(화면은 띄우지 않았다). 붙잡는 사유: %s", MaxShowSeconds, *Reason);
			}

			return;
		}

		if (!bShowing && ShouldDisplayInThisProcess() && CanDisplayNow())
		{
			Show();
			bEpisodeShown = true;
		}

		return;
	}

	CurrentReason.Reset();
	// 남은 이유가 없다 = 등록된 PC 가 준비됐다고 답했다. 시작 단계를 끝낸다.
	bStartPhase = false;
	if (bEpisodeActive)
	{
		bEpisodeActive = false;
		// 유지는 실제로 띄웠던 에피소드 뒤에만. 시네마틱 직전이면 유지, 흐림 없이 바로 걷는다.
		const float HoldSeconds = VBLoadingScreen::SanitizeSeconds(GetDefault<UVBUISettings>()->LoadingHoldSeconds, 0.0f, 2.0f);
		HoldUntilSeconds = (bEpisodeShown && !bEpisodeAbandoned && !bSkipHold) ? Time + HoldSeconds : 0.0;
	}

	if (bShowing && (bSkipHold || !(Time < HoldUntilSeconds)))
	{
		Hide();
	}
}

bool UVBLoadingScreenSubsystem::ShouldDisplayInThisProcess() const
{
#if !UE_BUILD_SHIPPING
	// viewport, 시험 표시 조건보다 먼저 MoviePlayer 의 non-Shipping 끄기 옵션을 따른다.
	// Playbot 은 Slate 로 입력을 넣는다 - 이 화면이 뜨면 그 입력을 먹는다.
	bool bSuppressLoadingScreen = FCommandLine::IsInitialized() && FParse::Param(FCommandLine::Get(), TEXT("NoLoadingScreen"));
#if WITH_DEV_AUTOMATION_TESTS
	if (NoLoadingScreenForTests.IsSet())
	{
		bSuppressLoadingScreen = NoLoadingScreenForTests.GetValue();
	}
#endif
	if (bSuppressLoadingScreen)
	{
		return false;
	}
#endif
#if WITH_DEV_AUTOMATION_TESTS
	if (bHeadlessForTests)
	{
		return true;
	}
#endif
	if (IsRunningDedicatedServer())
	{
		return false;
	}

	return !GIsEditor || GetDefault<UVBUISettings>()->bShowLoadingScreenInEditor;
}

bool UVBLoadingScreenSubsystem::CanDisplayNow() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bHeadlessForTests)
	{
		return true;
	}
#endif
	// 그리는 것이 없는 실행(NullRHI, 헤드리스 시험)에서는 화면도 입력 막이도 걸지 않는다 - 보이지 않는 화면이 입력만 잠근다.
	return FApp::CanEverRender() && FSlateApplication::IsInitialized() && GetGameInstance() && GetGameInstance()->GetGameViewportClient();
}

void UVBLoadingScreenSubsystem::Show()
{
	bShowing = true;
	ShownAtSeconds = Now();
	VB_LOG(Log, "[로딩] 띄움 - %s", *CurrentReason);
#if WITH_DEV_AUTOMATION_TESTS
	if (bHeadlessForTests)
	{
		// 헤드리스 시험은 표시 판정, 신호만 본다. 실제 위젯, 입력 확인에는 viewport 가 필요하다.
		OnShowingChanged.Broadcast(true);
		return;
	}
#endif
	UGameViewportClient* Viewport = GetGameInstance()->GetGameViewportClient();
	const UVBUISettings* Settings = GetDefault<UVBUISettings>();
	Widget = MakeLoadingWidget(bShowNextWithoutFade ? 0.0f : Settings->LoadingFadeInSeconds);
	Viewport->AddViewportWidgetContent(Widget.ToSharedRef(), Settings->LoadingZOrder);
	StartBlockingInput();
	OnShowingChanged.Broadcast(true);
}

TSharedRef<SVBLoadingScreen> UVBLoadingScreenSubsystem::MakeLoadingWidget(float FadeInSeconds) const
{
	// viewport 와 movie 인스턴스가 같은 설정과 위젯 factory 를 쓴다.
	const UVBUISettings* Settings = GetDefault<UVBUISettings>();
	FSlateFontInfo Font = Settings->LoadingTextFont;
	if (!Font.HasValidFont())
	{
		Font = FCoreStyle::GetDefaultFontStyle("Regular", Font.Size);
	}

	return SNew(SVBLoadingScreen)
		.Text(Settings->LoadingText)
		.Font(Font)
		.TextColor(Settings->LoadingTextColor)
		.BackgroundColor(Settings->LoadingBackgroundColor)
		.FadeInSeconds(FadeInSeconds)
		.ContentPadding(Settings->LoadingContentPadding)
		.ThrobberSpacing(Settings->LoadingThrobberSpacing);
}

void UVBLoadingScreenSubsystem::Hide()
{
	if (!bShowing)
	{
		return;
	}

	bShowing = false;
	VB_LOG(Log, "[로딩] 걷음 - %.2f초 동안 떠 있었다", Now() - ShownAtSeconds);
	StopBlockingInput();
	if (Widget.IsValid())
	{
		if (UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
		}

		Widget.Reset();
	}

	RestoreScreenFocus();
	OnShowingChanged.Broadcast(false);
}

void UVBLoadingScreenSubsystem::RestoreScreenFocus() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		// 포커스, 입력 설정을 맨 위 활성 화면(돌아온 메뉴 포함)에 되돌린다. 헤드리스에는 router 가 없다.
		if (UCommonUIActionRouterBase* Router = LocalPlayer ? LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>() : nullptr)
		{
			Router->RefreshUIInputConfig();
			Router->RefreshActiveRootFocus();
		}
	}
}

void UVBLoadingScreenSubsystem::StartBlockingInput()
{
	if (GIsEditor || InputBlocker.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	InputBlocker = MakeShared<VBLoadingScreen::FInputBlocker>();
	FSlateApplication::Get().RegisterInputPreProcessor(InputBlocker, 0);
}

void UVBLoadingScreenSubsystem::StopBlockingInput()
{
	if (!InputBlocker.IsValid())
	{
		return;
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(InputBlocker);
	}

	InputBlocker.Reset();
}

void UVBLoadingScreenSubsystem::HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName)
{
	// 내 게임 인스턴스가 여는 맵만.
	if (WorldContext.OwningGameInstance != GetGameInstance())
	{
		return;
	}

	bMapLoading = true;
	// 준비 단계는 게임 맵에서만 시작한다. OpenLevel URL 은 long package name 을 쓴다.
	const FString GameplayLevel = GetDefault<UVBGameFlowSettings>()->GameplayLevel.GetLongPackageName();
	bStartPhase = !GameplayLevel.IsEmpty() && FPaths::GetBaseFilename(MapName) == FPaths::GetBaseFilename(GameplayLevel);
	const bool bWasShowing = bShowing;
	const bool bUseMovie = CanInstallMovie();
	// 맵 로딩 movie 아래에 불투명한 viewport 위젯을 붙인다.
	bShowNextWithoutFade = bUseMovie;
	Evaluate(false);
	bShowNextWithoutFade = false;
	if (bUseMovie && bShowing)
	{
		// 이미 보이는 화면을 덮는 movie 는 다시 fade in 하지 않는다.
		InstallMovie(bWasShowing ? 0.0f : GetDefault<UVBUISettings>()->LoadingFadeInSeconds);
		VB_LOG(Log, "[로딩] 맵 여는 동안 로딩 스레드가 그린다(MoviePlayer)");
		return;
	}
	// 남의 movie 가 재생 중이면 엔진이 viewport 를 다시 그리지 않으니 Slate 도 틱하지 않는다.
	if (MoviePort && MoviePort->IsPlaying())
	{
		return;
	}
	// 맵 열기는 동기라 이 안에서 게임 틱이 없다 - 띄운 프레임을 지금 한 번 그린다. 그다음 로드 동안은 멈춘 그림이다.
	//   Slate 틱 안에서 다시 돌리지 않는다.
	if (Widget.IsValid() && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsTicking())
	{
		VB_LOG(Log, "[로딩] 무비 길을 못 쓴다(에디터·-NoLoadingScreen·단일 코어·남의 무비) - 맵 여는 동안 멈춘 그림");
		FSlateApplication::Get().Tick();
	}
}

bool UVBLoadingScreenSubsystem::CanInstallMovie() const
{
	// 준비됐거나 재생 중인 내용은 그것을 건 쪽의 것이다 - 덮어쓰지 않는다.
	return MoviePort && ShouldDisplayInThisProcess() && MoviePort->IsAvailable() && !MoviePort->IsPlaying() && !MoviePort->IsPrepared();
}

void UVBLoadingScreenSubsystem::InstallMovie(float FadeInSeconds)
{
	MoviePort->Install(MakeLoadingWidget(FadeInSeconds));
	bMovieInstalledByMe = true;
	bMovieStarted = false;
}

void UVBLoadingScreenSubsystem::ClearMovieIfNotStarted(const TCHAR* Why)
{
	if (!bMovieInstalledByMe || bMovieStarted || !MoviePort)
	{
		// 시작된 재생은 엔진의 Post, WaitForMovieToFinish 정리가 맡는다 - attribute 를 비우면 그 분기를 건너뛴다.
		return;
	}
	// 설치가 시작되지 않았을 때 재생 실패와 잘못된 허용 판정을 가른다.
	VB_LOG(Warning, "[로딩] 무비 길을 설치했는데 재생이 시작되지 않았다(%s) - 내가 넣은 속성을 비운다", Why);
	MoviePort->Clear();
	bMovieInstalledByMe = false;
}

void UVBLoadingScreenSubsystem::HandleMovieStarted()
{
	if (bMovieInstalledByMe)
	{
		bMovieStarted = true;
	}
}

void UVBLoadingScreenSubsystem::HandleMovieFinished()
{
	// Finished 는 남의 movie 까지 포함한 전역 신호다. 남의 재생에는 내 플래그가 켜져 있을 수 없고, 엔진이 attribute 를 이미 되돌렸다.
	bMovieInstalledByMe = false;
	bMovieStarted = false;
}

void UVBLoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (!bMapLoading)
	{
		return;
	}

	if (!LoadedWorld)
	{
		// 엔진은 로드가 실패해도 null 로 Post 를 부른다 - 내가 연 로드가 끝난 것으로 친다. 어느 GI 의 실패인지 알 길이 없어
		//   여러 PIE 창에서는 남의 실패로 일찍 걷힐 수 있다(받아들임 - 이 신호뿐이다).
		EndMapLoad(TEXT("맵 로드 실패(Post 의 월드가 없다)"));
		return;
	}

	if (LoadedWorld->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	// 원격 클라는 권위 PC 과정을 등록하지 않는다 - 시작 단계를 열어 두면 영원히 기다린다.
	if (LoadedWorld->GetNetMode() == NM_Client)
	{
		bStartPhase = false;
	}

	EndMapLoad(nullptr);
}

void UVBLoadingScreenSubsystem::HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	if (bMapLoading && (!World || World->GetGameInstance() == GetGameInstance()))
	{
		EndMapLoad(TEXT("이동 실패"));
	}
}

void UVBLoadingScreenSubsystem::EndMapLoad(const TCHAR* Why)
{
	bMapLoading = false;
	ClearMovieIfNotStarted(Why ? Why : TEXT("맵 열기 끝"));
	if (Why)
	{
		// 실패한 이동은 게임에 들어가지 않았다 - 오지 않을 PC 의 시작 대기도 끝낸다.
		bStartPhase = false;
		VB_LOG(Warning, "[로딩] %s - 맵 여는 중 사유를 내린다", Why);
	}

	Evaluate(false);
}

void UVBLoadingScreenSubsystem::Tick(float DeltaTime)
{
	Evaluate(false);
}

bool UVBLoadingScreenSubsystem::IsTickable() const
{
	return !IsTemplate() && (bShowing || bMapLoading || bEpisodeActive || Processes.Num() > 0);
}

TStatId UVBLoadingScreenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVBLoadingScreenSubsystem, STATGROUP_Tickables);
}

UWorld* UVBLoadingScreenSubsystem::GetTickableGameObjectWorld() const
{
	return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
}
