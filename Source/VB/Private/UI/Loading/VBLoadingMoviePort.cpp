// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Loading/VBLoadingMoviePort.h"

#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "MoviePlayer.h"
#include "Widgets/SWidget.h"

namespace
{
	class FVBEngineMoviePort final : public IVBLoadingMoviePort
	{
	public:
		virtual ~FVBEngineMoviePort() override { Unbind(); }

		virtual bool IsAvailable() const override
		{
			// 엔진 PlayMovie 의 진입 조건과 같다(MoviePlayer.cpp, DefaultGameMoviePlayer.cpp). FApp::CanEverRender 는 RHI 모듈 없이 NullRHI 를 거른다.
			return FCommandLine::IsInitialized() && IsMoviePlayerEnabled() && FApp::CanEverRender() && FPlatformMisc::NumberOfCores() > 1;
		}

		virtual bool IsPlaying() const override
		{
			const IGameMoviePlayer* Player = GetMoviePlayer();
			return Player && Player->IsMovieCurrentlyPlaying();
		}

		virtual bool IsPrepared() const override
		{
			const IGameMoviePlayer* Player = GetMoviePlayer();
			return Player && Player->LoadingScreenIsPrepared();
		}

		virtual void Install(const TSharedRef<SWidget>& Widget) override
		{
			FLoadingScreenAttributes Attributes;
			Attributes.WidgetLoadingScreen = Widget;
			// 동기 맵 로딩만 맡는다 - 로딩 뒤 준비는 viewport 가 맡는다(수동 정지, 엔진 tick 은 game thread 를 묶는다).
			Attributes.bAutoCompleteWhenLoadingCompletes = true;
			Attributes.bWaitForManualStop = false;
			Attributes.bAllowEngineTick = false;
			Attributes.bMoviesAreSkippable = false;
			// LoadingHoldSeconds 는 로딩 뒤 viewport 쪽 담당 클래스가 적용한다.
			Attributes.MinimumLoadingScreenDisplayTime = -1.0f;
			GetMoviePlayer()->SetupLoadingScreen(Attributes);
		}

		virtual void Clear() override
		{
			GetMoviePlayer()->SetupLoadingScreen(FLoadingScreenAttributes());
		}

		virtual void Bind(TFunction<void()> OnStarted, TFunction<void()> OnFinished) override
		{
			Unbind();
			IGameMoviePlayer* Player = GetMoviePlayer();
			StartedHandle = Player->OnMoviePlaybackStarted().AddLambda(MoveTemp(OnStarted));
			FinishedHandle = Player->OnMoviePlaybackFinished().AddLambda(MoveTemp(OnFinished));
		}

		virtual void Unbind() override
		{
			if (!StartedHandle.IsValid() && !FinishedHandle.IsValid())
			{
				return;
			}

			IGameMoviePlayer* Player = GetMoviePlayer();
			Player->OnMoviePlaybackStarted().Remove(StartedHandle);
			Player->OnMoviePlaybackFinished().Remove(FinishedHandle);
			StartedHandle.Reset();
			FinishedHandle.Reset();
		}

	private:
		FDelegateHandle StartedHandle;
		FDelegateHandle FinishedHandle;
	};
}

TUniquePtr<IVBLoadingMoviePort> VBLoadingMovie::MakeEnginePort()
{
	return MakeUnique<FVBEngineMoviePort>();
}
