// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_Telegraph.generated.h"

class UVBTelegraphConfig;

/**
 * 적 공격 예고 큐. OnActive 에 발광, 소리, 위협 등록, OnRemove 에 발광 정지, 등록 해제. 상태는 갖지 않는다(CDO).
 * 파라미터: RawMagnitude = 예고 길이(초), AggregatedSourceTags = 답 태그(Attack.Answer.*) 하나.
 * 생성자에서 GameplayCueTag 를 넣지 않는다(레지스트리 키가 None 이 된다). 태그는 BP 자식이 준다.
 */
UCLASS()
class VB_API UVBGC_Telegraph : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
	// 연출 데이터. 비면 CDO 폴백(무연출) + 경고.
	UPROPERTY(EditDefaultsOnly, Category="VB|Telegraph")
	TObjectPtr<UVBTelegraphConfig> TelegraphConfig;

private:
	const UVBTelegraphConfig* GetConfig() const;

	// 파라미터에서 답 태그(Attack.Answer 아래 첫 태그)를 꺼낸다. 없으면 빈 태그.
	static FGameplayTag ResolveAnswerTag(const FGameplayCueParameters& Parameters);
};
