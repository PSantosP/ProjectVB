// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchLibrary.h"   // FPoseSearchBlueprintResult
#include "UObject/Interface.h"
#include "VBInteractionAnimInterface.generated.h"

UINTERFACE(MinimalAPI, NotBlueprintable)
class UVBInteractionAnimInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 상호작용 결과를 드는 AnimInstance. UVBFinisherComponent 가 채우고 비우며 InteractionWarp 가 읽는다.
 * 구현 클래스는 저장 필드에 UPROPERTY 를 달아야 한다 - 결과 안의 SelectedAnim 을 GC 가 가져간다.
 */
class VB_API IVBInteractionAnimInterface
{
	GENERATED_BODY()

public:
	/** 지금 배정된 상호작용. 없으면 기본 구조체(`bIsInteraction=false`)다. */
	virtual const FPoseSearchBlueprintResult& GetInteractionResult() const = 0;

	/** 결과를 채우거나(시작) 비운다(종료). */
	virtual void SetInteractionResult(const FPoseSearchBlueprintResult& InResult) = 0;
};
