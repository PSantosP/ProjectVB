// UVBGASPLocomotionStateLibrary 의 네이티브 CMC 선택: 재선택 판단, 선택 결과 적용, 강제 블렌드 소비.

#include "Animation/VBGASPLocomotionStateLibrary.h"

#include "VBGASPCMCSelection.h"
#include "VBGASPNodeLookup.h"
#include "VBGASPStateFields.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "Chooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"

namespace VBGASP
{
	namespace
	{
		bool HasLegacyProjection(const FStateView& State)
		{
			const FStateView Blend = State.Child(TEXT("BlendStackInputs"));
			const FProperty* Current = State.Find(TEXT("BlendStackInputs"));
			const FProperty* Previous = State.Find(TEXT("Previous_BlendStackInputs"));
			const FArrayProperty* Tags = CastField<FArrayProperty>(Blend.Find(TEXT("Tags")));
			return Current && Previous && Current->SameType(Previous)
				&& CastField<FObjectPropertyBase>(Blend.Find(TEXT("Anim")))
				&& CastField<FBoolProperty>(Blend.Find(TEXT("Loop")))
				&& CastField<FNumericProperty>(Blend.Find(TEXT("StartTime")))
				&& CastField<FNumericProperty>(Blend.Find(TEXT("BlendTime")))
				&& CastField<FObjectPropertyBase>(Blend.Find(TEXT("BlendProfile")))
				&& Tags && CastField<FNameProperty>(Tags->Inner)
				&& CastField<FBoolProperty>(State.Find(TEXT("NoValidAnim")));
		}

		bool ApplyCMCSelection(UAnimInstance* Instance, uint8 MachineState, bool bForceBlend,
			UAnimationAsset* Animation, const FVBGASPCMCChooserOutput& Output)
		{
			const FStateView View(Instance);
			FVBGASPCMCState* State = View.NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
			FVBGASPCMCBlendInputs* Inputs = View.NativePtr<FVBGASPCMCBlendInputs>(TEXT("CMCBlendInputs"));
			if (!State || !Inputs || !HasLegacyProjection(View)
				|| !UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output))
			{
				return false;
			}

			FVBGASPCMCBlendInputs Next;
			Next.Anim = Animation;
			UPoseSearchLibrary::IsAnimationAssetLooping(Animation, Next.bLoop);
			Next.StartTime = Output.StartTime;
			Next.bMirror = Output.bMirror;
			Next.BlendTime = Output.BlendTime;
			Next.BlendProfile = const_cast<UBlendProfile*>(Instance->GetBlendProfileByName(Output.BlendProfileName));
			Next.BlendCurve = Output.BlendCurve;
			Next.Tags = Output.Tags;
			if (!Output.BlendProfileName.IsNone() && !Next.BlendProfile)
			{
				return false;
			}
			// 같은 경사 자산이면 상태를 다시 골라도 계속 갱신된 파라미터를 유지한다.
			if (Next.Tags.Contains(TEXT("BS_Slope")))
			{
				Next.BlendParameters = Inputs->BlendParameters;
			}

			// 네이티브 명령이나 옛 뷰에 내기 전에 결과 전체를 검증한다.
			const bool bSelectionChanged = Inputs->Anim != Next.Anim || Inputs->bMirror != Next.bMirror ||
				!State->bHasSelection || State->SelectedState != MachineState || State->SelectedWeaponPool != State->CapturedWeaponPool;
			CopyField(View, TEXT("Previous_BlendStackInputs"), View, TEXT("BlendStackInputs"));
			const FStateView Blend = View.Child(TEXT("BlendStackInputs"));
			WriteObject(Blend, TEXT("Anim"), Next.Anim);
			WriteBool(Blend, TEXT("Loop"), Next.bLoop);
			WriteNumber(Blend, TEXT("StartTime"), Next.StartTime);
			WriteNumber(Blend, TEXT("BlendTime"), Next.BlendTime);
			WriteObject(Blend, TEXT("BlendProfile"), Next.BlendProfile);
			WriteNames(Blend, TEXT("Tags"), Next.Tags);
			WriteBool(View, TEXT("NoValidAnim"), false);
			// 주기적 무장 검색이 상태 그래프보다 먼저 전이 노티파이를 소비하면 안 된다.
			if (bSelectionChanged || bForceBlend)
			{
				WriteBool(View, TEXT("NotifyTransition_Re-Transition"), false);
				WriteBool(View, TEXT("NotifyTransition_ToLoop"), false);
			}

