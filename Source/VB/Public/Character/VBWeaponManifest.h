// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

// 무기 실체화 재질(M_WeaponManifest)의 계약 한 곳 - 파라미터 이름과 드러남 범위 계산. 손에 든 소환 무기와 땅의 줍기가 같이 쓴다.
// 재질의 기본값은 아직 안 나타남(진행 0 = 다 가림)이라, 이 재질을 입은 메시는 누가 진행을 정하지 않으면 보이지 않는다.
namespace VBWeaponManifest
{
	// 메시 bounds 를 한 축에 투영한 범위. 재질의 RevealMin/RevealLength 와 형성 효과 위치가 같은 값을 쓴다.
	struct FRevealRange
	{
		FVector Axis = FVector::UpVector;
		FVector BoundsCenter = FVector::ZeroVector;
		float Min = 0.0f;
		float Max = 1.0f;
	};

	// 축(메시 로컬, 길이 0 이면 위쪽)으로 메시 bounds 를 잰다. 메시가 없으면 작은 기본 상자.
	VB_API FRevealRange MeasureReveal(const UStaticMesh* Mesh, const FVector& LocalAxis);
	// 메시 bounds 의 가장 긴 축 - 축을 따로 갖지 않는 쪽(줍기)이 쓴다.
	VB_API FVector LongestAxis(const UStaticMesh* Mesh);
	// 이 재질이 실체화 진행 파라미터를 갖는가.
	VB_API bool UsesManifest(const UMaterialInterface* Material);
	VB_API void ApplyReveal(UMaterialInstanceDynamic& Material, const FRevealRange& Range);
	VB_API void SetProgress(UMaterialInstanceDynamic& Material, float Progress);
}
