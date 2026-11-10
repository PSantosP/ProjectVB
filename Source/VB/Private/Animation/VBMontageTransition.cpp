#include "Animation/VBMontageTransition.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

bool VBMontageTransition::RequestSectionBlend(UAnimInstance* Anim, const UAnimMontage* Montage, float Seconds)
{
	if (!IsValidSectionBlend(Seconds))
	{
		return false;
	}
	if (Seconds == 0.0f)
	{
		return true;
	}
	if (!Anim || !Montage)
	{
		return false;
	}
	Anim->RequestSlotGroupInertialization(Montage->GetGroupName(), Seconds);
	return true;
}
