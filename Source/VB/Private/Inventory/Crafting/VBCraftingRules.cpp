// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Crafting/VBCraftingRules.h"

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Crafting/VBCraftingStationDefinition.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "VB/VB.h"

EVBCraftResult VBCrafting::CheckCraft(const FVBCraftQuote& Quote)
{
	if (!Quote.bValid)
	{
		return EVBCraftResult::Invalid;
	}

	if (!Quote.bAvailable)
	{
		return EVBCraftResult::Locked;
	}

	if (!Quote.bHasInputs)
	{
		return EVBCraftResult::MissingInputs;
	}

	if (Quote.GoldHeld < Quote.GoldRequired)
	{
		return EVBCraftResult::NotEnoughGold;
	}

	if (!Quote.bOutputFits)
	{
		return EVBCraftResult::NoRoom;
	}

	return EVBCraftResult::Ok;
}

namespace
{
	TArray<FVBItemAmount> WithGold(const TArray<FVBItemAmount>& Materials, int32 Gold, const UVBItemDefinition* Currency)
	{
		TArray<FVBItemAmount> Costs = Materials;
		if (Gold > 0 && Currency)
		{
			FVBItemAmount& GoldRow = Costs.AddDefaulted_GetRef();
			GoldRow.Item = Currency;
			GoldRow.Count = Gold;
		}

		return Costs;
	}
}

TArray<FVBItemAmount> VBCrafting::RecipeCosts(const FVBCraftRecipe& Recipe, const UVBItemDefinition* Currency)
{
	return WithGold(Recipe.Inputs, Recipe.Gold, Currency);
}

TArray<FVBItemAmount> VBCrafting::EnhanceStepCosts(const FVBEnhanceStep& Step, const UVBItemDefinition* Currency)
{
	return WithGold(Step.Materials, Step.Gold, Currency);
}

const TCHAR* VBCrafting::LexToString(EVBCraftResult Result)
{
	switch (Result)
	{
	case EVBCraftResult::Ok: return TEXT("Ok");
	case EVBCraftResult::Invalid: return TEXT("Invalid");
	case EVBCraftResult::Locked: return TEXT("Locked");
	case EVBCraftResult::MissingInputs: return TEXT("MissingInputs");
	case EVBCraftResult::NotEnoughGold: return TEXT("NotEnoughGold");
	case EVBCraftResult::NoRoom: return TEXT("NoRoom");
	}

	return TEXT("Unknown");
}

void FVBEffect_OpenCrafting::Apply(const FVBRuleContext& Context) const
{
	const AController* Controller = Context.Instigator ? Context.Instigator->GetOwningController() : nullptr;
	UVBCraftingComponent* Crafting = Controller ? Controller->FindComponentByClass<UVBCraftingComponent>() : nullptr;
	if (!Crafting || !Station)
	{
		VB_LOG(Warning, "효과(제작대 열기 %s): %s", *GetNameSafe(Station),
			!Station ? TEXT("제작대가 비었다") : TEXT("부른 플레이어의 PC 에 제작 컴포넌트가 없다"));
		return;
	}

	Crafting->OpenStation(Station, Context.Counterpart);
}
