// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AVBCharacter;
class UAnimInstance;
class UVBLocomotionIntentComponent;

// 슬라이드 그래프로 들어가고 나오는 시점을 정한다. UVBAnimInstance 가 값으로 들고 게임 스레드에서만 쓴다.
struct VB_API FVBAnimSlideHandoff
{
	// 한 프레임의 판정 입력.
	struct FInputs
	{
		float DeltaSeconds = 0.0f;
		const AVBCharacter* Character = nullptr;
		UAnimInstance* SlideGraph = nullptr;
		UAnimInstance* LocomotionGraph = nullptr;
		const UVBLocomotionIntentComponent* Intent = nullptr;
		bool bUseGASPLocomotion = false;
		bool bPoseOwnedByMontage = false;
		bool bTraversalOwningPose = false;
		bool bRagdollPowered = false;
	};

	// 이번 프레임의 슬라이드 포즈 사용 여부를 돌려준다.
	bool Update(const FInputs& In, bool bUseSlidePose);
	// 그래프가 슬라이드에 들어갔다는 기록을 지운다.
	void ClearEntered() { bEntered = false; }

private:
	// 그래프가 슬라이드 상태에 실제로 들어갔나.
	bool bEntered = false;
	// 물리 슬라이드가 끝난 뒤 슬라이드 포즈를 유지한 시간(초).
	float ExitElapsed = 0.f;
	// 슬라이드 그래프 없이 미끄러질 때 경고를 한 번만 낸다.
	bool bWarnedMissingSlideGraph = false;
	// 질주 인계용 본 그래프 이름표가 있는지 한 번만 확인했나.
	bool bSprintHandoffNamesChecked = false;
};
