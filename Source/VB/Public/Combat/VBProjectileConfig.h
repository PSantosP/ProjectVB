// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBProjectileConfig.generated.h"

class UGameplayEffect;
class UStaticMesh;
struct FVBHitSpecValues;

/**
 * 던지는 투사체 하나의 설정. 비행, 수명, 모양과 맞았을 때 싣는 타격 값의 홈이다.
 * 타격 값의 이름과 뜻은 근접 공격 설정(UVBAttackConfig)과 같다 - 받는 쪽이 둘을 가르지 않는다.
 */
UCLASS(BlueprintType)
class VB_API UVBProjectileConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVBProjectileConfig();

	// 이 설정과 던진 쪽 공격력으로 타격 값을 만든다. 미는 방향은 맞은 순간의 비행 방향을 부르는 쪽이 준다.
	FVBHitSpecValues MakeHitValues(float AttackPower, float PushYaw) const;

	// 날아가는 모습. 충돌은 구가 맡고 이 메시는 보이기만 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile")
	TObjectPtr<UStaticMesh> Mesh;

	// 발사 속력(cm/s).
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile", meta=(ClampMin="1", Units="cm/s"))
	float Speed = 1500.0f;

	// 월드 중력에 곱하는 배율. 0 이면 곧게 난다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile", meta=(ClampMin="0"))
	float GravityScale = 0.0f;

	// 충돌 구의 반지름(cm).
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile", meta=(ClampMin="1", Units="cm"))
	float CollisionRadius = 20.0f;

	// 아무것도 맞히지 못했을 때 사라지기까지의 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile", meta=(ClampMin="0.1", Units="s"))
	float LifetimeSeconds = 4.0f;

	// 움직이는 표적을 앞질러 겨누는 최대 거리(cm). 0 이면 표적 지점을 겨눈다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Projectile", meta=(ClampMin="0", Units="cm"))
	float MaxLeadDistance = 300.0f;

	// 맞은 대상에 거는 피해 효과.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// 던진 쪽 AttackPower 에 곱하는 피해 배율(근접 공격 설정의 같은 이름 값과 같은 층).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0"))
	float DamageMultiplier = 1.0f;

	// 받는 쪽 넉백의 수평 세기(cm/s).
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0"))
	float KnockbackStrength = 0.0f;

	// 기존 자산의 상방 밀치기를 검출하기 위한 값. 반드시 0이어야 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Deprecated", AdvancedDisplay)
	float KnockbackZ = 0.0f;

	// 경직 게이지 피해.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(ClampMin="0"))
	float PoiseDamage = 0.0f;

	// 선언하는 반응 등급. 받는 쪽 저항이 깎는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	EVBHitImpact HitImpact = EVBHitImpact::Light;

	// 무엇으로 답할 수 있는가. 예고 큐의 색과 받는 쪽 가드 판정이 같은 값을 읽는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat", meta=(Categories="Attack.Answer"))
	FGameplayTag AttackAnswer;

	// 닿은 자리에서 한 번 내는 큐(깨짐, 먼지). 대상에 맞든 벽에 맞든 같다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Effects", meta=(Categories="GameplayCue"))
	FGameplayTag ImpactCueTag;

	// 날아가는 동안의 궤적 큐. 각 기기에서 투사체 자신을 대상으로 한 번 실행한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Effects", meta=(Categories="GameplayCue"))
	FGameplayTag TrailCueTag;

#if WITH_EDITOR
	// 피해 효과가 비었거나 속력, 수명, 반지름이 0 이하면 저장을 거절한다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
