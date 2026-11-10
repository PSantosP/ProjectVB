// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/VBTargetable.h"
#include "GameFramework/Actor.h"
#include "VBPlayerBodyTestTypes.generated.h"

// UVBTargetLockComponent 가 lock 대상으로 받는 가장 작은 actor(IVBTargetable + IsTargetable).
//   VBPlayerBodyTestFixture.h 가 하나 놓고, 시험은 제품 경로(parry lock, riposte window)로 lock 한다.
UCLASS(NotBlueprintable, NotPlaceable)
class AVBTestLockTarget : public AActor, public IVBTargetable
{
	GENERATED_BODY()

public:
	virtual bool IsTargetable() const override { return bTargetable; }
	virtual FVector GetTargetMarkerLocation() const override { return GetActorLocation(); }
	// 시험이 꺼서 아직 있지만 더는 자격이 없는 대상(예: 죽은 적)을 흉내 낸다.
	bool bTargetable = true;
};
