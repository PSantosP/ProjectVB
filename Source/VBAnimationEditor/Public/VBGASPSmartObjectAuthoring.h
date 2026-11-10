#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPSmartObjectAuthoring.generated.h"
class UBlueprint;
class UPoseSearchDatabase;
UCLASS()
class VBANIMATIONEDITOR_API UVBGASPSmartObjectAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 원본 저작 자산을 복제하고, 컴파일 전에 실행 BP 노드, context 를 바꾼다 */
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static FString BuildSmartObjectAssets(UBlueprint* CharacterReference, const FString& DestinationPath);
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static FString CheckSmartObjectSearchIndex(const FString& DestinationPath, bool bStartBuild);
	/** 복사한 StateTree 가 모두 컴파일되고 native 거리 index 가 만들어진 뒤에만 연결한다 */
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static bool ConnectSmartObjectCharacter(UBlueprint* CharacterBlueprint, const FString& DestinationPath);
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static FString InspectSmartObjectAssets(const FString& DestinationPath);
	/** 원본 tree 넷을 transient package 에 복사해 컴파일만 한다 - 저장하거나 게임을 돌리지 않는다 */
	UFUNCTION(BlueprintCallable, Category="VB|Authoring")
	static FString ValidateSmartObjectStateTreeCopies();
};
