#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPLocomotionAuthoring.generated.h"

class UAnimBlueprint;
class UInputAction;
class UInputMappingContext;
class UChooserTable;
class UAnimationAsset;
class UPoseSearchDatabase;

UCLASS()
class VBANIMATIONEDITOR_API UVBGASPLocomotionAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// 빈 문자열이면 성공 - installer 는 모든 계약이 통과한 뒤에만 저장한다
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString BuildCMCAnimationChooser(UChooserTable* Source, UChooserTable* Destination, UClass* ContextClass,
		UChooserTable* PoseMatchReference, UAnimationAsset* WalkSlope, UAnimationAsset* RunSlope, UPoseSearchDatabase* TransitionReference);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString BuildWeaponAnimationChooser(UChooserTable* MasterChooser, UChooserTable* Destination, UClass* ContextClass);

	// 다시 불러온 뒤에도 빈 문자열이면 성공 - index 확인을 켜면 "index pending" 이 올 수 있다
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString ValidateCMCAnimationChooser(UChooserTable* Root, bool bValidateSearchIndex = false);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString BuildCMCCompatibilityDatabaseChooser(UChooserTable* Source, UChooserTable* Destination, UClass* ContextClass);

	// Python installer 가 먼저 상속 override 를 만들고, 여기서는 고른 프로젝트 override 만 바꾼다 (원본 GASP 는 그대로)
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool InstallNativeOverride(UAnimBlueprint* Blueprint, FName FunctionName,
		TSubclassOf<UBlueprintFunctionLibrary> Library, UObject* ConstantObject = nullptr);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static TArray<FName> GetNativeFunctions(TSubclassOf<UBlueprintFunctionLibrary> Library);

	// 저장 전에 컴파일한다 - thumbnail 평가에 Slot mixer subsystem 이 필요하다
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool CompileAndValidateMixer(UAnimBlueprint* Blueprint);

	UFUNCTION(BlueprintPure, Category="VB|GASP|Editor")
	static FString DescribeEnum(UEnum* Enum);

	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static bool MapUnassignedInput(UInputMappingContext* Context, UInputAction* Action, FName KeyName);
};