			WriteNumber(View, TEXT("Search Cost"), Output.SearchCost);
			WriteObject(View, TEXT("CurrentSelectedAnim"), Animation);
			WriteObject(View, TEXT("CurrentSelectedDatabase"), Output.SourceDatabase);
			TArray<FName> DatabaseTags;
			if (Output.SourceDatabase)
			{
				UPoseSearchLibrary::GetDatabaseTags(Output.SourceDatabase, DatabaseTags);
			}

			if (State->bUsesWeaponLocomotion)
			{
				DatabaseTags = UVBGASPLocomotionStateLibrary::ProjectCMCPlaybackTags(Output.Tags, DatabaseTags);
			}

			WriteNames(View, TEXT("CurrentDatabaseTags"), DatabaseTags);
			*Inputs = MoveTemp(Next);
			State->LastSelection = Output;
			State->SelectedState = MachineState;
			State->SelectedWeaponKey = State->WeaponSelectionKey;
			State->SelectedWeaponPool = State->CapturedWeaponPool;
			if (State->bUsesWeaponLocomotion)
			{
				State->SelectedGait = UVBGASPLocomotionStateLibrary::GetCMCSelectionGait(*State);
			}

			State->bSelectedWeaponLocomotion = State->bUsesWeaponLocomotion;
			State->bWaitingForPhase = false;
			State->bHasSelection = true;
			State->bSelectionContractFailed = false;
			++State->SelectionSerial;
			if (bForceBlend || Output.bForceBlend)
			{
				State->PendingForceSerial = State->SelectionSerial;
			}
			// 풀에 시작/정지가 없으면 전이가 반복 클립으로 대체되며, 그 클립은 완료 노티파이를 내지 못한다.
			if (State->bUsesWeaponLocomotion && Inputs->bLoop &&
				UVBGASPLocomotionStateLibrary::GetCMCPhase(CMCStateName(View)) == EVBGASPCMCPhase::Transition)
			{
				State->bTransitionToLoopRequested = true;
				WriteBool(View, TEXT("NotifyTransition_ToLoop"), true);
				// 피벗 클립 없는 풀에서 매 프레임 피벗 왕복을 막으려고, 원판정이 거짓이 될 때까지 래치로 진입을 막는다.
				const bool bWasLatched = State->bPivotFallbackLatched;
				State->bPivotFallbackLatched = UVBGASPLocomotionStateLibrary::StepPivotFallbackLatch(
					bWasLatched, true, UVBGASPLocomotionLibrary::IsPivotingRaw(Instance), false);
				if (State->bPivotFallbackLatched && !bWasLatched)
				{
					State->bPivotFallbackCrouched = View.Is(TEXT("Stance"), TEXT("Crouch"));
				}
			}

			if (State->bUsesWeaponLocomotion && bSelectionChanged)
			{
				UE_LOG(LogVBGASPState, Log, TEXT("CMC armed selection: state=%s phase=%d pool=%d asset=%s loop=%d source=%s candidates=%d"),
					*CMCStateName(View).ToString(), static_cast<int32>(Output.Phase), State->CapturedWeaponPool,
					*GetNameSafe(Animation), Inputs->bLoop, *GetNameSafe(Output.SourceDatabase), State->AllowedDatabases.Num());
			}

