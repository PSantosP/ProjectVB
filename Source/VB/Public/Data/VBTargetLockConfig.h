// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // ECollisionChannel (SearchTraceChannel) - VBAttackConfig.h 와 동일 패턴
#include "Engine/DataAsset.h"
#include "VBTargetLockConfig.generated.h"

/** 락온 대상을 보면서 소유 캐릭터 뒤에서 잡는 임시 구도 */
USTRUCT(BlueprintType)
struct VB_API FVBTargetLockFraming
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="VB|Camera") float MinPitch = -65.0f;
	UPROPERTY(EditAnywhere, Category="VB|Camera") float MaxPitch = 20.0f;
	UPROPERTY(EditAnywhere, Category="VB|Camera") float RearYawOffset = 0.0f;
	UPROPERTY(EditAnywhere, Category="VB|Camera", meta=(ClampMin="0.1")) float InterpSpeed = 16.0f;
	UPROPERTY(EditAnywhere, Category="VB|Camera", meta=(ClampMin="0.1")) float MaxTrackingDegreesPerSecond = 540.0f;
	/** 대상 사망 뒤 현재 구도를 이어갈 최대 시간 */
	UPROPERTY(EditAnywhere, Category="VB|Camera", meta=(ClampMin="0.0")) float DefeatFollowSeconds = 0.0f;
	bool IsValid() const;
	FRotator DesiredRotation(float OwnerYaw, const FVector& CameraLocation, const FVector& Aim) const;
};

/**
 * 락온(Target Lock) 탐색, 해제 수치와 전투 카메라 프레이밍 DataAsset - 디자이너가 에디터에서 바로 튜닝하려고 뺐다
 * VBTargetLockComponent 가 참조하고, 비어 있으면 CDO 기본값을 쓴다
 */
