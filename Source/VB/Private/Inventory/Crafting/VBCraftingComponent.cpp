// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Crafting/VBCraftingComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Character/VBWeaponStateComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Crafting/VBCraftingStationDefinition.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "VB/VB.h"

const FVBCraftRecipe* FVBCraftingView::GetRecipe(const FVBCraftingViewEntry& ViewEntry) const
{
	return Station && Station->Recipes.IsValidIndex(ViewEntry.RecipeIndex) ? &Station->Recipes[ViewEntry.RecipeIndex] : nullptr;
}

UVBCraftingComponent::UVBCraftingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 -> 소유 클라이언트 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다(상점 컴포넌트와 같다).
	SetIsReplicatedByDefault(true);
}

bool UVBCraftingComponent::OpenStation(const UVBCraftingStationDefinition* Station, AActor* Counterpart)
{
	return OpenSession(Station, Counterpart);
}

void UVBCraftingComponent::CloseStation()
{
	CloseSession();
}

void UVBCraftingComponent::RequestCraft(int32 RecipeIndex)
{
	if (View.IsActive())
	{
		ServerCraft(View.Station, RecipeIndex);
	}
}

void UVBCraftingComponent::RequestEnhance(const FVBEnhanceTarget& Target)
{
	if (View.IsActive())
	{
		ServerEnhance(View.Station, Target);
	}
}

void UVBCraftingComponent::RequestClose()
{
	if (View.IsActive())
	{
		ServerClose();
	}
}

bool UVBCraftingComponent::IsTargetOwned(const FVBEnhanceTarget& Target) const
{
	if (Target.IsWeapon())
	{
		const AController* Controller = Cast<AController>(GetOwner());
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		const UVBWeaponStateComponent* Weapons = Pawn ? Pawn->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
		return Weapons && Weapons->IsWeaponOwned(Target.Weapon);
	}

	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	return Target.IsItem() && Inventory && Inventory->GetCount(Target.Item) > 0;
}

FVBCraftQuote UVBCraftingComponent::BuildCraftQuote(const FVBCraftRecipe& Recipe, bool bAvailable, const UVBInventoryComponent& Inventory,
	const UVBItemDefinition* Currency)
{
	FVBCraftQuote Quote;
	Quote.bValid = Recipe.Output.Item && Recipe.Output.Count > 0
		&& !Recipe.Inputs.ContainsByPredicate([](const FVBItemAmount& Input) { return !Input.Item || Input.Count <= 0; });
	Quote.bAvailable = bAvailable;
	// 같은 재료가 두 줄이면 합쳐서 본다(교환이 합치는 것과 같다).
	TMap<const UVBItemDefinition*, int32> Needed;
	for (const FVBItemAmount& Input : Recipe.Inputs)
	{
		Needed.FindOrAdd(Input.Item.Get()) += Input.Count;
	}

	Quote.bHasInputs = true;
	for (const TPair<const UVBItemDefinition*, int32>& Need : Needed)
	{
		Quote.bHasInputs &= Need.Key && Inventory.GetCount(Need.Key) >= Need.Value;
	}

	Quote.GoldRequired = Recipe.Gold;
	Quote.GoldHeld = Currency ? Inventory.GetCount(Currency) : 0;
	Quote.bOutputFits = Recipe.Output.Item
		&& Recipe.Output.Item->ClampToMax(Inventory.GetCount(Recipe.Output.Item), Recipe.Output.Count) == Recipe.Output.Count;
	return Quote;
}

FVBEnhanceQuote UVBCraftingComponent::BuildEnhanceQuote(const FVBEnhanceTarget& Target, bool bOwned, const UVBInventoryComponent& Inventory,
	const UVBEnhancementComponent& Enhancement, const UVBItemDefinition* Currency, TArray<FVBItemAmount>& OutCosts)
{
	OutCosts.Reset();
	FVBEnhanceQuote Quote;
	Quote.bTargetValid = Target.IsValid();
	Quote.Category = VBEnhancement::CategoryOf(Target);
	Quote.bOwned = bOwned;
	Quote.CurrentLevel = Enhancement.GetLevel(Target);
	const UVBEnhancementConfig* Config = Enhancement.GetConfig();
	Quote.MaxLevel = Config ? Config->MaxLevel : 0;
	Quote.bTrackUsable = Config && Config->IsTrackUsable(Quote.Category);
	const FVBEnhanceTrack* Track = Quote.bTrackUsable ? Config->FindTrack(Quote.Category) : nullptr;
	// Steps[n] = +n 에서 +n+1 로 가는 비용. 최대면 다음 단계가 없다(판정이 MaxLevel 로 거절한다).
	if (Track && Track->Steps.IsValidIndex(Quote.CurrentLevel))
	{
		const FVBEnhanceStep& Step = Track->Steps[Quote.CurrentLevel];
		OutCosts = VBCrafting::EnhanceStepCosts(Step, Currency);
		TMap<const UVBItemDefinition*, int32> Needed;
		for (const FVBItemAmount& Material : Step.Materials)
		{
			Needed.FindOrAdd(Material.Item.Get()) += Material.Count;
		}

		Quote.bHasMaterials = true;
		for (const TPair<const UVBItemDefinition*, int32>& Need : Needed)
		{
			Quote.bHasMaterials &= Need.Key && Inventory.GetCount(Need.Key) >= Need.Value;
		}

		Quote.GoldRequired = Step.Gold;
		Quote.GoldHeld = Currency ? Inventory.GetCount(Currency) : 0;
	}

	return Quote;
}

