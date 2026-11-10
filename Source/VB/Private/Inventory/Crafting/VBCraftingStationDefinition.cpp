// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Crafting/VBCraftingStationDefinition.h"

#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBCraftingStationDefinition"

#if WITH_EDITOR
EDataValidationResult UVBCraftingStationDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (int32 Index = 0; Index < Recipes.Num(); ++Index)
	{
		const FVBCraftRecipe& Recipe = Recipes[Index];
		if (!Recipe.Output.Item || Recipe.Output.Count <= 0)
		{
			Context.AddError(FText::Format(LOCTEXT("NoOutput", "{0} 번 제작법: 결과 아이템이 비었거나 개수가 0 이하다."), Index));
			Result = EDataValidationResult::Invalid;
		}

		if (Recipe.Inputs.ContainsByPredicate([](const FVBItemAmount& Input) { return !Input.Item || Input.Count <= 0; }))
		{
			Context.AddError(FText::Format(LOCTEXT("BadInput", "{0} 번 제작법: 빈 재료 줄 또는 0 이하 개수."), Index));
			Result = EDataValidationResult::Invalid;
		}

		if (Recipe.Gold < 0)
		{
			Context.AddError(FText::Format(LOCTEXT("NegativeGold", "{0} 번 제작법: 돈이 음수다."), Index));
			Result = EDataValidationResult::Invalid;
		}
		// 공짜 제작법은 무한 복제다 - 재료나 돈 중 하나는 있어야 한다(강화 단계의 공짜 단계 금지와 같은 규칙).
		if (Recipe.Inputs.IsEmpty() && Recipe.Gold <= 0)
		{
			Context.AddError(FText::Format(LOCTEXT("Free", "{0} 번 제작법: 재료도 돈도 없다."), Index));
			Result = EDataValidationResult::Invalid;
		}

		const FText ConditionsWhere = FText::Format(LOCTEXT("ConditionsWhere", "{0} 번 제작법의 조건"), Index);
		if (!VBRules::ValidateStructArray(Recipe.Conditions, FVBCondition::StaticStruct(), ConditionsWhere, Context))
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	if (Recipes.IsEmpty() && !bAllowEnhancement)
	{
		Context.AddError(LOCTEXT("Empty", "제작법도 없고 강화도 끈 제작대 - 열어도 할 수 있는 것이 없다."));
		Result = EDataValidationResult::Invalid;
	}

	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