UCLASS(BlueprintType)
class VB_API UVBTargetLockConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 락온 탐색/해제
	// 락온 최대 가능 거리
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float MaxLockDistance = 800.0f;

	// 락온 자동 해제 거리(이보다 멀어지면 해제)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float LockBreakDistance = 1200.0f;

	// 화면 중앙 기준 락온 탐색 각도(반각, 도)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float LockAngle = 45.0f;

	// 타겟 검색 반경
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float TargetSearchRadius = 800.0f;

	// 카메라 회전 보간 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float InterpSpeed = 10.0f;

	// 락온 중 카메라 pitch 클램프(도) - MinCameraPitch 는 내려다보는 최대각
	// 근접하면 look-at 이 이 값에 걸려 탑다운이 되므로 완만하게 둔다 (멀리서는 영향 없다)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float MinCameraPitch = -20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float MaxCameraPitch = 15.0f;

	// 이전 자산 호환용. 현재 회전은 입력 오프셋 합성을 쓰며 이 값은 소비하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float LockOnLookDampingFactor = 0.1f;

	// 타겟 선정 점수에서 거리 점수의 가중치(방향 dot 점수 대비).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float DistanceScoreWeight = 0.3f;

	// 근접 공격 명중 때 맞은 적으로 자동 락온할지 - 이미 수동 락온 중이면 무시하고, 스윙당 최대 1회
	// 기본은 자유 시점이고 공격 보정은 소프트 런지 경로가 맡는다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	bool bAutoLockOnHit = false;

	// 지속 락온과 독립: 실제 근접 피해 후 잠깐만 정렬한다. 값은 플레이로 조정할 초기값.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Camera|HitRecenter")
	bool bRecenterCameraOnHit = true;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.01"))
	float HitRecenterDuration = 0.25f;
	// 마지막 명중 뒤 이만큼 쉬어야 새 보정을 허용한다. 연타로 보정이 계속 재시작되지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.0"))
	float HitRecenterQuietSeconds = 0.8f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.0"))
	float HitRecenterInputGraceSeconds = 0.35f;
	// 화면 중앙에서의 정규화 반폭/반높이. 이 안에 보이면 카메라를 움직이지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter")
	FVector2D HitRecenterSafeHalfExtent = FVector2D(0.25, 0.30);
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.0", ClampMax="180.0"))
	float HitRecenterMaxYaw = 35.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.0", ClampMax="90.0"))
	float HitRecenterMaxPitch = 12.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera|HitRecenter", meta=(ClampMin="0.0"))
	float HitRecenterMaxNotificationAge = 0.35f;

	// 가드, 패링 자동 락온 값은 읽는 쪽인 UVBGA_Guard, UVBParryConfig 에 있다

	// 소프트 런지(락온 없이 첫 스윙 돌진) - 정면 근접 첫 타라 락온보다 좁은 콘, 짧은 반경을 따로 둔다

	// 소프트 런지 후보 탐색 반경(cm). 락온 TargetSearchRadius 와 따로 조정한다(지금 기본값은 둘 다 800) - 근접 첫 스윙 돌진 거리만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float SoftLungeRadius = 800.0f;

	// 소프트 런지 정면 콘 반각(도). 락온 LockAngle 과 따로 조정한다(지금 기본값은 둘 다 45) - "정면" 의도 엄격히(오조준 방지).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float SoftLungeAngle = 45.0f;

	// 락온 중 방향 공격 전환 - 공격할 때 이동 입력이 다른 적을 분명히 가리키면 그 적으로 락온과 공격을 전환한다
	// 값은 플레이로 맞출 초기값이고 VBTargetLockPolicy::PickDirectionalTarget 이 쓴다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional")
	bool bDirectionalAttackSwitch = true;
	// 가공된 이동 입력 크기(0~1) 이하면 방향 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DirectionalMinInputMagnitude = 0.35f;
	// 후보 콘 반각(도) - 입력 방향에서 이 안의 적만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional",
		meta=(ClampMin="0.0", ClampMax="180.0", Units="Degrees"))
	float DirectionalCandidateHalfAngle = 45.0f;
	// 지금 대상 쪽 유지 콘 반각(도) - 입력이 지금 대상을 이만큼 가리키면 바꾸지 않는다. 후보 콘보다 작아야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional",
		meta=(ClampMin="0.0", ClampMax="180.0", Units="Degrees"))
	float DirectionalCurrentKeepHalfAngle = 20.0f;
	// 새 대상은 지금 대상보다 이만큼(도) 더 입력 방향에 맞아야 한다 - 애매한 옆걸음이 대상을 바꾸지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", Units="Degrees"))
	float DirectionalAngleAdvantage = 10.0f;
	// 가장 맞는 각에서 이 폭(도) 안의 후보는 거리로 가른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", Units="Degrees"))
	float DirectionalAngleTieBand = 3.0f;
	// 첫 타의 전환 거리(cm). 락온 획득 거리(MaxLockDistance, TargetSearchRadius)로도 다시 깎인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", Units="Centimeters"))
	float DirectionalMaxDistance = 600.0f;
	// 콤보 다음 타의 전환 거리(cm) - 이미 고른 변형이라 더 짧게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", Units="Centimeters"))
	float DirectionalChainMaxDistance = 300.0f;
	// 높이 차 한계(cm) - 다른 층의 적을 거른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn|Directional", meta=(ClampMin="0.0", Units="Centimeters"))
	float DirectionalMaxVerticalDelta = 150.0f;

	// 타겟 전환 입력(락온 중 좌우 스위치) - (VBCharacter 에서 이동 - 논리상 락온 튜닝)
	// 이전 자산 호환용. 마우스 이동 전환은 폐기되었고 휠 펄스를 사용한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float SwitchTargetMouseThreshold = 1.5f;

	// 이전 자산 호환용. 패드는 중립 복귀 래치, 요청 중에는 서버 응답으로 연쇄 전환을 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	float SwitchTargetCooldown = 0.3f;

	// 타겟 탐색(SphereOverlap) 오브젝트 채널. UEngineTypes::ConvertToObjectType 로 변환해 사용.
	// 왜: FindBestTarget/FindSideTarget/FindSoftLungeTarget 3곳 하드코딩 ECC_Pawn 외부화. 현행 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|LockOn")
	TEnumAsByte<ECollisionChannel> SearchTraceChannel = ECC_Pawn;

	// 화면 좌표는 로컬 viewport에 대해 0..1로 정규화한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0"))
	float ScreenVerticalWeight = 1.5f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinSwitchHorizontalDelta = 0.015f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StickSwitchPressThreshold = 0.65f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0", ClampMax="0.99"))
	float StickSwitchReleaseThreshold = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn")
	bool bAutoRetargetOnDeath = false;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0"))
	float ManualUnlockHitDelay = 1.5f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0"))
	float AcquisitionDistanceWeight = 0.15f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RetreatSprintDotThreshold = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0"))
	float RetreatSprintUnlockSeconds = 0.2f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn")
	TEnumAsByte<ECollisionChannel> VisibilityTraceChannel = ECC_Visibility;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.01"))
	float VisibilityCheckInterval = 0.1f;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn", meta=(ClampMin="0.0"))
	float OcclusionGraceSeconds = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float MaxLookYawOffset = 20.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float MaxLookPitchOffset = 10.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float LookOffsetReturnDelay = 0.6f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float LookOffsetReturnSpeed = 5.0f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float TrackingDeadZoneDegrees = 0.5f;
	UPROPERTY(EditDefaultsOnly, Category="VB|Camera", meta=(ClampMin="0.0"))
	float MaxTrackingDegreesPerSecond = 180.0f;
};
