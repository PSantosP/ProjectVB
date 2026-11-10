#include "Animation/VBGASPLocomotionStateLibrary.h"

#include "VBGASPCMCSelection.h"
#include "VBGASPLegacySelection.h"
#include "VBGASPNodeLookup.h"
#include "VBGASPStateFields.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "Chooser.h"
#include "PoseSearch/PoseSearchHistoryCollectorAnimNodeLibrary.h"

using VBGASP::FStateView;
using VBGASP::FindBlendStack;
using VBGASP::FindTaggedNode;

namespace
{
	void EnterState(UAnimInstance* Instance, UChooserTable* Chooser, const TCHAR* StateName, bool bForceBlend,
		bool bCaptureTargetRotation = false)
	{
		const FStateView State(Instance);
		if (bCaptureTargetRotation)
		{
			VBGASP::CopyField(State, TEXT("TargetRotationOnTransitionStart"), State, TEXT("TargetRotation"));
		}

		const FByteProperty* Property = CastField<FByteProperty>(State.Find(TEXT("StateMachineState")));
		if (Property && Property->Enum)
		{
			for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
			{
				if (Property->Enum->GetAuthoredNameStringByIndex(Index) == StateName)
				{
					UVBGASPLocomotionStateLibrary::SetBlendStackAnimFromChooser(Instance,
						static_cast<uint8>(Property->Enum->GetValueByIndex(Index)), bForceBlend, Chooser);
					return;
				}
			}
		}

		VBGASP::WriteBool(State, TEXT("NoValidAnim"), true);
		UE_LOG(LogVBGASPState, Error, TEXT("Missing GASP StateMachineState value '%s' on %s"), StateName, *GetNameSafe(Instance));
	}
}

void UVBGASPLocomotionStateLibrary::SetBlendStackAnimFromChooser(UAnimInstance* Instance,
	uint8 StateMachineState, bool ForceBlend, UChooserTable* Chooser)
{
	if (!Instance)
	{
		return;
	}

	const FStateView State(Instance);
	if (const FVBGASPCMCState* Native = State.NativePtr<FVBGASPCMCState>(TEXT("CMCState"));
		Native && Native->ActiveChooser)
	{
		VBGASP::SelectNativeCMC(Instance, StateMachineState, ForceBlend, Native->ActiveChooser);
		return;
	}

	if (Chooser && Chooser->GetContextData().Num() == 3)
	{
		VBGASP::SelectNativeCMC(Instance, StateMachineState, ForceBlend, Chooser);
		return;
	}

	VBGASP::SelectLegacyBlendInputs(Instance, StateMachineState, ForceBlend, Chooser);
}

void UVBGASPLocomotionStateLibrary::OnStateEntry_IdleLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Idle Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToIdle(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Idle Loop"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_LocomotionLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Locomotion Loop"), false, true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToLocomotion(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Locomotion Loop"), true, true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_InAirLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("In Air Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToInAir(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to In Air Loop"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_IdleBreak(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Idle Break"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_SlideLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Slide Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToSlide(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Slide"), true);
}

void UVBGASPLocomotionStateLibrary::OnUpdate_TransitionToLocomotion(UAnimInstance* Instance)
{
	if (!Instance)
	{
		return;
	}

	const FStateView State(Instance);
	const FRotator Rotation = FMath::RInterpTo(State.Struct<FRotator>(TEXT("TargetRotationOnTransitionStart")),
		State.Struct<FRotator>(TEXT("TargetRotation")), Instance->GetDeltaSeconds(), 5.f);
	const FStructProperty* Property = CastField<FStructProperty>(State.Find(TEXT("TargetRotationOnTransitionStart")));
	if (Property && Property->Struct == TBaseStructure<FRotator>::Get())
	{
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(State.Data), &Rotation);
	}
}

bool UVBGASPLocomotionStateLibrary::IsAnimationAlmostComplete(UAnimInstance* Instance)
{
	FBlendStackAnimNodeReference Node;
	return FindBlendStack(Instance, Node) && !UBlendStackAnimNodeLibrary::IsCurrentAssetLooping(Node)
		&& UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(Node) <= 0.75f;
}

bool UVBGASPLocomotionStateLibrary::PlayLand(UAnimInstance* Instance)
{
	const FStateView State(Instance);
	return State.Is(TEXT("MovementMode"), TEXT("OnGround")) && State.Is(TEXT("MovementMode_LastFrame"), TEXT("InAir"));
}

bool UVBGASPLocomotionStateLibrary::PlayMovingLand(UAnimInstance* Instance)
{
	return PlayLand(Instance) && FMath::Abs(UVBGASPLocomotionLibrary::Get_TrajectoryTurnAngle(Instance)) <= 120.0;
}

FPoseHistoryReference UVBGASPLocomotionStateLibrary::Get_PoseHistoryReference(UAnimInstance* Instance)
{
	EAnimNodeReferenceConversionResult Result;
	const FPoseSearchHistoryCollectorAnimNodeReference Node = UPoseSearchHistoryCollectorAnimNodeLibrary::ConvertToPoseHistoryNode(
		FindTaggedNode(Instance, TEXT("PoseHistory")), Result);
	return Result == EAnimNodeReferenceConversionResult::Succeeded ?
		UPoseSearchHistoryCollectorAnimNodeLibrary::GetPoseHistoryReference(Node) : FPoseHistoryReference();
}
