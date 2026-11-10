#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBEffectAuthoring.generated.h"

class UParticleSystem;
class UParticleModule;

/** 이펙트 자산의 구성 조회와 분포 캐시 갱신을 맡는 에디터 도구 */
UCLASS()
class VBANIMATIONEDITOR_API UVBEffectAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Cascade 이미터와 LOD별 형식 및 모듈 구성 조회 */
	UFUNCTION(BlueprintPure, Category="VB|EffectAuthoring")
	static TArray<FString> DescribeCascade(UParticleSystem* System);

	/** 모듈이 가진 float 또는 vector 분포의 저장용 캐시를 갱신한다 */
	UFUNCTION(BlueprintCallable, Category="VB|EffectAuthoring")
	static bool RebuildCascadeDistribution(UParticleModule* Module, FName PropertyName);
};
