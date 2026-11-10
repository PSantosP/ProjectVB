// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CoreLoop/VBServerWrite.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "VB/VB.h"

bool VBServerWrite::IsAllowed(const UActorComponent& Component, const TCHAR* Subject, const TCHAR* What)
{
	const AActor* Owner = Component.GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		VB_LOG(Warning, "%s %s 을 권한 없는 쪽에서 불렀다 - 무시한다(쓰기는 서버만)", Subject, What);
		return false;
	}

	return true;
}

bool VBServerWrite::HasOwnerAuthority(const UActorComponent& Component)
{
	const AActor* Owner = Component.GetOwner();
	return Owner && Owner->HasAuthority();
}
