// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Crafting/VBCraftingScreenWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Character/VBWeaponStateComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Crafting/VBCraftingStationDefinition.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "UI/VBScreenText.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBCraftingScreen"

UVBCraftingScreenWidget::UVBCraftingScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CostLineFormat = LOCTEXT("CostLine", "{Name} {Held}/{Need}");
	LevelFormat = LOCTEXT("Level", "+{0}");
	NextLevelFormat = LOCTEXT("NextLevel", "+{Current} -> +{Next} (x{Scale})");
	OutputFormat = LOCTEXT("Output", "{Name} x{Count}");
	CraftActionText = LOCTEXT("CraftAction", "제작");
	EnhanceActionText = LOCTEXT("EnhanceAction", "강화");
	CraftResultTexts.Add(EVBCraftResult::Locked, LOCTEXT("Locked", "아직 만들 수 없다"));
	CraftResultTexts.Add(EVBCraftResult::MissingInputs, LOCTEXT("MissingInputs", "재료가 모자라다"));
	CraftResultTexts.Add(EVBCraftResult::NotEnoughGold, VBScreenText::NotEnoughCurrency());
	CraftResultTexts.Add(EVBCraftResult::NoRoom, VBScreenText::NoRoom());
	EnhanceResultTexts.Add(EVBEnhanceResult::NotEnhanceable, LOCTEXT("NotEnhanceable", "강화할 수 없는 물건이다"));
	EnhanceResultTexts.Add(EVBEnhanceResult::NotOwned, LOCTEXT("NotOwned", "가지고 있지 않다"));
	EnhanceResultTexts.Add(EVBEnhanceResult::MaxLevel, LOCTEXT("MaxLevel", "더 강화할 수 없다"));
	EnhanceResultTexts.Add(EVBEnhanceResult::NoTrack, LOCTEXT("NoTrack", "강화 수치가 준비되지 않았다"));
	EnhanceResultTexts.Add(EVBEnhanceResult::MissingMaterials, LOCTEXT("MissingMaterials", "재료가 모자라다"));
	EnhanceResultTexts.Add(EVBEnhanceResult::NotEnoughGold, VBScreenText::NotEnoughCurrency());
}

void UVBCraftingScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnAction)
	{
		BtnAction->OnClicked.AddUniqueDynamic(this, &UVBCraftingScreenWidget::HandleActionClicked);
	}

	if (BtnTabCraft)
	{
		BtnTabCraft->OnClicked.AddUniqueDynamic(this, &UVBCraftingScreenWidget::HandleCraftTabClicked);
	}

	if (BtnTabEnhance)
	{
		BtnTabEnhance->OnClicked.AddUniqueDynamic(this, &UVBCraftingScreenWidget::HandleEnhanceTabClicked);
	}
}

void UVBCraftingScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Tab = ETab::Craft;
	SelectedRecipeIndex = INDEX_NONE;
	SelectedTarget = FVBEnhanceTarget();
	Rebuild();
	// 인벤토리, 강화 단계는 PlayerState 에 있다. 원격 클라는 늦게 올 수 있다.
	if (AVBPlayerController* VBPC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PlayerStateSet.Register(*VBPC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBCraftingScreenWidget::BindPlayerState));
	}
	else
	{
		VB_LOG(Warning, "제작 화면: 주인이 AVBPlayerController 가 아니다 - PlayerState 를 기다릴 수 없다");
	}
}

void UVBCraftingScreenWidget::NativeOnDeactivated()
{
	PlayerStateSet.Unregister();
	Inventory.Reset();
	Enhancement.Reset();
	Super::NativeOnDeactivated();
}

UVBStationSessionComponent* UVBCraftingScreenWidget::FindSession() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? PC->FindComponentByClass<UVBCraftingComponent>() : nullptr;
}

void UVBCraftingScreenWidget::BindPlayerState(APlayerState* /*PlayerState*/)
{
	// 재료, 돈, 가진 장비가 바뀌면 목록(강화 대상)과 버튼이 바뀐다. 단계가 바뀌면 줄의 +N 과 비용이 바뀐다.
	Inventory.Bind(UVBInventoryComponent::FindFor(GetOwningPlayer()), &UVBInventoryComponent::OnInventoryChanged,
		this, &UVBCraftingScreenWidget::Rebuild);
	Enhancement.Bind(UVBEnhancementComponent::FindFor(GetOwningPlayer()), &UVBEnhancementComponent::OnLevelChanged,
		this, &UVBCraftingScreenWidget::HandleLevelChanged);
	Rebuild();
}

void UVBCraftingScreenWidget::HandleLevelChanged(const FVBEnhanceTarget& /*Target*/)
{
	Rebuild();
}

void UVBCraftingScreenWidget::HandleCraftTabClicked()
{
	SetTab(ETab::Craft);
}

void UVBCraftingScreenWidget::HandleEnhanceTabClicked()
{
	SetTab(ETab::Enhance);
}

void UVBCraftingScreenWidget::SetTab(ETab NewTab)
{
	if (Tab != NewTab)
	{
		Tab = NewTab;
		Rebuild();
	}
}

