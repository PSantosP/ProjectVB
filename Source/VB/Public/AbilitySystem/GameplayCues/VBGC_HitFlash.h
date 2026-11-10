// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_HitFlash.generated.h"

/**
 * 히트 플래시 큐. 피격자의 UVBHitFlashComponent 에 위임해 메시를 순간 발광시킨다(상태 없음).
 * 생성자에서 GameplayCueTag 를 넣지 않는다 - 네이티브 부모가 태그를 들면 레지스트리 키가 None 이 되어 등록에서 빠진다.
 */
UCLASS()
class VB_API UVBGC_HitFlash : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

};
