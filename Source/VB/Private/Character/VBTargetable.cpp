// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBTargetable.h"

#include "GameFramework/Actor.h"

bool IVBTargetable::IsActorTargetable(const AActor* Actor)
{
	const IVBTargetable* Targetable = Cast<IVBTargetable>(Actor);
	return Targetable && Targetable->IsTargetable();
}
