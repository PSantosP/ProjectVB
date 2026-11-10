// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBCharacterSurface.h"
#include "Components/PrimitiveComponent.h"

void VBCharacterSurface::ApplyBodyRules(UPrimitiveComponent& Body)
{
	// 바닥 피 자국이 발등 위에도 그려지지 않게 한다.
	Body.SetReceivesDecals(false);
}

void VBCharacterSurface::ApplyVisualRules(UPrimitiveComponent& Visual)
{
	ApplyBodyRules(Visual);
	Visual.SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
