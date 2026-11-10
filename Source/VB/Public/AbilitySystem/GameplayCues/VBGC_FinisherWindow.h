// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "Data/VBHitFlashConfig.h"   // FVBHitFlashProfile - 발광 프로필을 그대로 저작한다
#include "VBGC_FinisherWindow.generated.h"

/**
 * 처형 창이 열린 동안 대상 메시를 빛나게 하는 큐. 발광 상태는 대상의 UVBHitFlashComponent 가 가진다.
 * 생성자에서 GameplayCueTag 를 넣지 않는다(네이티브 부모가 태그를 들면 레지스트리 키가 None 이 된다). 태그는 BP 자식이 준다.
 */
UCLASS()
class VB_API UVBGC_FinisherWindow : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
	// 발광 프로필. 끄는 것은 Duration 이 아니라 OnRemove 다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FVBHitFlashProfile GlowProfile;

	// 외곽선용으로 대상 메시에 찍는 스텐실 번호. 0 이면 안 찍는다. r.CustomDepth=3 이 필요하다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher",
		meta=(ClampMin="0", ClampMax="255"))
	int32 OutlineStencilValue = 1;

private:
	// 대상의 모든 스켈레탈 메시에 커스텀 뎁스, 스텐실을 켜거나 끈다.
	void SetOutline(AActor* Target, bool bEnable) const;
};
