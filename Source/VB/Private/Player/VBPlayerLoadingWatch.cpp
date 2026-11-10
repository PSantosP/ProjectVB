// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Player/VBPlayerLoadingWatch.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "UI/Loading/VBLoadingScreenSubsystem.h"

namespace
{
	void HandlePreparationChanged(const APlayerController& Controller)
	{
		// 준비 알림은 시작 RequestPlay 바로 전에 온다 - 그 경계에서는 hold, fade 를 동기로 건너뛴다.
		UVBLoadingScreenSubsystem* Loading = UVBLoadingScreenSubsystem::Get(&Controller);
		if (!Loading)
		{
			return;
		}

		const UWorld* World = Controller.GetWorld();
		const UVBCinematicStartupPolicy* Policy = World ? World->GetSubsystem<UVBCinematicStartupPolicy>() : nullptr;
		Loading->ReevaluateNow(Policy && Policy->IsRequestingStartupPlayback(&Controller));
	}
}

bool VBPlayerLoadingRules::ShouldShowLoadingScreen(const APlayerController& Controller, FString& OutReason)
{
	const APawn* ControlledPawn = Controller.GetPawn();
	if (!ControlledPawn)
	{
		OutReason = TEXT("폰 기다림");
		return true;
	}

	const UVBPlayerReadinessComponent* Readiness = ControlledPawn->FindComponentByClass<UVBPlayerReadinessComponent>();
	if (!Readiness)
	{
		// Pawn, 준비 컴포넌트가 생기기 전 틈도 기다림으로 쳐서 로딩 화면이 깜빡이지 않게 한다.
		OutReason = TEXT("폰 준비 기다림");
		return true;
	}

	return SelectLoadingReason(Readiness->Query(), OutReason);
}

bool VBPlayerLoadingRules::SelectLoadingReason(const FVBPlayerPreparationSnapshot& Snapshot, FString& OutReason)
{
	// 허용 목록의 이유만 로딩을 붙잡는다. blocked, unavailable mask 를 둘 다 읽는다.
	static const TPair<EVBPlayerPreparationReason, const TCHAR*> LoadingReasons[] = {
		{EVBPlayerPreparationReason::Initialization, TEXT("Initialization")},
		{EVBPlayerPreparationReason::SaveApplication, TEXT("SaveApplication")},
		{EVBPlayerPreparationReason::Restore, TEXT("Restore")},
		{EVBPlayerPreparationReason::AbilitySystem, TEXT("AbilitySystem")},
	};
	const uint32 Reasons = Snapshot.BlockedReasons | Snapshot.UnavailableReasons;
	for (const TPair<EVBPlayerPreparationReason, const TCHAR*>& Reason : LoadingReasons)
	{
		if (Reasons & static_cast<uint32>(Reason.Key))
		{
			OutReason = Reason.Value;
			return true;
		}
	}

	return false;
}

void FVBPlayerLoadingWatch::Register(APlayerController& Controller)
{
	if (UVBLoadingScreenSubsystem* Loading = UVBLoadingScreenSubsystem::Get(&Controller))
	{
		Loading->RegisterProcess(&Controller);
		bRegistered = true;
		RebindPreparationSource(Controller);
	}
}

void FVBPlayerLoadingWatch::HandlePawnChanged(APlayerController& Controller)
{
	if (bRegistered)
	{
		RebindPreparationSource(Controller);
		HandlePreparationChanged(Controller);
	}
}

void FVBPlayerLoadingWatch::Shutdown(APlayerController& Controller)
{
	if (UVBPlayerReadinessComponent* Readiness = BoundReadiness.Get())
	{
		Readiness->OnPreparationChanged.Remove(PreparationChangedHandle);
	}

	BoundReadiness.Reset();
	PreparationChangedHandle.Reset();
	if (bRegistered)
	{
		if (UVBLoadingScreenSubsystem* Loading = UVBLoadingScreenSubsystem::Get(&Controller))
		{
			Loading->UnregisterProcess(&Controller);
		}

		bRegistered = false;
	}
}

void FVBPlayerLoadingWatch::RebindPreparationSource(APlayerController& Controller)
{
	const APawn* ControlledPawn = Controller.GetPawn();
	UVBPlayerReadinessComponent* Next = ControlledPawn ? ControlledPawn->FindComponentByClass<UVBPlayerReadinessComponent>() : nullptr;
	if (BoundReadiness.Get() == Next)
	{
		return;
	}

	if (UVBPlayerReadinessComponent* Previous = BoundReadiness.Get())
	{
		Previous->OnPreparationChanged.Remove(PreparationChangedHandle);
	}

	PreparationChangedHandle.Reset();
	BoundReadiness = Next;
	if (Next)
	{
		APlayerController* ControllerPtr = &Controller;
		PreparationChangedHandle = Next->OnPreparationChanged.AddWeakLambda(ControllerPtr, [ControllerPtr]()
		{
			HandlePreparationChanged(*ControllerPtr);
		});
	}
}
