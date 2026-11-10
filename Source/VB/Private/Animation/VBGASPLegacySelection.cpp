#include "VBGASPLegacySelection.h"

#include "VBGASPNodeLookup.h"
#include "VBGASPStateFields.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"
#include "ChooserTypes.h"
#include "IObjectChooser.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "StructUtils/InstancedStruct.h"

namespace VBGASP
{
	namespace
	{
		bool EvaluateAnimations(UAnimInstance* Instance, UChooserTable* Chooser, TArray<UAnimationAsset*>& Results,
			FInstancedStruct& Output)
		{
			if (!Chooser)
			{
				return false;
			}

			const TConstArrayView<FInstancedStruct> Signature = Chooser->GetContextData();
			if (Signature.Num() != 2)
			{
				return false;
			}

			const FContextObjectTypeClass* ObjectContext = Signature[0].GetPtr<FContextObjectTypeClass>();
			const FContextObjectTypeStruct* StructContext = Signature[1].GetPtr<FContextObjectTypeStruct>();
			if (!ObjectContext || !ObjectContext->Class || !Instance->IsA(ObjectContext->Class) || !StructContext ||
				!StructContext->Struct || StructContext->Struct->GetFName() != TEXT("S_ChooserOutputs"))
			{
				return false;
			}

			// 저작된 Chooser 바인딩이 요구하는 원래 UDS 형을 유지한다.
			Output.InitializeAs(StructContext->Struct);
			FChooserEvaluationContext Context(Instance);
			Context.AddStructViewParam(FStructView(Output.GetScriptStruct(), Output.GetMutableMemory()));
			// EvaluateChooser2 의 네이티브 전개(AnimInstance 프록시 동기화 포함)와 맞춘다.
			const FInstancedStruct Selection = UChooserFunctionLibrary::MakeEvaluateChooser(Chooser);
			for (UObject* Result : UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(Context, Selection,
				UAnimationAsset::StaticClass()))
			{
				Results.Add(Cast<UAnimationAsset>(Result));
			}

			return true;
		}

		// 옛 블렌드 스택 입력의 기본 칸을 초기화한다. 칸이 하나라도 없으면 재생을 끄고 false.
		bool ResetLegacyBlendState(UAnimInstance* Instance, const FStateView& State, uint8 StateMachineState)
		{
			bool bContractValid = WriteNumber(State, TEXT("StateMachineState"), StateMachineState);
			bContractValid &= CopyField(State, TEXT("Previous_BlendStackInputs"), State, TEXT("BlendStackInputs"));
			bContractValid &= WriteBool(State, TEXT("NoValidAnim"), false);
			bContractValid &= WriteBool(State, TEXT("NotifyTransition_Re-Transition"), false);
			bContractValid &= WriteBool(State, TEXT("NotifyTransition_ToLoop"), false);
			if (!bContractValid)
			{
				WriteBool(State, TEXT("NoValidAnim"), true);
				UE_LOG(LogVBGASPState, Error, TEXT("Invalid native GASP blend-stack property contract on %s"), *GetNameSafe(Instance));
				return false;
			}

			return true;
		}

		// 선택기를 평가해 후보를 기록한다. 평가가 틀렸거나 후보가 없으면 재생을 끄고 false.
		bool EvaluateLegacyCandidates(UAnimInstance* Instance, const FStateView& State, UChooserTable* Chooser,
			TArray<UAnimationAsset*>& Candidates, FInstancedStruct& Outputs)
		{
			if (!EvaluateAnimations(Instance, Chooser, Candidates, Outputs))
			{
				StoreValidAnimations(State, Candidates);
				WriteBool(State, TEXT("NoValidAnim"), true);
				UE_LOG(LogVBGASPState, Error, TEXT("Invalid GASP state Chooser context: %s / %s"), *GetNameSafe(Instance),
					*GetNameSafe(Chooser));
				return false;
			}

			if (!StoreValidAnimations(State, Candidates) || Candidates.IsEmpty())
			{
				WriteBool(State, TEXT("NoValidAnim"), true);
				return false;
			}

			return true;
		}

