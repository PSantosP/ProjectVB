#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBEnvironmentCameraAuthoring.generated.h"
class UCameraAsset;
class UVBEnvironmentCameraConfig;

UCLASS()
class VBANIMATIONEDITOR_API UVBEnvironmentCameraAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Python 에 안 드러난 native camera node 까지 reflection 기본값을 읽는다. */
	UFUNCTION(BlueprintCallable, Category="VB|Camera|Editor")
	static FString InspectCameraObject(UObject* Asset);
	/** 프로젝트가 가진 복제본만 받고, 패키지는 저장하지 않는다. */
	UFUNCTION(BlueprintCallable, Category="VB|Camera|Editor")
	static bool WireEnvironmentCamera(UCameraAsset* Camera, UBlueprint* Director, UBlueprint* Character,
		UVBEnvironmentCameraConfig* Config);
};
