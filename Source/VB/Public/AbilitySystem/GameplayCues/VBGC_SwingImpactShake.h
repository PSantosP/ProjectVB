// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_SwingImpactShake.generated.h"

/**
 * 방향성 임팩트 쉐이크 큐. 휘두른 방향(Parameters.Normal)으로 로컬 카메라를 한 번 밀었다 되돌린다.
 * 쉐이크 자산과 세기는 SourceObject(UVBSwingImpactProfile)로 온다. BP 노드는 PlaySpace 를 못 정해 C++ 로 둔다.
 */
UCLASS()
class VB_API UVBGC_SwingImpactShake : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UVBGC_SwingImpactShake();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

};