		// 첫 후보와 선택기 출력을 옛 블렌드 스택 입력에 쓴다. 칸이 하나라도 없으면 재생을 끄고 false.
		bool WriteLegacyBlendInputs(UAnimInstance* Instance, const FStateView& State, const FStateView& Blend, const FStateView& Output,
			UAnimationAsset* First, bool bLoop)
		{
			bool bContractValid = WriteObject(Blend, TEXT("Anim"), First);
			bContractValid &= WriteBool(Blend, TEXT("Loop"), bLoop);
			bContractValid &= WriteNumber(Blend, TEXT("StartTime"), Output.Number(TEXT("StartTime")));
			bContractValid &= WriteNumber(Blend, TEXT("BlendTime"), Output.Number(TEXT("BlendTime")));
			bContractValid &= WriteObject(Blend, TEXT("BlendProfile"),
				const_cast<UBlendProfile*>(Instance->GetBlendProfileByName(ReadName(Output, TEXT("BlendProfile")))));
			bContractValid &= CopyField(Blend, TEXT("Tags"), Output, TEXT("Tags"));
			if (!bContractValid)
			{
				WriteBool(State, TEXT("NoValidAnim"), true);
				UE_LOG(LogVBGASPState, Error, TEXT("Invalid GASP BlendStackInputs fields on %s"), *GetNameSafe(Instance));
				return false;
			}

			return true;
		}

		// 후보 중에서 Motion Matching 으로 고쳐 고른다. 고른 표본이 없거나 비용 상한을 넘으면 재생을 끄고 false.
		bool ApplyLegacyMotionMatch(UAnimInstance* Instance, const FStateView& State, const FStateView& Blend, const FStateView& Output,
			const TArray<UAnimationAsset*>& Candidates, bool bLoop)
		{
			TArray<UObject*> SearchAssets;
			SearchAssets.Reserve(Candidates.Num());
			for (UAnimationAsset* Candidate : Candidates)
			{
				SearchAssets.Add(Candidate);
			}

			FPoseSearchBlueprintResult Result;
			UPoseSearchLibrary::MotionMatch(Instance, SearchAssets, TEXT("PoseHistory"), {}, {}, Result);
			WriteNumber(State, TEXT("Search Cost"), Result.SearchCost);
			UAnimationAsset* Selected = Cast<UAnimationAsset>(Result.SelectedAnim);
			const double CostLimit = Output.Number(TEXT("MMCostLimit"));
			const bool bAcceptCost = CostLimit > 0.0 ? Result.SearchCost <= CostLimit : true;
			if (!Selected || !bAcceptCost)
			{
				WriteBool(State, TEXT("NoValidAnim"), true);
				return false;
			}

			UPoseSearchLibrary::IsAnimationAssetLooping(Selected, bLoop);
			WriteObject(Blend, TEXT("Anim"), Selected);
			WriteBool(Blend, TEXT("Loop"), bLoop);
			WriteNumber(Blend, TEXT("StartTime"), Result.SelectedTime);
			return true;
		}
	}

	void SelectLegacyBlendInputs(UAnimInstance* Instance, uint8 StateMachineState, bool bForceBlend, UChooserTable* Chooser)
	{
		const FStateView State(Instance);
		const FStateView Blend = State.Child(TEXT("BlendStackInputs"));
		if (!ResetLegacyBlendState(Instance, State, StateMachineState))
		{
			return;
		}

		TArray<UAnimationAsset*> Candidates;
		FInstancedStruct Outputs;
		if (!EvaluateLegacyCandidates(Instance, State, Chooser, Candidates, Outputs))
		{
			return;
		}

		const FStateView Output(Outputs.GetScriptStruct(), Outputs.GetMutableMemory());
		UAnimationAsset* First = Candidates[0];
		bool bLoop = false;
		UPoseSearchLibrary::IsAnimationAssetLooping(First, bLoop);
		if (!WriteLegacyBlendInputs(Instance, State, Blend, Output, First, bLoop))
		{
			return;
		}

		if (Output.Bool(TEXT("UseMM")) && !ApplyLegacyMotionMatch(Instance, State, Blend, Output, Candidates, bLoop))
		{
			return;
		}

		if (bForceBlend)
		{
			FBlendStackAnimNodeReference Node;
			if (FindBlendStack(Instance, Node))
			{
				UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Node);
			}
		}
	}
}
