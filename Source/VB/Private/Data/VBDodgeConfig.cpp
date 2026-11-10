// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBDodgeConfig.h"

#include "Animation/AnimMontage.h"

namespace
{
	// 세트 안에서 방향 슬롯 하나를 꺼낸다. 미할당이면 nullptr -> 상위 폴백으로 내려간다.
	UAnimMontage* PickDirection(const FVBDodgeMontageSet& Set, EVBDodgeDirection Direction)
	{
		switch (Direction)
		{
		case EVBDodgeDirection::Forward: return Set.Forward;
		case EVBDodgeDirection::Backward: return Set.Backward;
		case EVBDodgeDirection::Left: return Set.Left;
		case EVBDodgeDirection::Right: return Set.Right;
		case EVBDodgeDirection::ForwardLeft: return Set.ForwardLeft;
		case EVBDodgeDirection::ForwardRight: return Set.ForwardRight;
		case EVBDodgeDirection::BackwardLeft: return Set.BackwardLeft;
		case EVBDodgeDirection::BackwardRight: return Set.BackwardRight;
		default: return nullptr;
		}
	}
}

UAnimMontage* UVBDodgeConfig::ResolveDodgeMontage(const FVBDodgeMontageSet* WeaponSet, EVBDodgeDirection Direction) const
{
	// 무기 전용 -> 통일 닷지 -> nullptr(호출부가 LaunchCharacter 안전망). 폴백은 방향 슬롯 단위다.
	if (WeaponSet)
	{
		if (UAnimMontage* M = PickDirection(*WeaponSet, Direction))
		{
			return M;
		}
	}

	// 통일 세트 - 맨손과 전용 세트 없는 무기가 여기로 온다.
	return PickDirection(DefaultDodgeSet, Direction);
}

UAnimMontage* UVBDodgeConfig::PickFromSet(const FVBDodgeMontageSet& Set, const FVBDodgeHeading& Heading)
{
	UAnimMontage* Eight = PickDirection(Set, Heading.Eight);
	return Eight ? Eight : PickDirection(Set, Heading.Cardinal);
}

UAnimMontage* UVBDodgeConfig::ResolveDodgeMontage(const FVBDodgeMontageSet* WeaponSet, const FVBDodgeHeading& Heading) const
{
	UAnimMontage* FromWeapon = WeaponSet ? PickFromSet(*WeaponSet, Heading) : nullptr;
	return FromWeapon ? FromWeapon : PickFromSet(DefaultDodgeSet, Heading);
}