EVBCraftResult UVBCraftingComponent::PreviewCraft(const FVBCraftingViewEntry& ViewEntry) const
{
	const FVBCraftRecipe* Recipe = View.GetRecipe(ViewEntry);
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	if (!Recipe || !Inventory)
	{
		return EVBCraftResult::Invalid;
	}

	return VBCrafting::CheckCraft(BuildCraftQuote(*Recipe, ViewEntry.bAvailable, *Inventory, UVBShopComponent::GetCurrency()));
}

EVBEnhanceResult UVBCraftingComponent::PreviewEnhance(const FVBEnhanceTarget& Target, TArray<FVBItemAmount>* OutCosts) const
{
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	const UVBEnhancementComponent* Enhancement = UVBEnhancementComponent::FindFor(GetOwner());
	if (!View.IsActive() || !View.Station->bAllowEnhancement || !Inventory || !Enhancement)
	{
		return EVBEnhanceResult::Invalid;
	}

	TArray<FVBItemAmount> Costs;
	const EVBEnhanceResult Result = VBEnhancement::Check(
		BuildEnhanceQuote(Target, IsTargetOwned(Target), *Inventory, *Enhancement, UVBShopComponent::GetCurrency(), Costs));
	if (OutCosts)
	{
		*OutCosts = MoveTemp(Costs);
	}

	return Result;
}

bool UVBCraftingComponent::CanOpenSession(const UObject* Definition) const
{
	if (!UVBShopComponent::GetCurrency())
	{
		// 돈을 모르면 비용을 판정할 수 없다. 열어 두면 모든 버튼이 꺼진 제작대가 되므로 아예 열지 않는다(상점과 같다).
		VB_LOG(Warning, "제작대 열기 거절(%s): 경제 설정에 돈 아이템이 없다", *GetNameSafe(Definition));
		return false;
	}

	return true;
}

void UVBCraftingComponent::SendSessionClosed()
{
	ClientCloseCrafting();
}

const UVBCraftingStationDefinition* UVBCraftingComponent::GetServerStation() const
{
	return Cast<UVBCraftingStationDefinition>(GetSessionDefinition());
}

void UVBCraftingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	View = FVBCraftingView();
	Super::EndPlay(EndPlayReason);
}

void UVBCraftingComponent::ServerCraft_Implementation(const UVBCraftingStationDefinition* Station, int32 RecipeIndex)
{
	if (!ValidateSessionRequest(Station, TEXT("제작")))
	{
		return;
	}

	if (!GetServerStation()->Recipes.IsValidIndex(RecipeIndex))
	{
		VB_LOG(Warning, "제작 거절: %s 에 %d 번 제작법이 없다", *GetNameSafe(GetServerStation()), RecipeIndex);
		return;
	}

	const FVBCraftRecipe& Recipe = GetServerStation()->Recipes[RecipeIndex];
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
	if (!Inventory || !Currency)
	{
		VB_LOG(Warning, "제작 거절: %s", !Inventory ? TEXT("이 플레이어의 인벤토리가 없다") : TEXT("경제 설정에 돈 아이템이 없다"));
		return;
	}
	// 화면이 켠 버튼을 믿지 않는다 - 조건, 재료, 돈, 자리를 여기서 다시 본다.
	const bool bAvailable = VBRules::EvaluateAll(Recipe.Conditions, MakeRuleContext());
	const EVBCraftResult Result = VBCrafting::CheckCraft(BuildCraftQuote(Recipe, bAvailable, *Inventory, Currency));
	if (Result != EVBCraftResult::Ok)
	{
		VB_LOG(Log, "제작 거절: %s - %s", *GetNameSafe(Recipe.Output.Item), VBCrafting::LexToString(Result));
		SendView();
		return;
	}
	// 판정이 다 통과했으니 교환도 통과해야 한다. 실패하면(획득 효과 거절 등) 아무것도 바뀌지 않았다.
	if (!Inventory->TryExchange(VBCrafting::RecipeCosts(Recipe, Currency), {Recipe.Output}))
	{
		VB_LOG(Error, "제작: 판정은 통과했는데 교환이 거절됐다(%s) - 아무것도 바뀌지 않았다", *GetNameSafe(Recipe.Output.Item));
		SendView();
		return;
	}

	VB_LOG(Log, "제작: %s x%d (%s)", *GetNameSafe(Recipe.Output.Item), Recipe.Output.Count, *GetNameSafe(GetServerStation()));
	SendView();
}

