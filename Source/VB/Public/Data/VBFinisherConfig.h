// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBFinisherConfig.generated.h"

class UContextualAnimSceneAsset;
class UAnimMontage;
class UPoseSearchDatabase;

/** 처형 이동 경로를 검사할 간격과 캡슐 여유다. */
USTRUCT()
struct FVBFinisherSpaceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.01", ClampMax="0.1", Units="s"))
	float SampleInterval = 1.0f / 30.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", Units="cm"))
	float HorizontalMargin = 2.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", ClampMax="5.0", Units="cm"))
	float GroundTolerance = 2.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="2", ClampMax="512"))
	int32 MaxSamplesPerRole = 256;

	bool IsValid() const
	{
		return FMath::IsFinite(SampleInterval) && SampleInterval >= 0.01f && SampleInterval <= 0.1f
			&& FMath::IsFinite(HorizontalMargin) && HorizontalMargin >= 0.0f
			&& FMath::IsFinite(GroundTolerance) && GroundTolerance >= 0.0f && GroundTolerance <= 5.0f
			&& MaxSamplesPerRole >= 2 && MaxSamplesPerRole <= 512;
	}
};

/** 처형 카메라의 장애물 여유와 구도 보정 범위다. */
USTRUCT()
struct FVBFinisherCameraCollisionSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="1.0", Units="cm"))
	float MinimumProbeRadius = 16.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", Units="cm"))
	float SurfaceMargin = 3.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="1.0", Units="cm"))
	float MinimumViewDistance = 60.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", Units="cm"))
	float MaxLateralAdjustment = 80.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", Units="cm"))
	float MaxHeightAdjustment = 60.0f;

	UPROPERTY(EditAnywhere, Category="VB|Finisher", meta=(ClampMin="0.0", Units="cm"))
	float CandidateSwitchMargin = 12.0f;

	bool IsValid() const
	{
		return FMath::IsFinite(MinimumProbeRadius) && MinimumProbeRadius > 0
			&& FMath::IsFinite(SurfaceMargin) && SurfaceMargin >= 0
			&& FMath::IsFinite(MinimumViewDistance) && MinimumViewDistance > 0
			&& FMath::IsFinite(MaxLateralAdjustment) && MaxLateralAdjustment >= 0
			&& FMath::IsFinite(MaxHeightAdjustment) && MaxHeightAdjustment >= 0
			&& FMath::IsFinite(CandidateSwitchMargin) && CandidateSwitchMargin >= 0;
	}
};

/** 처형 한 벌이 걸리는 방향. 시작 순간 두 액터의 yaw 차로 정한다. */
UENUM(BlueprintType)
enum class EVBFinisherApproach : uint8
{
	Front UMETA(DisplayName="Front"), // 마주 본 상태에서 시작
	Back UMETA(DisplayName="Back"), // 등 뒤에서 시작
};

/** 카메라가 이 키에 도달하는 방식. */
UENUM(BlueprintType)
enum class EVBFinisherCameraInterp : uint8
{
	// 앞뒤 키를 함께 보는 부드러운 곡선(기본).
	Curve UMETA(DisplayName="커브 (부드럽게, 기본)"),

	// 일정 속도의 직선.
	Linear UMETA(DisplayName="직선 (일정 속도)"),

	// 앞 키에 머물다가 마지막 BlendTime 초에 옮겨간다.
	Hold UMETA(DisplayName="머물다 이동 (블렌드 시간)"),

	// 그 시각에 즉시 바꾼다.
	Cut UMETA(DisplayName="컷 (즉시)"),
};

/**
 * 처형 중 카메라가 지나갈 한 지점. 시각은 씬 재생 시간, 위치는 앵커 기준 상대값이다.
 * 멤버가 EditAnywhere 인 이유: 파이썬 저작 경로가 EditDefaultsOnly 를 거부한다.
 */
USTRUCT(BlueprintType)
struct FVBFinisherCameraKey
{
	GENERATED_BODY()

	// 씬 재생 시간(초). 0 = 처형 시작.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.0"))
	float Time = 0.0f;

	// 카메라가 설 자리(앵커 기준).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher")
	FVector RelativeLocation = FVector::ZeroVector;

	// 카메라가 볼 지점(앵커 기준). 회전은 위치와 이 점에서 계산해 체격이 달라도 구도가 유지된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher")
	FVector LookAtOffset = FVector(0.0f, 0.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="5.0", ClampMax="170.0"))
	float FOV = 90.0f;

	// 이 키에 도달하는 방식.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher",
		meta=(DisplayName="도달 방식"))
	EVBFinisherCameraInterp Interp = EVBFinisherCameraInterp::Curve;

	// 「머물다 이동」에서 마지막 몇 초에 옮겨갈지. 다른 방식에서는 숨긴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.0",
		EditCondition="Interp == EVBFinisherCameraInterp::Hold", EditConditionHides))
	float BlendTime = 0.2f;
};

