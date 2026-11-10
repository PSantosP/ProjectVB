#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBLookAtPOIAuthoring.generated.h"
class UAnimBlueprint;
class UDataAsset;
class USkeleton;

UCLASS()
class VBANIMATIONEDITOR_API UVBLookAtPOIAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static UAnimBlueprint* CreateLookAtLayer(UAnimBlueprint* Reference, UDataAsset* Settings, const FString& PackagePath);
	/** 애니메이션된 poi 본이 없는 마네킹용으로 프로젝트 소유 사본을 설정한다. */
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static bool ConfigureLookAtLayerForSkeleton(UAnimBlueprint* Layer, USkeleton* Skeleton, UDataAsset* Settings,
		FVector BaselineOffset, const TArray<FName>& DrivenBones);
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static bool ConnectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory = true);
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static FString InspectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory = true);
};
