#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPGraphAuthoring.generated.h"

class UBlueprint;
class UAnimBlueprint;

/** 가져온 GASP 계약을 위한 에디터 전용 저작. 월드를 띄우지 않는다. */
UCLASS()
class VBANIMATIONEDITOR_API UVBGASPGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString DescribeGraph(UBlueprint* Blueprint, FName GraphName);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString DescribeStruct(UScriptStruct* Struct);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool InstallCharacterInterface(UBlueprint* Blueprint, UBlueprint* Interface);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool AppendWeaponDatabaseOverride(UAnimBlueprint* Blueprint);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool InstallLinkedLocomotion(UAnimBlueprint* Blueprint, UAnimBlueprint* Locomotion);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool InstallRootOffsetOverride(UAnimBlueprint* Blueprint, FName FunctionName);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool InstallAnimationInterfaceForwarding(UAnimBlueprint* Blueprint, UBlueprint* Interface);
};
