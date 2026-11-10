#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBChooserAuthoring.generated.h"

class UChooserTable;

/** 기존 Chooser float-range 행을 살피고 조정하는 좁은 편집기 동작. */
UCLASS()
class VBANIMATIONEDITOR_API UVBChooserAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 열, 바인딩, 행이 없으면 자산을 바꾸지 않고 false 를 돌려준다. 확인한 뒤 따로 저장한다. */
	UFUNCTION(BlueprintCallable, Category="VB|Animation|Editor")
	static bool SetFloatRangeRow(UChooserTable* Table, FName BindingName, int32 RowIndex,
		float Min, float Max, bool bNoMin, bool bNoMax);
};
