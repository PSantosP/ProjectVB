// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h" // TSubclassOf - CoreMinimal 은 Core 모듈이라 이 템플릿을 끌어오지 않는다
#include "VBSwingImpactProfile.generated.h"

class UCameraShakeBase;
class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다(VBParryConfig.h:11 동형)

/**
 * 스윙 한 번의 임팩트 쉐이크(방향 + 쉐이크 + 세기). 큐 파라미터는 UObject 만 실을 수 있어 자산이다.
 * 우선순위: Attack Trace 노티의 SwingImpactProfile -> UVBAttackConfig::DefaultSwingImpactProfile. 둘 다 비면 쉐이크 없음.
 */
UCLASS(BlueprintType)
class VB_API UVBSwingImpactProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 아바타 로컬 기준 스윙 축. 부호가 곧 밀림 방향이다. 길이는 런타임에 정규화된다.
	// 로컬인 이유: 스윙 중 캡슐이 돌아도 저작 의도가 캐릭터를 따라간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FVector LocalSwingAxis = FVector::ForwardVector;

	// 재생할 카메라 쉐이크 자산(Root Shake Pattern = Wave Oscillator 전제). 무기별 성격은 자산 교체로 낸다.
	// 미할당이면 GCN 이 조용히 스킵한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	// StartCameraShake 세기 배율. 쉐이크 자산을 여러 스윙이 공유해도 세기만 따로 바꾸려고 분리한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.0"))
	float ShakeScale = 1.0f;

	// 정규화된 스윙 축. 영벡터 축이면 영벡터를 그대로 돌려준다 - 판별은 호출부 몫이다.
	FVector GetNormalizedAxis() const;

#if WITH_EDITOR
	// 영벡터 축을 저장/쿡 시점에 잡는다. 런타임에서는 쉐이크가 조용히 안 날 뿐이다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
