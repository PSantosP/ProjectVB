// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBInteractionReplay.generated.h"

class UMultiAnimAsset;
class UPoseSearchDatabase;
class UVBFinisherConfig;

/**
 * 상호작용 처형 한 번의 복제 값(서버의 선택만). AnimInstance 는 수신 쪽에서 재구성한다.
 * 선택 칸은 FVBFinisherSelection 이, 진행 칸은 UVBFinisherComponent 가 채운다. 역할별 배열은 역할 번호로 색인한다.
 */
USTRUCT()
struct FVBInteractionReplay
{
	GENERATED_BODY()
	UPROPERTY() int32 SessionId = 0;
	UPROPERTY() bool bActive = false;
	UPROPERTY() bool bVictimReleased = false;
	UPROPERTY() TObjectPtr<UMultiAnimAsset> Asset = nullptr;
	UPROPERTY() TObjectPtr<UPoseSearchDatabase> Database = nullptr;
	UPROPERTY() TObjectPtr<const UVBFinisherConfig> Config = nullptr;
	UPROPERTY() TArray<TObjectPtr<AActor>> Actors;
	UPROPERTY() TArray<float> StartTimes;
	UPROPERTY() TArray<float> PlayRates;
	UPROPERTY() TArray<uint8> SupersededMontagePlayIds;
	UPROPERTY() TArray<FTransform> RootTransforms;
	UPROPERTY() TArray<FTransform> RootBoneTransforms;
	UPROPERTY() FVector BlendParameters = FVector::ZeroVector;
	UPROPERTY() double ServerStartTime = 0.0;
};