			return true;
		}
	}

	void SelectNativeCMC(UAnimInstance* Instance, uint8 MachineState, bool bForceBlend, UChooserTable* Chooser)
	{
		const FStateView View(Instance);
		FVBGASPCMCState* State = View.NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
		if (!ensureMsgf(State, TEXT("Native CMC chooser requires CMCState on %s"), *GetNameSafe(Instance))
			|| State->bSelectionContractFailed)
		{
			return;
		}

		State->bNeedsReselect = false;
		State->SecondsSinceSearch = 0.0f;
		if (!State->bHasSelection || State->SelectedState != MachineState || bForceBlend ||
			State->bSelectedWeaponLocomotion != State->bUsesWeaponLocomotion || State->SelectedWeaponPool != State->CapturedWeaponPool)
		{
			State->bTransitionToLoopRequested = false;
		}
		// 피벗 대체 래치는 풀, 경로별 사실이라 경로나 무기 풀이 바뀌면 지운다.
		if (UVBGASPLocomotionStateLibrary::HasPivotFallbackOwnerChanged(*State))
		{
			State->bPivotFallbackLatched = false;
		}
		// 상태는 입력 필터다. 결과가 받아들여지기 전에는 재생 입력을 바꾸지 않는다.
		if (!WriteNumber(View, TEXT("StateMachineState"), MachineState))
		{
			State->bSelectionContractFailed = true;
			UE_LOG(LogVBGASPState, Error, TEXT("Missing CMC StateMachineState on %s"), *GetNameSafe(Instance));
			return;
		}

		UAnimationAsset* Animation = nullptr;
		FVBGASPCMCChooserOutput Output;
		UAnimInstance* Context = State->SelectionContext ? State->SelectionContext.Get() : Instance;
		const bool bEvaluated = UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(Instance, Context, Chooser, Animation, Output);
		State->bHistoryWasReady = UVBGASPLocomotionStateLibrary::IsCMCPoseHistoryReady(Instance);
		const bool bSamePlaybackOwner = State->bHasSelection &&
			State->bSelectedWeaponLocomotion == State->bUsesWeaponLocomotion &&
			(!State->bUsesWeaponLocomotion || State->SelectedWeaponPool == State->CapturedWeaponPool);
		// 정지 전용 풀이 Idle Loop 진입 뒤 잠깐 남을 수 있다. CMC 를 끄지 않고 마지막 포즈를 유지한 채 풀 변경 때 다시 시도한다.
		if (bEvaluated && !Animation &&
			State->bUsesWeaponLocomotion && bSamePlaybackOwner)
		{
			if (!State->bWaitingForPhase)
			{
				UE_LOG(LogVBGASPState, Log,
					TEXT("CMC armed awaiting phase: state=%s phase=%d pool=%d candidates=%d phaseCandidates=%d held=%s"),
					*CMCStateName(View).ToString(), static_cast<int32>(Output.Phase), State->CapturedWeaponPool,
					State->AllowedDatabases.Num(), Output.bHasPhaseCandidates,
					*GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Instance).Anim));
			}

			State->bWaitingForPhase = true;
			WriteBool(View, TEXT("NoValidAnim"), true);
			return;
		}

		if (!bEvaluated || !ApplyCMCSelection(Instance, MachineState, bForceBlend, Animation, Output))
		{
			WriteBool(View, TEXT("NoValidAnim"), true);
			StoreValidAnimations(View, {});
			// 재시도 가능한 경우는 위에서 처리했다. 잘못된 문맥, 명령이나 다른 무기의 낡은 포즈는 막는 쪽으로 실패한다.
			if (State->bUsesWeaponLocomotion || !bEvaluated || !bSamePlaybackOwner)
			{
				State->bSelectionContractFailed = true;
				UE_LOG(LogVBGASPState, Error,
					TEXT("CMC selection contract failed (%s, chooser=%s, state=%s, pool=%d, evaluated=%d, history=%d, candidateChooser=%d, phaseCandidates=%d, phaseValid=%d, allowed=%d, asset=%s, source=%s, poseMatched=%d, fallback=%d, cost=%g, start=%g, blend=%g, profile=%s); restore MM until reinitialization"),
					*GetNameSafe(Instance), *GetNameSafe(Chooser), *CMCStateName(View).ToString(), State->CapturedWeaponPool,
					bEvaluated, Output.bHistoryReady, Output.bCandidateChooserEvaluated, Output.bHasPhaseCandidates,
					Output.bPhaseDataValid, Output.AllowedDatabases.Num(), *GetNameSafe(Animation), *GetNameSafe(Output.SourceDatabase),
					Output.bPoseMatched, Output.bFallback, Output.SearchCost, Output.StartTime, Output.BlendTime,
					*Output.BlendProfileName.ToString());
			}

			return;
		}

		StoreValidAnimations(View, { Animation });
		UVBGASPLocomotionStateLibrary::ConsumeCMCForceBlend(Instance);
	}
}

