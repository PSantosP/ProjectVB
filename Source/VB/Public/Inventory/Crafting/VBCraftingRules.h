// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Inventory/VBItemAmount.h"
#include "VBCraftingRules.generated.h"

class UVBCraftingStationDefinition;
class UVBItemDefinition;
struct FVBCraftRecipe;
struct FVBEnhanceStep;

/** 제작 판정의 답. 화면은 이것으로 버튼을 켜고 끄고 까닭을 보인다. */
UENUM(BlueprintType)
enum class EVBCraftResult : uint8
{
	Ok,
	// 제작법이 없거나 결과가 비었다 - 정상 화면에서는 나오지 않는 요청.
	Invalid,
	// 제작법의 조건이 거짓.
	Locked,
	MissingInputs,
	NotEnoughGold,
	// 결과의 상한에 걸려 다 받을 수 없다. 재료만 빠지는 반쪽 제작을 막으려고 아예 안 만든다.
	NoRoom,
};

/** 제작 판정에 드는 사실. 서버(결정)와 화면(버튼)이 같은 판정을 쓴다 - 모으는 쪽 = UVBCraftingComponent::BuildCraftQuote. */
struct FVBCraftQuote
{
	bool bValid = false;
	bool bAvailable = true;
	bool bHasInputs = false;
	int32 GoldRequired = 0;
	int32 GoldHeld = 0;
	bool bOutputFits = false;
};

namespace VBCrafting
{
	VB_API EVBCraftResult CheckCraft(const FVBCraftQuote& Quote);

	// 제작법이 내는 것 전부(재료 + 돈 한 줄). 돈이 0 이거나 돈 아이템이 없으면 돈 줄을 넣지 않는다.
	VB_API TArray<FVBItemAmount> RecipeCosts(const FVBCraftRecipe& Recipe, const UVBItemDefinition* Currency);

	// 강화 한 단계가 내는 것 전부(재료 + 돈 한 줄).
	VB_API TArray<FVBItemAmount> EnhanceStepCosts(const FVBEnhanceStep& Step, const UVBItemDefinition* Currency);

	VB_API const TCHAR* LexToString(EVBCraftResult Result);
}

/** 제작대를 연다(대화, 작업대 완료 효과). 문맥의 상대를 전달하고, 상대가 사라지면 닫힌다. */
USTRUCT(BlueprintType, meta=(DisplayName="제작대 열기"))
struct VB_API FVBEffect_OpenCrafting : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Crafting")
	TObjectPtr<const UVBCraftingStationDefinition> Station;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