/** 처형 중 한 번 거는 슬로우. 적용은 UVBTimeDilationSubsystem::PushTimed 가 맡는다. */
USTRUCT(BlueprintType)
struct FVBFinisherTimeDilationSegment
{
	GENERATED_BODY()

	// 씬 재생 시간(초). 카메라 키와 같은 축.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.0"))
	float Time = 0.0f;

	// 1.0 = 정상, 0.01 = 거의 정지.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.01", ClampMax="1.0"))
	float Scale = 0.5f;

	// 실제 시간 기준 길이(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.0"))
	float RealDuration = 0.3f;
};

/**
 * 처형 한 벌의 연출(카메라, 슬로우, 피해 배수, 기상). 두 캐릭터의 안무는 씬 자산이 가진다.
 * 새 처형은 씬 자산 하나와 이 설정 하나로 추가한다.
 */
UCLASS(BlueprintType)
class VB_API UVBFinisherConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 두 캐릭터의 안무(역할, 클립, 상대 정렬, 섹션).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	TObjectPtr<UContextualAnimSceneAsset> Scene = nullptr;

	// 이 처형의 검색 후보 한 벌. 자세는 MotionMatchMulti 가 고른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	TObjectPtr<UPoseSearchDatabase> InteractionDatabase = nullptr;

	// 앞에서 걸리는 처형인가 뒤에서 걸리는 처형인가.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	EVBFinisherApproach Approach = EVBFinisherApproach::Front;

	// 카메라 경로. 비면 게임플레이 카메라를 그대로 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(TitleProperty="{Time}초  FOV {FOV}"))
	TArray<FVBFinisherCameraKey> CameraKeys;

	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FVBFinisherCameraCollisionSettings CameraCollision;

	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FVBFinisherSpaceSettings Space;

	// 카메라 키의 기준이 되는 씬 역할. Scene 의 역할 중에서 고른다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(GetOptions="GetSceneRoleOptions"))
	FName CameraAnchorRole = FName("Victim");

	// 게임플레이 카메라로 되돌아가는 블렌드 시간.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.0"))
	float CameraReturnBlend = 0.35f;

	// 처형 시작 때 적을 플레이어 기준 자리로 데려오는 시간(초). 0 이면 한 프레임에 옮긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="적을 데려오는 시간(초)"))
	float AlignBlendTime = 0.15f;

	// 처형을 버틴 대상이 일어나는 몽타주(처형 클립과 짝). 비면 기상 연출 없이 선다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(DisplayName="버텼을 때 일어나는 몽타주"))
	TObjectPtr<UAnimMontage> GetUpMontage = nullptr;

	// 기상 방향 손보정(도). 각도는 UVBGA_GetUp 이 재서 맞추므로 보통 0 이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(ClampMin="-180.0", ClampMax="180.0", DisplayName="기상 회전 손보정(도, 보통 0)"))
	float GetUpYawOffset = 0.0f;

	// 슬로우 구간. 비면 슬로우 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(TitleProperty="{Time}초  x{Scale}  {RealDuration}초간"))
	TArray<FVBFinisherTimeDilationSegment> TimeDilationSegments;

	// 처형 타격 순간의 큐. 비면 판정 모양의 기본 큐를 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher", meta=(Categories="GameplayCue"))
	FGameplayTag StrikeCueTag;

	// 처형 피해 배수. 피해 GameplayEffect 에 SetByCaller 로 넘어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="1.0"))
	float DamageMultiplier = 3.0f;

	// 중간에 빠져나올 수 있는가. 켜면 이탈 때 입력, 슬로우, 카메라, 태그를 차례로 풀어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	bool bAllowEarlyOut = false;

	// CameraAnchorRole 드롭다운 - Scene 이 가진 역할 이름.
	UFUNCTION()
	TArray<FString> GetSceneRoleOptions() const;

	/** 이 시각의 카메라 구도(앵커 기준)를 낸다. 런타임과 저작 도구가 함께 쓴다. 키가 없으면 false. */
	bool EvaluateCameraAt(float SceneTime, FVector& OutRelLocation, FVector& OutRelLookAt, float& OutFOV) const;

#if WITH_EDITOR
	// 아무 일도 안 일어나는 처형을 저장 시점에 막는다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