void UVBGASPLocomotionStateLibrary::UpdateCMCSelection(UAnimInstance* Instance)
{
	const VBGASP::FStateView View(Instance);
	FVBGASPCMCState* State = View.NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
	if (!State || !State->bSetupWasActive || State->bSelectionContractFailed || !State->ActiveChooser)
	{
		return;
	}
	// 피벗 대체 래치 풀기 - 원판정과 자세로 본다.
	RefreshPivotFallbackLatch(Instance, *State);
	const bool bHistoryReady = IsCMCPoseHistoryReady(Instance);
	bool bHistoryBecameReady = bHistoryReady && !State->bHistoryWasReady;
	State->bHistoryWasReady = bHistoryReady;
	bool bRestartFinishedAsset = false;
	if (State->bUsesWeaponLocomotion && State->bHasSelection)
	{
		const EVBGASPCMCPhase Phase = GetCMCPhase(VBGASP::CMCStateName(View));
		const EVBGASPCMCPhase SelectionPhase = GetCMCSelectionPhase(VBGASP::CMCStateName(View), View.Struct<FVector>(TEXT("Velocity")).Z);
		const FVBGASPCMCBlendInputs Inputs = GetCMCBlendInputs(Instance);
		FBlendStackAnimNodeReference Stack;
		const bool bCurrentSample = VBGASP::FindBlendStack(Instance, Stack) &&
			UBlendStackAnimNodeLibrary::GetCurrentAsset(Stack) == Inputs.Anim;
		const float Remaining = bCurrentSample ? UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(Stack) : MAX_flt;
		const bool bRelease = ShouldReleaseCMCTransition(Phase, Inputs.bLoop,
			View.Bool(TEXT("NotifyTransition_ToLoop")), Remaining, Inputs.BlendTime);
		if (Phase == EVBGASPCMCPhase::Transition && !Inputs.bLoop && !bRelease && !State->bWaitingForPhase)
		{
			bHistoryBecameReady = false;
		}

		if (bRelease)
		{
			VBGASP::WriteBool(View, TEXT("NotifyTransition_ToLoop"), true);
			if (!State->bTransitionToLoopRequested)
			{
				State->bNeedsReselect = true;
			}

			State->bTransitionToLoopRequested = true;
		}
		// 반복 상태에 잘못 남은 비반복 클립을 복구하되, 정상 시작/정지는 끊지 않는다.
		State->bNeedsReselect |= SelectionPhase == EVBGASPCMCPhase::Loop && !Inputs.bLoop && !State->bWaitingForPhase;
		State->bNeedsReselect |= State->bWaitingForPhase && State->SecondsSinceSearch >= 0.1f;
		// 시작, 멈춤 클립 재생 중에 걸음이 바뀌면 그 갱신에서 다시 고른다.
		State->bNeedsReselect |= !State->bWaitingForPhase &&
			ShouldReselectCMCForGait(Phase, Inputs.bLoop, State->SelectedGait, GetCMCSelectionGait(*State));
		bRestartFinishedAsset = bCurrentSample && !Inputs.bLoop && Remaining <= UE_SMALL_NUMBER;
	}

	if (State->bNeedsReselect || bHistoryBecameReady || !State->bHasSelection)
	{
		const uint64 PreviousSerial = State->SelectionSerial;
		VBGASP::SelectNativeCMC(Instance, static_cast<uint8>(View.Number(TEXT("StateMachineState"))),
			false, State->ActiveChooser);
		// 다 재생한 같은 자산을 다시 고르면 한 번 재시작한다. 받아들인 선택에서만 한다.
		if (bRestartFinishedAsset && State->SelectionSerial != PreviousSerial &&
			!State->bWaitingForPhase && !State->bSelectionContractFailed)
		{
			State->PendingForceSerial = State->SelectionSerial;
		}
	}
}

bool UVBGASPLocomotionStateLibrary::ConsumeCMCForceRequest(FVBGASPCMCState& State)
{
	if (State.PendingForceSerial == 0 || State.PendingForceSerial == State.ConsumedForceSerial)
	{
		return false;
	}

	State.ConsumedForceSerial = State.PendingForceSerial;
	return true;
}

void UVBGASPLocomotionStateLibrary::ConsumeCMCForceBlend(UAnimInstance* Instance)
{
	FVBGASPCMCState* State = VBGASP::FStateView(Instance).NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
	if (!State || State->bSelectionContractFailed || State->PendingForceSerial == State->ConsumedForceSerial)
	{
		return;
	}

	FBlendStackAnimNodeReference Node;
	if (VBGASP::FindBlendStack(Instance, Node) && ConsumeCMCForceRequest(*State))
	{
		UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Node);
	}
}

FVBGASPCMCBlendInputs UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(UAnimInstance* Instance)
{
	const FVBGASPCMCBlendInputs* Inputs = VBGASP::FStateView(Instance).NativePtr<FVBGASPCMCBlendInputs>(TEXT("CMCBlendInputs"));
	return Inputs ? *Inputs : FVBGASPCMCBlendInputs();
}
