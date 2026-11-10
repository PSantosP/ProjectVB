// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBEnemyAnimSet.generated.h"

class UAnimSequenceBase;
class UBlendSpace;
class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * 적 아키타입 하나의 로코모션 클립 묶음. ABP_Enemy_Base 하나가 모든 적을 돌리고 클립은 이 자산이 정한다.
 * 골격이 다른 몬스터는 ABP 를 복제한다(ABP_Boss_Troll) - 그래프를 고치면 양쪽을 함께 고친다.
 */
UCLASS(BlueprintType)
class VB_API UVBEnemyAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 서 있을 때. 골반 높이가 Locomotion 과 맞는 클립을 고른다 - 다르면 전환 때 메시가 튄다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UAnimSequenceBase> Idle;

	// 이동. 2D 블렌드스페이스(X 방향 -180..180 / Y 속도 cm/s). 적은 대상을 보며 움직여 8방향 세트가 필요하다.
	// 샘플 클립은 EnableRootMotion=true(제자리 잠금)여야 메시가 캡슐에서 미끄러지지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation")
	TObjectPtr<UBlendSpace> Locomotion;

	// Idle 과 Locomotion 을 가르는 속도 문턱(cm/s). 0 근처의 잔떨림이 이동으로 읽히지 않게 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Animation", meta=(ClampMin="0.0"))
	float MovingSpeedThreshold = 10.0f;

	// Idle <-> Locomotion 블렌드 시간은 ABP_Enemy_Base 의 BlendListByBool 핀 리터럴이 홈이다(핀 바인딩이 값을 못 넘겼다).

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
