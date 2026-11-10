// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h" // EVBWeaponType (WeaponProfileOverrides 키)
#include "Types/VBSlideMovementSettings.h"
#include "Engine/DataAsset.h"
#include "VBMovementConfig.generated.h"

class UCurveFloat;

/**
 * 한 (무기/상태) 조합의 속도 프로파일. X=Forward Y=Strafe Z=Backward.
 * UVBSlideMovementComponent::UpdateWalkSpeeds 가 이동 방향 구역으로 X/Y/Z 를 보간해 MaxWalkSpeed 를 정한다.
 * 인라인 디폴트 = 무장 값. 비무장 등은 config 생성자에서 초기화한다.
 */
USTRUCT(BlueprintType)
struct FVBMovementProfile
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(ToolTip="X=Forward Y=Strafe Z=Backward"))
	FVector WalkSpeeds = FVector(165.f, 165.f, 165.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(ToolTip="기본 순항 jog(8-way strafe)"))
	FVector RunSpeeds = FVector(250.f, 235.f, 225.f);

	// 45/135도 대각선 jog 클립의 루트 이동 속도. 축 방향 속도 사이를 단순 보간하면 대각선이 과속한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(ClampMin="0"))
	float RunDiagonalSpeed = 225.f;

	// Sprint 1단계(홀드 0~SprintRampDuration). bSprintRampEnabled=false 인 프로파일은 sprint 시 항상 이 값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	FVector SprintRunSpeeds = FVector(550.f, 550.f, 550.f);

	// Sprint 2단계(홀드 ≥ SprintRampDuration). bSprintRampEnabled=true 일 때만 도달.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	FVector SprintSpeeds = FVector(640.f, 640.f, 640.f);

	// Sprint 해제 후 고속 풀을 유지할 하한. Dense Chooser의 해당 무기 Sprint 행 Min과 일치해야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(ClampMin="0"))
	float SprintPoolJoinSpeed = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	float CrouchSpeed = 165.f;

	// true=2단 램프(1단->2단 승급), false=SprintRunSpeeds 에서 멈춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	bool bSprintRampEnabled = true;
};

/**
 * 플레이어 이동 튜닝 - CMC 물리, 회전율, 슬라이드, 무기별 속도 프로파일.
 * AVBCharacter 가 참조하고, 미할당이면 CDO 디폴트로 떨어진다(GetMovementConfig).
 */
UCLASS(BlueprintType)
class VB_API UVBMovementConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVBMovementConfig();

	// CMC 물리(무기 무관, BeginPlay 1회 적용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float BrakingDecelerationWalking = 750.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float MaxAcceleration = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float GroundFriction = 5.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float BrakingFrictionFactor = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float JumpZVelocity = 500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	float AirControl = 0.35f;

	// 회전율(매 tick 소비 - UpdateRotationRateForMovementMode)
	// Yaw=-1 = infinite(즉시 snap, GASP 관용).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	FRotator DefaultRotationRate = FRotator(0.f, -1.f, 0.f);

	// 공중(Falling) 회전. 즉시 snap 이 공중에서 부자연스러워 분리한다(GASP Falling 분기).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	FRotator AirborneRotationRate = FRotator(0.f, 200.f, 0.f);

	// 무장 지상 회전. 이 분기는 OffsetRootBone 흡수가 없어 캡슐 회전이 메시에 바로 보이므로 유한 속도로 돈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Movement")
	FRotator ArmedGroundedRotationRate = FRotator(0.f, 360.f, 0.f);

	// 기타 전역

	// 앉은 채 누른 공격을 일어선 뒤 실행해 주는 창(초). 일어서기가 더 늦게 끝나면 입력을 버린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(ClampMin="0", Units="s"))
	float CrouchAttackStandBufferSeconds = 0.25f;

	// 패드 스틱 기울기(데드존 이후 0~1)가 이 값 미만이면 걷고 이상이면 달린다. 범위 밖은 IsDataValid 가 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(UIMin="0.05", UIMax="1"))
	float AnalogWalkRunThreshold = 0.7f;

	// 서 있을 때 액터 방향이 메시 루트에서 이 각도(도) 이상 벌어지면 제자리 회전한다. (0, 180] 밖은 IsDataValid 가 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion", meta=(UIMin="1", UIMax="180", Units="deg"))
	float TurnInPlaceYawThreshold = 50.f;

	// sprint 1->2단 승급 홀드 시간(초). 프로파일 bSprintRampEnabled=true 일 때만 유효.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	float SprintRampDuration = 0.6f;

	// 이동 방향(0~180도) -> 속도 구역(0 앞, 1 옆, 2 뒤) 곡선. 비면 IsDataValid 가 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	TObjectPtr<UCurveFloat> StrafeSpeedMapCurve;

	// 질주+앉기 슬라이드. CMC 걷기 물리를 써서 바닥, 충돌, 앉기 예측이 그대로 돈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide")
	float SlideStartSpeed = 380.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide")
	float SlideEndSpeed = 200.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide")
	float SlideMaxDuration = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide")
	FVBSlideMovementSettings SlideSettings;
	// 질주를 누른 채 슬라이드가 끝날 때 "달려 나오기" 를 보이는 최소 시간(초). 그 뒤 이동 그래프가 질주로 이어 받는다.
	// 너무 짧으면 앉은 포즈에서 질주로 팝이 난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide", meta=(ClampMin="0", Units="s"))
	float SlideSprintExitHandoff = 0.25f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Slide")
	float SlideMaxSpeed = 800.f;

	// 속도 프로파일(base + override)
	// 비무장 base. config 생성자에서 초기화한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	FVBMovementProfile UnarmedProfile;

	// 무장 base(Fighter/Magic). jog 8-way 루트 속도와 Run/Sprint 클립에 맞춘 기본값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	FVBMovementProfile ArmedDefaultProfile;

	// 무기별 오버라이드(미등록 = ArmedDefault base). Blade/BigSword 는 전용 클립 속도에 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Locomotion")
	TMap<EVBWeaponType, FVBMovementProfile> WeaponProfileOverrides;

	// 무기타입, 무장여부로 프로파일 해석. 비무장=Unarmed, 오버라이드 있으면 그것, 그 외=ArmedDefault.
	const FVBMovementProfile& ResolveProfile(EVBWeaponType WeaponType, bool bArmed) const;

#if WITH_EDITOR
	// 슬라이드 끝 속도가 시작 속도보다 작아야 한다 - 같거나 크면 시작한 그 프레임에 끝난다.
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
