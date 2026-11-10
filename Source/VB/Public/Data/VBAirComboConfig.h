// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Data/VBTargetLockConfig.h"
#include "VBAirComboConfig.generated.h"

class UAnimMontage;
class UCurveFloat;
class UVBAttackConfig;
class UVBAirAfterimageConfig;

/** 무기별 공중 추격, 연타, 내려찍기, 카메라 설정.
 * 공격자는 동작 흐름을, 적의 넘어짐 능력은 요청에 담긴 낙하 기한과 충격 큐를 읽는다.
 */
UCLASS(BlueprintType)
class VB_API UVBAirComboConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 추격 이동과 동시에 시작하는 이륙 자세.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase")
	TObjectPtr<UAnimMontage> ChaseTakeoffMontage;

	// 공중에서 적 앞까지 가는 동안 트는 클립. 위치는 클립이 아니라 쫓기 이동이 정한다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase")
	TObjectPtr<UAnimMontage> ChaseMontage;

	// 띄운 뒤 이 시간(초) 안에 누른 공격만 쫓기가 된다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase", meta=(ClampMin="0.1"))
	float OpeningSeconds = 1.5f;

	// 이보다 먼 적은 쫓지 않는다(cm, 3D 거리).
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase", meta=(ClampMin="0.0"))
	float MaxChaseDistance = 450.0f;

	// 도착했을 때 적과의 수평 거리(cm). 첫 공중 타의 손끝 거리에 맞춘다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase", meta=(ClampMin="0.0"))
	float ChaseStopDistance = 85.0f;

	// 도착 높이 - 적 중심에서 더하는 값(cm).
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase")
	float ChaseHeightOffset = 0.0f;

	// 입력 직후부터 목표까지 이동하는 시간(초).
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase", meta=(ClampMin="0.05"))
	float ChaseSeconds = 0.35f;

	// 추격 목표 위치의 도착 허용 오차(cm).
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Chase", meta=(ClampMin="0.1"))
	float ChaseArrivalTolerance = 20.0f;

	// 공중 연타의 잎(섹션 = 연타 한 타).
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|String")
	TObjectPtr<UVBAttackConfig> StringAttack;

	// 연타 중 현재 목표를 유지하는 이동 태스크의 상수 1 시간 곡선.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|String")
	TObjectPtr<UCurveFloat> StringFollowTimeMapping;

	/** 공중 연타 중 락온 대상이 보이는 뒤쪽 구도 */
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Camera")
	FVBTargetLockFraming StringCameraFraming;

	// 추격 시작부터 도착까지 사용하는 로컬 연출. 비우면 연출을 사용하지 않는다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Staging")
	TObjectPtr<UVBAirAfterimageConfig> Staging;

	// 쫓기부터 공중 연타 끝까지 공격자가 떠 있을 수 있는 최대 시간(초). 넘으면 떨어진다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|String", meta=(ClampMin="0.1"))
	float AttackerMaxAirSeconds = 3.0f;

	// 내려찍기 - 공중 연타 몽타주 안의 섹션 셋(시작, 낙하 반복, 착지). 비면 이 무기는 내려찍기가 없다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam")
	FName SlamStartSection;
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam")
	FName SlamLoopSection;
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam")
	FName SlamEndSection;

	// 공중 연타 중 공격을 이만큼(초) 누르고 있으면 내려찍는다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam", meta=(ClampMin="0.05"))
	float HoldToSlamSeconds = 0.3f;

	/** 마지막 연타 적중 시 적을 먼저 내려찍기 */
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam")
	bool bSlamOnFinalHit = false;

	/** 내려찍기 시작부터 착지까지 허용하는 시간 */
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam", meta=(ClampMin="0.1"))
	float SlamMaxSeconds = 2.0f;

	/** 적이 실제 바닥에 닿을 때 재생할 충격 큐 */
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam", meta=(Categories="GameplayCue"))
	FGameplayTag SlamImpactCue;

	// 내려찍기 낙하 속도(cm/s, 아래로). 클립에 이동이 없어 낙하는 이 값이 만든다.
	UPROPERTY(EditAnywhere, Category="VB|AirCombo|Slam", meta=(ClampMin="0.0"))
	float SlamFallSpeed = 1500.0f;

	bool HasSlam() const { return !SlamStartSection.IsNone(); }
	bool IsSlamMotionValid() const;
	bool HasReachedChaseTarget(const FVector& Location, const FVector& TargetLocation) const;
	bool HasFiniteSlamRecovery() const;

	// 저작 문제 - 하나에 한 줄. IsDataValid 와 시험이 같은 판정을 쓴다.
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
