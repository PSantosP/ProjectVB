// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimSlideHandoff.h"

#include "VBGASPStateView.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBGASPSlideLibrary.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Data/VBMovementConfig.h"
#include "VB/VB.h"

namespace
{
	// 본 그래프의 열거 속성에 그 이름표(authored name)가 있는지 한 번 확인한다(질주 인계 경고용).
	//   이름이 파일마다 다른 이유: unity 묶음에서 다른 파일의 static 과 부딪히지 않게.
	bool SlideHandoffHasEnumName(const VBGASP::FStateView& View, FName PropertyName, const TCHAR* AuthoredName)
	{
		const FByteProperty* Property = CastField<FByteProperty>(View.Find(PropertyName));
		if (!Property || !Property->Enum)
		{
			return false;
		}

		for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
		{
			if (Property->Enum->GetAuthoredNameStringByIndex(Index) == AuthoredName)
			{
				return true;
			}
		}

		return false;
	}
}

bool FVBAnimSlideHandoff::Update(const FInputs& In, bool bUseSlidePose)
{
	if (bUseSlidePose && In.SlideGraph)
	{
		bEntered |= UVBGASPSlideLibrary::HasEnteredSlide(In.SlideGraph);
	}
	// 슬라이드 중인데 GASPSlide 링크 그래프가 없으면 한 번 경고한다.
	if (!In.SlideGraph && In.bUseGASPLocomotion && In.Character && In.Character->GetIsSliding() && !bWarnedMissingSlideGraph)
	{
		bWarnedMissingSlideGraph = true;
		VB_LOG(Warning, "%s: 슬라이드 중인데 GASPSlide 링크 그래프가 없다 - 슬라이드 포즈 없이 미끄러진다", *GetNameSafe(In.Character));
	}

	if (In.SlideGraph && In.Character && In.Character->GetIsSliding())
	{
		bEntered |= UVBGASPSlideLibrary::HasEnteredSlide(In.SlideGraph);
		ExitElapsed = 0.f;
		return true;
	}

	ExitElapsed += bUseSlidePose ? In.DeltaSeconds : 0.f;
	// 질주 키를 누른 채 슬라이드가 끝나면 "달려 나오기"를 짧게 보인 뒤 본 그래프의 질주로 전환한다.
	const UVBMovementConfig* MoveConfig = In.Character ? In.Character->GetMovementConfig() : nullptr;
	// 본 그래프가 이미 "서서 Locomotion Loop" 일 때만 전환한다(그래야 출발 클립이 안 보인다). 아니면 아래 나오기 끝 경로로 간다.
	bool bSprintHandoff = false;
	if (bEntered && bUseSlidePose && In.Character && In.Intent
		&& In.Intent->GetIsSprinting() && MoveConfig
		&& ExitElapsed >= MoveConfig->SlideSprintExitHandoff)
	{
		const VBGASP::FStateView MainView(In.LocomotionGraph);
		bSprintHandoff = MainView.Is(TEXT("Stance"), TEXT("Stand")) && MainView.Is(TEXT("StateMachineState"), TEXT("Locomotion Loop"));
		// 이름표가 없으면 지름길이 영영 닫히므로 처음 한 번 확인해 경고한다.
		if (!bSprintHandoffNamesChecked)
		{
			bSprintHandoffNamesChecked = true;
			VB_CLOG(!SlideHandoffHasEnumName(MainView, TEXT("Stance"), TEXT("Stand"))
				|| !SlideHandoffHasEnumName(MainView, TEXT("StateMachineState"), TEXT("Locomotion Loop")), Warning,
				"%s: 슬라이드 질주 인계가 본 그래프의 Stance=Stand / StateMachineState=Locomotion Loop 를 못 찾는다(그래프 %s) - 질주 지름길 없이 슬라이드 나오기 끝까지 기다린다",
				*GetNameSafe(In.Character), *GetNameSafe(In.LocomotionGraph));
		}
	}
	// 그래프가 슬라이드에 들어가기 전에 끝났으면 기다릴 "나오기"가 없으니 바로 푼다(안 그러면 포즈가 남는다).
	const bool bNeverEntered = !bEntered;
	if (!In.SlideGraph || In.bPoseOwnedByMontage || In.bTraversalOwningPose || In.bRagdollPowered || bNeverEntered
		|| (bEntered && UVBGASPSlideLibrary::HasCompletedSlideExit(In.SlideGraph)) || bSprintHandoff)
	{
		bEntered = false;
		return false;
	}

	return bUseSlidePose;
}
