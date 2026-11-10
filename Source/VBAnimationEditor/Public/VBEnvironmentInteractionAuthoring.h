#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBEnvironmentInteractionAuthoring.generated.h"

class UAnimMontage;

UCLASS()
class VBANIMATIONEDITOR_API UVBEnvironmentInteractionAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// 세그먼트는 Grab, Idle, Push, Pull, Release 순서여야 한다.
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static bool ConfigurePushMontage(UAnimMontage* Montage);
};
