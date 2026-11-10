#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
class UAnimMontage;

namespace VBMontageTransition
{
	inline bool IsValidSectionBlend(float Seconds)
	{
		return FMath::IsFinite(Seconds) && Seconds >= 0.0f && Seconds <= 0.3f;
	}

	// 섹션 점프의 포즈 이음새를 몽타주가 속한 Slot Group에 요청한다.
	VB_API bool RequestSectionBlend(UAnimInstance* Anim, const UAnimMontage* Montage, float Seconds);
}
