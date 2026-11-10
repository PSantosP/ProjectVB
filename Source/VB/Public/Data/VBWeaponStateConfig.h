// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "VBWeaponStateConfig.generated.h"

class UGameplayEffect;

/**
 * VBWeaponStateComponent 의 전투 진입/이탈 타이밍 튜닝. 미할당이면 CDO 디폴트로 떨어진다.
 * 무기별 데이터는 UVBWeaponDefinition 에 있다.
 */
UCLASS(BlueprintType)
class VB_API UVBWeaponStateConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 전투 행위(공격/피격/락온) 후 이 시간 경과 시 전투 이탈 시도(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float CombatExitDelay = 4.0f;

	// 이 반경 내에 적이 있으면 전투 유지(자동 이탈 안 함). 1500 = 15m
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float EnemyProximityRadius = 1500.0f;

	// 무기 전환 몽타주를 재생할 최대 평면 속도(cm/s). 넘으면 몽타주 없이 소환 VFX 만 낸다(이동 중 재생하면 발이 미끄러진다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float ChangeActingMaxSpeed = 150.0f;

	// 이 평면 속도(cm/s)를 넘으면 활동 중으로 보고 전투 이탈을 미룬다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	float CombatExitIdleSpeedThreshold = 10.0f;

	// 손에 든 무기의 강화 효과(무한, SetByCaller Data.WeaponDamageScale 로 덮어쓰기). 값은 강화 컴포넌트가 정한다.
	// 비면 강화가 데미지에 붙지 않는다(무기 상태가 경고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TSubclassOf<UGameplayEffect> WeaponEnhancementEffect;
};
