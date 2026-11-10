// UVBGASPLocomotionStateLibrary 의 CMC 선택기 평가: 세 문맥 계약, 입력 질의, 결과 검증.

#include "Animation/VBGASPLocomotionStateLibrary.h"

#include "VBGASPNodeLookup.h"
#include "VBGASPStateFields.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"
#include "ChooserTypes.h"
#include "IObjectChooser.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "StructUtils/InstancedStruct.h"

using VBGASP::FStateView;

namespace
{
	// 세 문맥의 형이 native 계약과 맞는지 본다.
	bool HasCMCContextContract(UAnimInstance* ContextInstance, const TConstArrayView<FInstancedStruct>& Signature)
	{
		if (Signature.Num() != 3)
		{
			return false;
		}

		const FContextObjectTypeClass* ObjectType = Signature[0].GetPtr<FContextObjectTypeClass>();
		const FContextObjectTypeStruct* OutputType = Signature[1].GetPtr<FContextObjectTypeStruct>();
		const FContextObjectTypeStruct* HistoryType = Signature[2].GetPtr<FContextObjectTypeStruct>();
		return ObjectType && ObjectType->Class && ContextInstance->IsA(ObjectType->Class)
			&& OutputType && OutputType->Struct == FVBGASPCMCChooserOutput::StaticStruct()
			&& OutputType->Direction == EContextObjectDirection::ReadWrite
			&& HistoryType && HistoryType->Struct == FPoseHistoryReference::StaticStruct();
	}

	// CMC 상태를 선택기 출력의 입력값과 무기 질의로 옮긴다.
	void PrepareCMCQuery(UAnimInstance* LinkedInstance, const FVBGASPCMCState& State, FVBGASPCMCChooserOutput& Output,
		FVBGASPCMCWeaponQuery& WeaponQuery)
	{
		Output.AllowedDatabases = State.AllowedDatabases;
		Output.InterruptMode = State.CurrentInterruptMode;
		if (State.bUsesWeaponLocomotion)
		{
			Output.Phase = State.bTransitionToLoopRequested ? EVBGASPCMCPhase::Loop :
				UVBGASPLocomotionStateLibrary::GetCMCSelectionPhase(VBGASP::CMCStateName(FStateView(LinkedInstance)),
					FStateView(LinkedInstance).Struct<FVector>(TEXT("Velocity")).Z);
			Output.bRequireCandidateChooser = true;
			WeaponQuery = State.WeaponQuery;
			const FStateView LinkedView(LinkedInstance);
			WeaponQuery.MovementMode = LinkedView.Is(TEXT("MovementMode"),
				TEXT("InAir")) ? EVBMovementMode::InAir : EVBMovementMode::Grounded;
			WeaponQuery.Stance = LinkedView.Is(TEXT("Stance"), TEXT("Crouch")) ? EVBStance::Crouch : EVBStance::Stand;
			WeaponQuery.MMState = UVBGASPLocomotionLibrary::IsMoving(LinkedInstance) ? EVBMMState::Moving : EVBMMState::Idle;
			WeaponQuery.Speed2D = static_cast<float>(LinkedView.Number(TEXT("Speed2D")));
			WeaponQuery.IsStarting = UVBGASPLocomotionLibrary::IsStarting(LinkedInstance);
			WeaponQuery.IsPivoting = UVBGASPLocomotionLibrary::IsPivoting(LinkedInstance);
			WeaponQuery.ShouldSpinTransition = UVBGASPLocomotionLibrary::ShouldSpinTransition(LinkedInstance);
			WeaponQuery.ShouldTurnInPlace = UVBGASPLocomotionLibrary::ShouldTurnInPlace(LinkedInstance);
			UVBGASPLocomotionStateLibrary::ApplyCMCWeaponPhase(WeaponQuery, VBGASP::CMCStateName(LinkedView), Output.Phase);
		}
	}

	// Pose Match 가 읽는 런타임 문맥. 실제 재생 중인 표본을 전달해 주기적 무장 검색이 이어가기 비용을 유지한다.
	void ReadCMCPlayerSettings(UAnimInstance* LinkedInstance, FChooserPlayerSettings& PlayerSettings)
	{
		FBlendStackAnimNodeReference StackReference;
		if (VBGASP::FindBlendStack(LinkedInstance, StackReference))
		{
			const FAnimNode_BlendStack* Stack = StackReference.GetAnimNodePtr<FAnimNode_BlendStack>();
			if (Stack)
			{
				PlayerSettings.PlayingAsset = UBlendStackAnimNodeLibrary::GetCurrentAsset(StackReference);
				PlayerSettings.PlayingAssetAccumulatedTime = UBlendStackAnimNodeLibrary::GetCurrentAssetTime(StackReference);
				PlayerSettings.bIsPlayingAssetMirrored = Stack->GetMirror();
				PlayerSettings.PlayingAssetBlendParameters = Stack->GetBlendParameters();
			}
		}
	}

