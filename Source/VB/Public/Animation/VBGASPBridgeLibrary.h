#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "VBGASPBridgeLibrary.generated.h"

class ACharacter;
class UAnimInstance;

/** 가져온 Blueprint 경계의 adapter - gameplay 는 여전히 VB 가 관리한다. */
UCLASS()
class VB_API UVBGASPBridgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// wildcard 는 에디터 저작 도구가 원래 GASP 반환 struct 에 연결한다
	UFUNCTION(BlueprintCallable, CustomThunk, Category="VB|GASP", meta=(CustomStructureParam="Properties"))
	static void ReadCharacterProperties(ACharacter* Character, int32& Properties);
	DECLARE_FUNCTION(execReadCharacterProperties);

	UFUNCTION(BlueprintCallable, Category="VB|GASP", meta=(BlueprintThreadSafe))
	static void ApplyWeaponDatabases(UAnimInstance* LinkedInstance, const FAnimNodeReference& Node);

	UFUNCTION(BlueprintPure, Category="VB|GASP", meta=(BlueprintThreadSafe))
	static EOffsetRootBoneMode ConstrainRootOffsetMode(UAnimInstance* LinkedInstance, EOffsetRootBoneMode Mode);

	static bool FillCharacterProperties(ACharacter* Character, UScriptStruct* Struct, void* Output);
	/** worker animation 평가 전에 gameplay 가 가진 값을 잡아 둔다. */
	static void CaptureCMCSelection(UAnimInstance* LinkedInstance);
};
