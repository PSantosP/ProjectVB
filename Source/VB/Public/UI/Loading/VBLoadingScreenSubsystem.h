// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/Loading/VBLoadingMoviePort.h"
#include "VBLoadingScreenSubsystem.generated.h"

class IInputProcessor;
class SVBLoadingScreen;
class SWidget;
class UWorld;
struct FWorldContext;

DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnLoadingScreenShowingChanged, bool /*bShowing*/);

/**
 * 로딩 화면을 띄울지 결정하는 유일한 담당 클래스(Lyra ULoadingScreenManager). 레벨 이동 뒤에도 유지되어야 해서 게임 인스턴스에 있다.
 * 사유: 맵 여는 중 / 준비 과정(IVBLoadingProcess) 미완 / 끝난 뒤 유지 시간. 떠 있는 동안 입력을 먹는다.
 * 한 에피소드가 최대 시간을 초과하면 화면을 내리고 Error 를 한 번 남기며, 그 에피소드에서는 다시 안 띄운다.
 */
UCLASS()
class VB_API UVBLoadingScreenSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	static UVBLoadingScreenSubsystem* Get(const UObject* WorldContextObject);

	// 전용 서버에는 만들지 않는다 - 그릴 화면이 없다.
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 준비 과정 등록(약한 참조). IVBLoadingProcess 를 구현해야 한다. 같은 객체를 두 번 넣지 않는다.
	void RegisterProcess(UObject* Process);
	void UnregisterProcess(UObject* Process);

	// 다음 틱을 기다리지 않고 지금 다시 판정한다. bSkipHold 면 사유가 없을 때 유지, 흐림 없이 바로 걷는다.
	void ReevaluateNow(bool bSkipHold = false);

	// 로딩 무비 재생 중도 보이는 것으로 친다.
	bool IsShowing() const { return bShowing; }

	// 실제로 보임이 바뀐 뒤 한 번 알린다. 구독자는 처음 상태를 IsShowing 으로 맞춘다.
	FVBOnLoadingScreenShowingChanged OnShowingChanged;
	// 지금 붙잡고 있는 사유(없으면 빈 글). 로그, 디버그용.
	const FString& GetCurrentReason() const { return CurrentReason; }

	//~FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~End FTickableGameObject

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBLoadingScreenTestAccess;
	// 시험이 시계를 주입한다(실시간 초). 비면 FPlatformTime.
	TFunction<double()> NowForTests;
	// 참이면 위젯, 입력 막이를 건너뛴다(뷰포트 없는 시험).
	bool bHeadlessForTests = false;
	// 명령줄 표시 정책만 덮어쓴다.
	TOptional<bool> NoLoadingScreenForTests;
#endif

	double Now() const;
	// 사유를 모아 띄울지 정한다. 사유가 있으면 OutReason 에 그 이름.
	bool CollectReason(FString& OutReason) const;
	void Evaluate(bool bSkipHold);
	bool ShouldDisplayInThisProcess() const;
	// 지금 그릴 수 있나. 못 그리면 띄움으로 치지 않는다(안 보이는 화면이 입력만 잠그지 않게).
	bool CanDisplayNow() const;
	void Show();
	void Hide();
	void StartBlockingInput();
	void StopBlockingInput();
	TSharedRef<SVBLoadingScreen> MakeLoadingWidget(float FadeInSeconds) const;
	// 무비가 끝나면 엔진이 초점을 뷰포트로 옮기므로 걷을 때 맨 위 화면 초점을 되돌린다.
	void RestoreScreenFocus() const;

	void HandlePreLoadMap(const FWorldContext& WorldContext, const FString& MapName);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);
	void EndMapLoad(const TCHAR* Why);

	// 남의 무비 위에는 설치하지 않는다. 지우는 것은 내가 설치하고 시작 안 된 것뿐이다.
	bool CanInstallMovie() const;
	void InstallMovie(float FadeInSeconds);
	void ClearMovieIfNotStarted(const TCHAR* Why);
	void BindMoviePort();
	void HandleMovieStarted();
	void HandleMovieFinished();

	TArray<TWeakObjectPtr<UObject>> Processes;

	bool bMapLoading = false;
	// 게임 맵 로드 뒤 로컬 PC 가 준비될 때까지 기다리는 구간(메뉴, 원격 클라 제외).
	bool bStartPhase = false;
	bool bShowing = false;
	// 에피소드: 사유가 있다가 다 없어질 때까지 한 번.
	bool bEpisodeActive = false;
	bool bEpisodeShown = false;
	bool bEpisodeAbandoned = false;
	double EpisodeStartSeconds = 0.0;
	double ShownAtSeconds = 0.0;
	// 사유가 다 끝난 뒤 이 시각까지 유지한다. 0 = 유지 없음.
	double HoldUntilSeconds = 0.0;
	FString CurrentReason;

	TSharedPtr<SWidget> Widget;
	TSharedPtr<IInputProcessor> InputBlocker;

	TUniquePtr<IVBLoadingMoviePort> MoviePort;
	bool bMovieInstalledByMe = false;
	bool bMovieStarted = false;
	// 무비 아래 붙인 위젯은 불투명으로 시작한다(무비가 끝날 때 흐림이 안 보이게).
	bool bShowNextWithoutFade = false;

	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle TravelFailureHandle;
};
