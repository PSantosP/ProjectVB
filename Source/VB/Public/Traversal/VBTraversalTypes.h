// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h" // EVBGait
#include "VBTraversalTypes.generated.h"

class UPrimitiveComponent;
class USplineComponent;

/**
 * 트래버설 동작 종류. 0~3 은 GASP 값 그대로다.
 * Hang 은 몽타주가 아니라 매달려 있는 상태라 chooser 를 거치지 않는다.
 */
UENUM(BlueprintType)
enum class EVBTraversalActionType : uint8
{
	None = 0 UMETA(DisplayName="None"),
	Vault = 1 UMETA(DisplayName="Vault"),
	Hurdle = 2 UMETA(DisplayName="Hurdle"),
	Mantle = 3 UMETA(DisplayName="Mantle"),
	Hang = 4 UMETA(DisplayName="Hang"),
	Climb = 5 UMETA(DisplayName="Climb"),
};

// 트래버설 세션이 지금 어느 단계인지.
UENUM()
enum class EVBTraversalPhase : uint8
{
	None, // 세션 없음
	Move, // 넘기, 허들, 맨틀 같은 한 번짜리 몽타주
	Attached, // 면에 붙어 있음(어느 면인지는 EVBTraversalSurface)
	Exiting, // 면에서 떨어져 올라서기 몽타주 재생 중
};

// 지금 붙어 있는 면. Attached 단계에서만 None 이 아니다.
UENUM()
enum class EVBTraversalSurface : uint8
{
	None,
	Hang,
	Ladder,
	Climb,
};

/** 트래버설 트레이스 결과. GASP S_TraversalCheckResult 와 같은 필드다. */
USTRUCT(BlueprintType)
struct FVBTraversalCheckResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	EVBTraversalActionType ActionType = EVBTraversalActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	bool bHasFrontLedge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	FVector FrontLedgeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	FVector FrontLedgeNormal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	bool bHasBackLedge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	FVector BackLedgeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	FVector BackLedgeNormal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	bool bHasBackFloor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	FVector BackFloorLocation = FVector::ZeroVector;

	// actor<->frontledge ΔZ
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float ObstacleHeight = 0.0f;

	// frontledge<->backledge(또는 top impact) 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float ObstacleDepth = 0.0f;

	// backledge<->backfloor 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float BackLedgeHeight = 0.0f;

	// 트레이스가 맞은 LevelBlock_Traversable 프리미티브
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	// 전방 렛지 스플라인. 매달림 옆걸음의 좌표축이다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	TObjectPtr<USplineComponent> FrontLedgeSpline = nullptr;
};

/**
 * 네 말단의 IK 목표. 값은 월드가 아니라 각 뼈 공간 오프셋이다(ABP 노드가 BCS_BoneSpace).
 * Weight 0 이면 그 말단은 애니 그대로 둔다.
 */
USTRUCT(BlueprintType)
struct FVBHangIKTargets
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	FVector HandL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	FVector HandR = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	FVector FootL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	FVector FootR = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	float HandWeightL = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	float HandWeightR = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	float FootLWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="VB|Traversal")
	float FootRWeight = 0.0f;
};

/**
 * 트래버설 chooser 평가 컨텍스트.
 * 멤버 이름은 chooser 컬럼 바인딩 이름과 일치해야 한다.
 */
USTRUCT(BlueprintType)
struct FVBTraversalChooserParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	EVBTraversalActionType ActionType = EVBTraversalActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	EVBGait Gait = EVBGait::Walk;

	// |Velocity.XY|
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float Speed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float ObstacleHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="VB|Traversal")
	float ObstacleDepth = 0.0f;
};