void UVBCraftingComponent::ServerEnhance_Implementation(const UVBCraftingStationDefinition* Station, const FVBEnhanceTarget& Target)
{
	if (!ValidateSessionRequest(Station, TEXT("강화")))
	{
		return;
	}

	if (!GetServerStation()->bAllowEnhancement)
	{
		VB_LOG(Warning, "강화 거절: %s 는 강화를 하지 않는 제작대다", *GetNameSafe(GetServerStation()));
		return;
	}

	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	UVBEnhancementComponent* Enhancement = UVBEnhancementComponent::FindFor(GetOwner());
	const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
	if (!Inventory || !Enhancement || !Currency)
	{
		VB_LOG(Warning, "강화 거절: %s", !Inventory ? TEXT("이 플레이어의 인벤토리가 없다")
			: !Enhancement ? TEXT("이 플레이어의 강화 컴포넌트가 없다") : TEXT("경제 설정에 돈 아이템이 없다"));
		return;
	}

	TArray<FVBItemAmount> Costs;
	const FVBEnhanceQuote Quote = BuildEnhanceQuote(Target, IsTargetOwned(Target), *Inventory, *Enhancement, Currency, Costs);
	const EVBEnhanceResult Result = VBEnhancement::Check(Quote);
	if (Result != EVBEnhanceResult::Ok)
	{
		VB_LOG(Log, "강화 거절: %s/%s - %s", *GetNameSafe(Target.Item), *UEnum::GetValueAsString(Target.Weapon),
			VBEnhancement::LexToString(Result));
		SendView();
		return;
	}

	if (!Inventory->TryExchange(Costs, {}))
	{
		VB_LOG(Error, "강화: 판정은 통과했는데 비용 교환이 거절됐다 - 단계를 올리지 않는다");
		SendView();
		return;
	}
	// 비용을 받은 뒤에만 단계를 쓴다. 판정이 쓰기 조건(대상, 갈래, 최대)을 이미 봤으므로 실패하지 않아야 한다.
	const int32 NextLevel = Quote.CurrentLevel + 1;
	if (!Enhancement->SetLevel(Target, NextLevel))
	{
		// 돌려주기도 교환이라 받은 줄마다 획득 사건이 난다("재료 n개 모으기" 류가 한 번 더 셀 수 있다). 판정을 통과한 뒤 쓰기가 실패하는 경우는
		//   없어야 하는 길이라 사건 없는 넣기를 따로 만들지 않는다 - 이 Error 가 보이면 그 자체가 고칠 결함이다.
		VB_LOG(Error, "강화: 비용을 받았는데 단계 %d 를 쓰지 못했다 - 비용을 돌려준다", NextLevel);
		if (!Inventory->TryExchange({}, Costs))
		{
			VB_LOG(Error, "강화: 비용 돌려주기도 실패했다(자리 없음 등) - 잃은 비용이 있다");
		}

		SendView();
		return;
	}

	VB_LOG(Log, "강화: %s/%s +%d -> +%d (%s)", *GetNameSafe(Target.Item), *UEnum::GetValueAsString(Target.Weapon),
		Quote.CurrentLevel, NextLevel, *GetNameSafe(GetServerStation()));
	SendView();
}

void UVBCraftingComponent::ServerClose_Implementation()
{
	CloseStation();
}

void UVBCraftingComponent::ClientShowCrafting_Implementation(const FVBCraftingView& InView)
{
	View = InView;
	if (!View.IsActive())
	{
		VB_LOG(Warning, "제작대 표시: 빈 보기를 받았다 - 빈 화면이 된다");
	}
	// 화면을 여는 것은 이 컴포넌트가 아니라 PC 다(AVBPlayerController 가 이 알림을 듣는다).
	OnViewChanged.Broadcast();
}

void UVBCraftingComponent::ClientCloseCrafting_Implementation()
{
	View = FVBCraftingView();
	OnViewChanged.Broadcast();
}

void UVBCraftingComponent::SendView()
{
	if (!GetServerStation())
	{
		return;
	}

	const FVBRuleContext Context = MakeRuleContext();
	FVBCraftingView NewView;
	NewView.Station = GetServerStation();
	for (int32 Index = 0; Index < GetServerStation()->Recipes.Num(); ++Index)
	{
		const FVBCraftRecipe& Recipe = GetServerStation()->Recipes[Index];
		if (!Recipe.Output.Item)
		{
			continue;
		}

		const bool bAvailable = VBRules::EvaluateAll(Recipe.Conditions, Context);
		if (!bAvailable && !Recipe.bShowWhenUnavailable)
		{
			continue;
		}

		FVBCraftingViewEntry& ViewEntry = NewView.Entries.AddDefaulted_GetRef();
		ViewEntry.RecipeIndex = Index;
		ViewEntry.bAvailable = bAvailable;
	}

	ClientShowCrafting(NewView);
}

