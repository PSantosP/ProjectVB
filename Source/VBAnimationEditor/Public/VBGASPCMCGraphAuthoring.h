#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPCMCGraphAuthoring.generated.h"

class UAnimBlueprint;
class UMirrorDataTable;

/** native CMC 재생 계약을 graph 에 설치하는 에디터 전용 도구 */
UCLASS()
class VBANIMATIONEDITOR_API UVBGASPCMCGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 성공이면 빈 문자열 - 컴파일, 저장은 하지 않으니 부르는 쪽이 컴파일하고 다시 읽은 뒤 저장한다 */
	UFUNCTION(BlueprintCallable, Category="VB|GASP|Editor")
	static FString InstallCMCBlendStackBindings(UAnimBlueprint* Base, UMirrorDataTable* MirrorTable);
};
