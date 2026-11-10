// UVBGASPLocomotionStateLibrary 의 상태 없는 규칙: 재생 단계, 재생 태그, 피벗 대체 래치, 전이 해제 판단.

#include "Animation/VBGASPLocomotionStateLibrary.h"

#include "VBGASPStateFields.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include <atomic>

EVBGASPCMCPhase UVBGASPLocomotionStateLibrary::GetCMCPhase(FName AuthoredState)
{
	if (AuthoredState == TEXT("Idle Loop") || AuthoredState == TEXT("Locomotion Loop"))
	{
		return EVBGASPCMCPhase::Loop;
	}

	if (AuthoredState == TEXT("Transition to Idle Loop") || AuthoredState == TEXT("Transition to Locomotion Loop"))
	{
		return EVBGASPCMCPhase::Transition;
	}

	return EVBGASPCMCPhase::Any;
}

EVBGASPCMCPhase UVBGASPLocomotionStateLibrary::GetCMCSelectionPhase(FName AuthoredState, float VerticalSpeed)
{
	const bool bIsInAirSelection = AuthoredState == TEXT("In Air Loop") || AuthoredState == TEXT("Transition to In Air Loop");
	if (bIsInAirSelection && FMath::IsFinite(VerticalSpeed) && VerticalSpeed < 10.0f)
	{
		return EVBGASPCMCPhase::Loop;
	}

	return GetCMCPhase(AuthoredState);
}

void UVBGASPLocomotionStateLibrary::ApplyCMCWeaponPhase(FVBGASPCMCWeaponQuery& Query,
	FName AuthoredState, EVBGASPCMCPhase Phase)
{
	if (AuthoredState == TEXT("Idle Loop") || AuthoredState == TEXT("Transition to Idle Loop"))
	{
		Query.MMState = EVBMMState::Idle;
	}

	if (AuthoredState == TEXT("Locomotion Loop") || AuthoredState == TEXT("Transition to Locomotion Loop"))
	{
		Query.MMState = EVBMMState::Moving;
	}

	if (Phase == EVBGASPCMCPhase::Loop)
	{
		// 반복 재생은 MM 의 일회성 판정과 무관하게 둔다. 아니면 피벗 전용 풀에서 Loop 가 영영 빈다.
		Query.IsStarting = Query.IsPivoting = Query.ShouldSpinTransition = Query.ShouldTurnInPlace = false;
		Query.JustTraversed = Query.JustLandedLight = Query.JustLandedHeavy = false;
	}
}

TArray<FName> UVBGASPLocomotionStateLibrary::ProjectCMCPlaybackTags(const TArray<FName>& PlaybackTags,
	const TArray<FName>& DatabaseTags)
{
	TArray<FName> Result = DatabaseTags;
	for (FName Tag : PlaybackTags)
	{
		if (Tag == TEXT("Start"))
		{
			Result.AddUnique(TEXT("Starts"));
		}
		else if (Tag == TEXT("Pivot"))
		{
			Result.AddUnique(TEXT("Pivots"));
		}
		else if (Tag == TEXT("Stop"))
		{
			Result.AddUnique(TEXT("Stops"));
		}
		else
		{
			Result.AddUnique(Tag);
		}
	}

	return Result;
}

bool UVBGASPLocomotionStateLibrary::StepPivotFallbackLatch(bool bLatched, bool bFellBackToLoop, bool bRawPivoting, bool bStanceChanged)
{
	if (!bRawPivoting || bStanceChanged)
	{
		return false;
	}

	return bLatched || bFellBackToLoop;
}

void UVBGASPLocomotionStateLibrary::RefreshPivotFallbackLatch(UAnimInstance* Instance, FVBGASPCMCState& State)
{
	if (!State.bPivotFallbackLatched || !Instance)
	{
		return;
	}

	if (HasPivotFallbackOwnerChanged(State))
	{
		State.bPivotFallbackLatched = false;
		return;
	}

	const bool bCrouchedNow = VBGASP::FStateView(Instance).Is(TEXT("Stance"), TEXT("Crouch"));
	State.bPivotFallbackLatched = StepPivotFallbackLatch(true, false, UVBGASPLocomotionLibrary::IsPivotingRaw(Instance),
		bCrouchedNow != State.bPivotFallbackCrouched);
}

bool UVBGASPLocomotionStateLibrary::HasPivotFallbackOwnerChanged(const FVBGASPCMCState& State)
{
	return State.bSelectedWeaponLocomotion != State.bUsesWeaponLocomotion || State.SelectedWeaponPool != State.CapturedWeaponPool;
}

bool UVBGASPLocomotionStateLibrary::ShouldReleaseCMCTransition(EVBGASPCMCPhase Phase,
	bool bLooping, bool bToLoopNotify, float TimeRemaining, float BlendTime)
{
	if (Phase != EVBGASPCMCPhase::Transition)
	{
		return false;
	}

	return bLooping || bToLoopNotify || (FMath::IsFinite(TimeRemaining) &&
		TimeRemaining <= FMath::Max(0.0f, BlendTime));
}

bool UVBGASPLocomotionStateLibrary::ShouldReselectCMCForGait(EVBGASPCMCPhase Phase, bool bLooping,
	EVBGait SelectedGait, EVBGait WantedGait)
{
	return Phase == EVBGASPCMCPhase::Transition && !bLooping && SelectedGait != WantedGait;
}

EVBGait UVBGASPLocomotionStateLibrary::GetCMCSelectionGait(const FVBGASPCMCState& State)
{
	if (const UVBAnimInstance* Main = Cast<UVBAnimInstance>(State.SelectionContext.Get()))
	{
		return Main->GetCurrentGait();
	}
	// 메인이 아니면 한 프레임 늦은 사본으로 비교한다.
	static std::atomic<bool> bWarned = false;
	if (!bWarned.exchange(true))
	{
		UE_LOG(LogVBGASPState, Warning,
			TEXT("CMC selection context %s is not UVBAnimInstance - the gait check uses the one-frame-old query copy"),
			*GetNameSafe(State.SelectionContext.Get()));
	}

	return State.WeaponQuery.Gait;
}
