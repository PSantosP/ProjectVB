#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

class UWorld;
class UVBFinisherConfig;
struct FPoseSearchBlueprintResult;
struct FVBFinisherSpaceSettings;

/** 선택 후보 한 역할의 월드 캡슐 경로다. */
struct FVBFinisherSpacePath
{
	TArray<FVector> Centers;
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	ECollisionChannel Channel = ECC_Pawn;
	FCollisionResponseContainer Responses;
};

namespace VBFinisherSpaceRules
{
	// 두 역할의 정렬된 루트 경로를 검사해 실제 이동 공간이 있는지 판정한다.
	VB_API bool HasInteractionSpace(TConstArrayView<FPoseSearchBlueprintResult> Results,
		const UVBFinisherConfig* Config);

	// 실제 시작점과 정렬된 루트 표본을 캡슐 경로로 바꾼다.
	VB_API bool BuildPath(TConstArrayView<FTransform> Roots, const FTransform& MeshRelative,
		const FVector& CurrentCenter, bool bGrounded, float Radius, float HalfHeight,
		const FVBFinisherSpaceSettings& Settings, FVBFinisherSpacePath& OutPath);

	// 두 역할의 경로 중 하나라도 월드 충돌에 막히면 거절한다.
	VB_API bool IsClear(UWorld& World, TConstArrayView<FVBFinisherSpacePath> Paths,
		TConstArrayView<const AActor*> Participants);
}
