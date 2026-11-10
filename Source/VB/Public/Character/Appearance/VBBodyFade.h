#pragma once

#include "CoreMinimal.h"
#include "VBBodyFade.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UVBBodyFadeConfig;

USTRUCT()
struct FVBBodyFadeSlot
{
	GENERATED_BODY()
	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMesh> Asset;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Original;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Applied;
	int32 Index = INDEX_NONE;
};

/** 외형 소유자가 임시로 맡긴 재질 슬롯만 바꾸고 원래 상태로 되돌린다. */
USTRUCT()
struct VB_API FVBBodyFade
{
	GENERATED_BODY()
public:
	uint64 Begin(const TArray<USkeletalMeshComponent*>& Meshes, const UVBBodyFadeConfig* InConfig, float InDuration);
	void End(uint64 Request);
	void Reset();
	void Tick(float DeltaTime);
	bool IsActive() const { return ActiveRequest != 0; }
private:
	UPROPERTY(Transient)
	TArray<FVBBodyFadeSlot> Slots;
	UPROPERTY(Transient)
	TObjectPtr<const UVBBodyFadeConfig> Config;
	uint64 ActiveRequest = 0;
	uint64 NextRequest = 0;
	float Age = 0.0f;
	float Duration = 0.0f;
};