	// 선택기가 고른 후보 DB 를 상태에 되쓰고 고른 표본이 그 후보에서 왔는지 본다.
	void StoreCMCCandidates(FVBGASPCMCState& State, const UAnimationAsset* Animation, FVBGASPCMCChooserOutput& Output)
	{
		State.AllowedDatabases = Output.AllowedDatabases;
		uint32 Key = 0;
		for (const auto& Database : Output.AllowedDatabases)
		{
			Key = HashCombineFast(Key, GetTypeHash(Database.Get()));
		}

		State.WeaponSelectionKey = static_cast<int32>(Key);
		if (!Output.bCandidateChooserEvaluated)
		{
			Output.bPhaseDataValid = false;
		}

		if (Animation && (!Output.SourceDatabase || !Output.AllowedDatabases.Contains(Output.SourceDatabase)))
		{
			Output.bPhaseDataValid = false;
		}
	}
}

bool UVBGASPLocomotionStateLibrary::IsCMCPoseHistoryReady(UAnimInstance* Instance)
{
	const FPoseHistoryReference History = Get_PoseHistoryReference(Instance);
	// 유효한 수집기도 첫 갱신에는 표본이 없을 수 있다.
	return History.PoseHistory.IsValid() && History.PoseHistory->GetNumEntries() >= 2;
}

bool UVBGASPLocomotionStateLibrary::IsValidCMCSelection(const UAnimationAsset* Animation,
	const FVBGASPCMCChooserOutput& Output)
{
	if (!Animation || !FMath::IsFinite(Output.StartTime) || Output.StartTime < 0.0f
		|| !FMath::IsFinite(Output.BlendTime) || Output.BlendTime < 0.0f
		|| !FMath::IsFinite(Output.MaxAcceptedCost))
	{
		return false;
	}

	if (!Output.bHistoryReady && !Output.bFallback)
	{
		return false;
	}

	if (!Output.bPoseMatched)
	{
		return true;
	}

	return Output.bHistoryReady && !Output.bFallback
		&& FMath::IsFinite(Output.SearchCost) && Output.SearchCost < MAX_flt
		&& (Output.MaxAcceptedCost <= 0.0f || Output.SearchCost <= Output.MaxAcceptedCost);
}

bool UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(UAnimInstance* LinkedInstance,
	UAnimInstance* ContextInstance, UChooserTable* Chooser, UAnimationAsset*& Animation,
	FVBGASPCMCChooserOutput& Output)
{
	Animation = nullptr;
	Output = FVBGASPCMCChooserOutput();
	if (!LinkedInstance || !ContextInstance || !Chooser)
	{
		return false;
	}

	if (!HasCMCContextContract(ContextInstance, Chooser->GetContextData()))
	{
		return false;
	}

	FPoseHistoryReference History = Get_PoseHistoryReference(LinkedInstance);
	const bool bHistoryReady = History.PoseHistory.IsValid() && History.PoseHistory->GetNumEntries() >= 2;
	Output.bHistoryReady = bHistoryReady;
	FVBGASPCMCState* State = FStateView(LinkedInstance).NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
	FVBGASPCMCWeaponQuery WeaponQuery;
	if (State)
	{
		PrepareCMCQuery(LinkedInstance, *State, Output, WeaponQuery);
	}

	FChooserEvaluationContext Context(ContextInstance);
	Context.AddStructParam(Output);
	Context.AddStructParam(History);
	if (State && State->bUsesWeaponLocomotion)
	{
		Context.AddStructParam(WeaponQuery);
	}

	FChooserPlayerSettings PlayerSettings;
	ReadCMCPlayerSettings(LinkedInstance, PlayerSettings);
	Context.AddStructParam(PlayerSettings);
	const FInstancedStruct Selection = UChooserFunctionLibrary::MakeEvaluateChooser(Chooser);
	Animation = Cast<UAnimationAsset>(UChooserFunctionLibrary::EvaluateObjectChooserBase(
		Context, Selection, UAnimationAsset::StaticClass()));
	// 저작 데이터가 아닌 입력값이라 출력 열이 준비 상태를 위조할 수 없다.
	Output.bHistoryReady = bHistoryReady;
	if (State)
	{
		Output.InterruptMode = State->CurrentInterruptMode;
		if (State->bUsesWeaponLocomotion)
		{
			StoreCMCCandidates(*State, Animation, Output);
		}
	}

	return Output.bPhaseDataValid;
}
