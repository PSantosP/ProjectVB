// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UPrimitiveComponent;
class USkeletalMeshComponent;
struct FGameplayCueParameters;
struct FHitResult;

/**
 * 근접 타격이 메시의 어디에 닿았는지 정하는 유일한 곳(피 이펙트, 피격 플래시가 같은 점을 쓴다).
 * 판정 접점은 메시 밖일 수 있어 물리 바디 표면으로 다시 맞춘다. 상태 없음, 클라 자세로 계산(코스메틱 전용).
 */
namespace VBBodyContact
{
	// 캡슐 중심축에서 날 쪽으로 이만큼 바깥에서 메시를 향해 쏜다(cm). 가장 두꺼운 캐릭터(트롤)의 반지름보다 커야 한다.
	inline constexpr float ProbeDistance = 90.0f;

	/** 날이 닿은 쪽: 캡슐 중심축 위 최근접점에서 날 위치로 향하는 단위벡터(면 법선은 겹침 접촉에서 뒤집혀 안 쓴다). */
	VB_API FVector ComputeBladeSide(const FVector& BladeLocation, const FVector& BodyBase, float HalfHeight, const FVector& Fallback);

	// 캐릭터는 GetMesh(), 아니면 맞은 컴포넌트, 그래도 없으면 첫 스켈레탈 메시. 메시 에셋이 없으면 null.
	VB_API USkeletalMeshComponent* FindBodyMesh(AActor* Victim, UPrimitiveComponent* HitComponent);

	/** Hit 을 메시 표면 값으로 바꾼다. 못 찾으면 접점을 날 위치로 두고 false(메시에 붙는 상처는 붙이지 않는다). */
	VB_API bool RefineOntoBody(FHitResult& Hit, const FVector& SourceLocation);

	/** 큐가 표시할 접촉점: 메시 표면으로 맞춘 HitResult 점, 없으면 큐 Location. 둘 다 없으면 false(원점에 표시하지 않는다). */
	VB_API bool ResolveCuePoint(const FGameplayCueParameters& Parameters, FVector& OutPoint);
}
