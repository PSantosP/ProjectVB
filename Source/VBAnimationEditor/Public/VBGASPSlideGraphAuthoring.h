#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPSlideGraphAuthoring.generated.h"

class UAnimBlueprint;
class AActor;

/**
 * 에디터 전용 저작 도구 - GASP 슬라이드 ABP 를 본 ABP 에 잇는 그래프 수술(궤적 그래프 이식, 연결 노드, 노티 중계)과 검증용 바닥 만들기.
 * 런타임에서는 안 쓴다. 소비처 = 파이썬 저작 스크립트.
 */
UCLASS()
class VBANIMATIONEDITOR_API UVBGASPSlideGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString AdaptTrajectoryGraph(UAnimBlueprint* Child, UAnimBlueprint* Original);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString InstallSlideLink(UAnimBlueprint* Main, UAnimBlueprint* Slide, FName LinkTag, FName ActiveVariable);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString AdaptNotifyRelay(UAnimBlueprint* Main);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static AActor* CreateSlideValidationFloor(UObject* WorldContextObject, FVector Location);
};
