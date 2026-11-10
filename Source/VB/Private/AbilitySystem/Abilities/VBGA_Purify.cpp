// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Purify.h"
#include "AbilitySystem/VBGameplayTags.h"

UVBGA_Purify::UVBGA_Purify()
{
	// InputTag는 BP에서도 설정 가능하지만 기본값 지정
	InputTag = VBGameplayTags::Input_Purify;
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Medical_Purify));
}