int32 UVBCraftingScreenWidget::BuildRows()
{
	const UVBCraftingComponent* Owned = GetSession<UVBCraftingComponent>();
	const FVBCraftingView* View = Owned ? &Owned->GetView() : nullptr;
	const bool bAllowEnhancement = View && View->Station && View->Station->bAllowEnhancement;
	// 강화를 하지 않는 제작대면 강화 탭을 숨기고 제작 탭으로 돌아간다.
	if (!bAllowEnhancement && Tab == ETab::Enhance)
	{
		Tab = ETab::Craft;
	}

	if (BtnTabEnhance)
	{
		BtnTabEnhance->SetVisibility(bAllowEnhancement ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (TxtStationName)
	{
		TxtStationName->SetText(View && View->Station ? View->Station->DisplayName : FText::GetEmpty());
	}

	if (TxtAction)
	{
		TxtAction->SetText(Tab == ETab::Craft ? CraftActionText : EnhanceActionText);
	}

	if (Tab == ETab::Craft)
	{
		RebuildCraftRows();
		return RowRecipeIndices.Num();
	}

	RebuildEnhanceRows();
	return RowTargets.Num();
}

void UVBCraftingScreenWidget::RebuildCraftRows()
{
	RowRecipeIndices.Reset();
	const UVBCraftingComponent* Owned = GetSession<UVBCraftingComponent>();
	const FVBCraftingView* View = Owned ? &Owned->GetView() : nullptr;
	const int32 EntryCount = View ? View->Entries.Num() : 0;
	for (int32 Index = 0; Index < EntryCount; ++Index)
	{
		const FVBCraftingViewEntry& ViewEntry = View->Entries[Index];
		const FVBCraftRecipe* Recipe = View->GetRecipe(ViewEntry);
		if (!Recipe || !Recipe->Output.Item)
		{
			continue;
		}

		AddRow(RowRecipeIndices.Add(ViewEntry.RecipeIndex), Recipe->Output.Item->DisplayName,
			Recipe->Output.Count > 1 ? FText::Format(LOCTEXT("RowCount", "x{0}"), Recipe->Output.Count) : FText::GetEmpty());
	}

	KeepSelectionOrFirst(RowRecipeIndices, SelectedRecipeIndex, INDEX_NONE);
}

void UVBCraftingScreenWidget::RebuildEnhanceRows()
{
	RowTargets.Reset();
	const UVBInventoryComponent* OwnedInventory = Inventory.Get();
	const UVBEnhancementComponent* OwnedEnhancement = Enhancement.Get();
	// 무기 = 폰이 가진 무기(캐릭터마다 하나), 장비 = 인벤토리에서 갈래가 있는 것.
	const APawn* Pawn = GetOwningPlayerPawn();
	if (const UVBWeaponStateComponent* Weapons = Pawn ? Pawn->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
	{
		for (const EVBWeaponType Weapon : Weapons->GetOwnedWeapons())
		{
			RowTargets.Add(FVBEnhanceTarget::ForWeapon(Weapon));
		}
	}

	if (OwnedInventory)
	{
		for (const FVBInventoryEntry& Entry : OwnedInventory->GetEntries())
		{
			const FVBEnhanceTarget Target = FVBEnhanceTarget::ForItem(Entry.Definition);
			if (Entry.Definition && VBEnhancement::CategoryOf(Target) != EVBEnhanceCategory::None)
			{
				RowTargets.Add(Target);
			}
		}
	}

	for (int32 RowIndex = 0; RowIndex < RowTargets.Num(); ++RowIndex)
	{
		const FVBEnhanceTarget& Target = RowTargets[RowIndex];
		const FText Label = Target.IsWeapon() ? UEnum::GetDisplayValueAsText(Target.Weapon) : Target.Item->DisplayName;
		const int32 Level = OwnedEnhancement ? OwnedEnhancement->GetLevel(Target) : 0;
		AddRow(RowIndex, Label, FText::Format(LevelFormat, Level));
	}

	KeepSelectionOrFirst(RowTargets, SelectedTarget, FVBEnhanceTarget());
}

void UVBCraftingScreenWidget::Select(int32 RowIndex)
{
	if (Tab == ETab::Craft && RowRecipeIndices.IsValidIndex(RowIndex))
	{
		SelectedRecipeIndex = RowRecipeIndices[RowIndex];
	}
	else if (Tab == ETab::Enhance && RowTargets.IsValidIndex(RowIndex))
	{
		SelectedTarget = RowTargets[RowIndex];
	}

	RefreshDetail();
}

FText UVBCraftingScreenWidget::FormatCosts(const TArray<FVBItemAmount>& Costs) const
{
	const UVBInventoryComponent* OwnedInventory = Inventory.Get();
	TArray<FText> Lines;
	for (const FVBItemAmount& Cost : Costs)
	{
		if (!Cost.Item)
		{
			continue;
		}

		Lines.Add(FText::FormatNamed(CostLineFormat, TEXT("Name"), Cost.Item->DisplayName,
			TEXT("Held"), OwnedInventory ? OwnedInventory->GetCount(Cost.Item) : 0, TEXT("Need"), Cost.Count));
	}

	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

void UVBCraftingScreenWidget::RefreshDetail()
{
	RefreshCurrency(Inventory.Get());
	if (Tab == ETab::Craft)
	{
		RefreshCraftDetail();
	}
	else
	{
		RefreshEnhanceDetail();
	}
}

void UVBCraftingScreenWidget::RefreshCraftDetail()
{
	const UVBCraftingComponent* Owned = GetSession<UVBCraftingComponent>();
	const FVBCraftingView* View = Owned ? &Owned->GetView() : nullptr;
	const int32 Selected = SelectedRecipeIndex;
	const FVBCraftingViewEntry* ViewEntry = View && Selected != INDEX_NONE
		? View->Entries.FindByPredicate([Selected](const FVBCraftingViewEntry& E) { return E.RecipeIndex == Selected; })
		: nullptr;
	const FVBCraftRecipe* Recipe = ViewEntry ? View->GetRecipe(*ViewEntry) : nullptr;
	const UVBItemDefinition* Item = Recipe ? Recipe->Output.Item.Get() : nullptr;
	if (TxtItemName)
	{
		TxtItemName->SetText(Item ? FText::FormatNamed(OutputFormat, TEXT("Name"), Item->DisplayName, TEXT("Count"), Recipe->Output.Count)
			: FText::GetEmpty());
	}

	if (TxtItemDescription)
	{
		TxtItemDescription->SetText(Item ? Item->Description : FText::GetEmpty());
	}

	if (TxtCost)
	{
		TxtCost->SetText(Recipe ? FormatCosts(VBCrafting::RecipeCosts(*Recipe, UVBShopComponent::GetCurrency())) : FText::GetEmpty());
	}
	// 버튼은 서버와 같은 판정으로 켠다(표시용). 고른 줄이 없으면 꺼진다.
	const EVBCraftResult Result = Owned && ViewEntry ? Owned->PreviewCraft(*ViewEntry) : EVBCraftResult::Invalid;
	if (BtnAction)
	{
		BtnAction->SetIsEnabled(Result == EVBCraftResult::Ok);
	}

	if (TxtStatus)
	{
		TxtStatus->SetText(VBScreenText::ResultReason(CraftResultTexts, Result, ViewEntry != nullptr));
	}
}

void UVBCraftingScreenWidget::RefreshEnhanceDetail()
{
	const UVBCraftingComponent* Owned = GetSession<UVBCraftingComponent>();
	const UVBEnhancementComponent* OwnedEnhancement = Enhancement.Get();
	const bool bHasTarget = SelectedTarget.IsValid();
	if (TxtItemName)
	{
		TxtItemName->SetText(!bHasTarget ? FText::GetEmpty()
			: SelectedTarget.IsWeapon() ? UEnum::GetDisplayValueAsText(SelectedTarget.Weapon) : SelectedTarget.Item->DisplayName);
	}

	TArray<FVBItemAmount> Costs;
	const EVBEnhanceResult Result = Owned && bHasTarget ? Owned->PreviewEnhance(SelectedTarget, &Costs) : EVBEnhanceResult::Invalid;
	if (TxtItemDescription)
	{
		FText Detail;
		if (bHasTarget && OwnedEnhancement)
		{
			const int32 Level = OwnedEnhancement->GetLevel(SelectedTarget);
			const UVBEnhancementConfig* Config = OwnedEnhancement->GetConfig();
			const FVBEnhanceTrack* Track = Config ? Config->FindTrack(VBEnhancement::CategoryOf(SelectedTarget)) : nullptr;
			const float NextScale = VBEnhancement::ScaleAt(Track, Level + 1);
			FNumberFormattingOptions TwoDecimals;
			TwoDecimals.MinimumFractionalDigits = 2;
			TwoDecimals.MaximumFractionalDigits = 2;
			Detail = Result == EVBEnhanceResult::MaxLevel
				? FText::Format(LevelFormat, Level)
				: FText::FormatNamed(NextLevelFormat, TEXT("Current"), Level, TEXT("Next"), Level + 1,
					TEXT("Scale"), FText::AsNumber(NextScale, &TwoDecimals));
		}

		TxtItemDescription->SetText(Detail);
	}

	if (TxtCost)
	{
		TxtCost->SetText(FormatCosts(Costs));
	}

	if (BtnAction)
	{
		BtnAction->SetIsEnabled(Result == EVBEnhanceResult::Ok);
	}

	if (TxtStatus)
	{
		TxtStatus->SetText(VBScreenText::ResultReason(EnhanceResultTexts, Result, bHasTarget));
	}
}

void UVBCraftingScreenWidget::HandleActionClicked()
{
	UVBCraftingComponent* Owned = GetSession<UVBCraftingComponent>();
	if (!Owned)
	{
		return;
	}

	if (Tab == ETab::Craft && SelectedRecipeIndex != INDEX_NONE)
	{
		Owned->RequestCraft(SelectedRecipeIndex);
	}
	else if (Tab == ETab::Enhance && SelectedTarget.IsValid())
	{
		Owned->RequestEnhance(SelectedTarget);
	}
}

#undef LOCTEXT_NAMESPACE
