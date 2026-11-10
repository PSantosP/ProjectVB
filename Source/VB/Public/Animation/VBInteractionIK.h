// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchConstraints.h"   // FPoseSearchConstraint
#include "PoseSearch/PoseSearchLibrary.h"       // FPoseSearchBlueprintResult
#include "VBInteractionIK.generated.h"

class UAnimInstance;
class UAnimMontage;
class UPoseSearchDatabase;

/**
 * 상호작용 중 한쪽 손이 상대의 어디를 잡을지. AnimGraph 의 IK 노드가 읽는다.
 * 모션워핑이 몸통을 맞춘 뒤 골격 차이로 남는 손끝 오차를 이 목표가 보정한다.
 */
USTRUCT(BlueprintType)
struct FVBInteractionIKHand
{
	GENERATED_BODY()

	// 잡아야 할 자리(컴포넌트 공간).
	UPROPERTY(BlueprintReadOnly, Category="VB|Finisher")
	FTransform Target = FTransform::Identity;

	// 잡는 강도(0~1). 엔진이 노티파이 램프로 만든다 - 우리가 보간하지 않는다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Finisher")
	float Alpha = 0.0f;
};

/** 상호작용 손 IK 런타임 상태. 플레이어, 적 AnimInstance 가 같은 덩어리를 멤버로 든다. */
USTRUCT(BlueprintType)
struct FVBInteractionIKRuntime
{
	GENERATED_BODY()

	// 상호작용 몽타주가 실제로 돌고 있을 때만 참.
	UPROPERTY(BlueprintReadOnly, Category="VB|Finisher")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category="VB|Finisher")
	FVBInteractionIKHand Left;

	UPROPERTY(BlueprintReadOnly, Category="VB|Finisher")
	FVBInteractionIKHand Right;

	// IK 대상 소켓. 클립의 제약 노티파이 이름과 같아야 한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FName LeftSocket = FName("hand_l");

	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	FName RightSocket = FName("hand_r");

	// 엔진이 채우고 지우는 제약 목록. 램프업이 누적되므로 프레임 간 유지한다.
	UPROPERTY(Transient)
	TArray<FPoseSearchConstraint> Constraints;

	// 직전 밀기 섹션. 복제된 섹션이 바뀔 때 블렌드를 요청한다.
	TWeakObjectPtr<UAnimMontage> PreviousPushMontage;
	FName PreviousPushSection;
};

/**
 * 상호작용 손 IK 갱신. 플레이어, 적 AnimInstance 가 매 프레임 부른다.
 * GASP Update_InteractionConstraints 의 C++ 판. 결과는 IVBInteractionAnimInterface 에서 읽는다.
 */
UCLASS()
class VB_API UVBInteractionIKLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 제약을 갱신하고 두 손의 IK 목표를 낸다. 몽타주 상태를 보므로 게임 스레드 전용. */
	static void Update(UAnimInstance* AnimInstance,
		const FPoseSearchBlueprintResult& Result,
		float DeltaSeconds,
		FVBInteractionIKRuntime& InOutRuntime);

	/**
	 * 이 AnimInstance 의 골격이 스키마가 그 역할에 선언한 골격과 같은가. 모르면 false.
	 * 엔진은 짝의 골격이 스키마와 같다고 믿고 본 인덱스를 쓰므로, 다르면 크래시한다.
	 */
	static bool DoesSkeletonMatchSchema(const UPoseSearchDatabase* Database,
		FName Role,
		const UAnimInstance* AnimInstance);
};
