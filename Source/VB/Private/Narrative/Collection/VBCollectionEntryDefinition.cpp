#include "Narrative/Collection/VBCollectionEntryDefinition.h"

#include "Engine/AssetManager.h"
#include "Engine/Texture2D.h"
#include "GameplayTagsManager.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryState.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetType UVBCollectionEntryDefinition::GetCollectionAssetType()
{
	static const FPrimaryAssetType Type(TEXT("VBCollection"));
	return Type;
}

FPrimaryAssetId UVBCollectionEntryDefinition::MakeCollectionAssetId(FGameplayTag Fact)
{
	return FPrimaryAssetId(GetCollectionAssetType(), Fact.GetTagName());
}

const UVBCollectionEntryDefinition* UVBCollectionEntryDefinition::FindById(FGameplayTag Fact)
{
	return Cast<UVBCollectionEntryDefinition>(UAssetManager::Get().GetPrimaryAssetPath(MakeCollectionAssetId(Fact)).TryLoad());
}

FPrimaryAssetId UVBCollectionEntryDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetCollectionAssetType(), DiscoveryFact.IsValid() ? DiscoveryFact.GetTagName() : GetFName());
}

FGameplayTag VBCollection::GetCategory(FGameplayTag Fact)
{
	static const FName Categories[] = {
		TEXT("Story.Collection.Document"), TEXT("Story.Collection.Collectible"), TEXT("Story.Collection.Enemy"),
		TEXT("Story.Collection.Person"), TEXT("Story.Collection.Area") };
	for (const FName CategoryName : Categories)
	{
		const FGameplayTag Category = FGameplayTag::RequestGameplayTag(CategoryName, false);
		if (Category.IsValid() && Fact != Category && Fact.MatchesTag(Category))
		{
			return Category;
		}
	}

	return {};
}

bool VBCollection::IsEntryFact(FGameplayTag Fact)
{
	return GetCategory(Fact).IsValid() && UGameplayTagsManager::Get().RequestGameplayTagChildren(Fact).IsEmpty();
}

FGameplayTag VBCollection::GetReadingFact(const UVBItemDefinition* Item)
{
	const FVBItemFragment_AcquireEffects* Acquire = Item ? Item->FindFragment<FVBItemFragment_AcquireEffects>() : nullptr;
	FGameplayTag Result;
	if (Acquire)
	{
		for (const FInstancedStruct& Effect : Acquire->Effects)
		{
			const FVBEffect_SetFact* Set = Effect.GetPtr<FVBEffect_SetFact>();
			if (!Set || !Set->bOn || !IsEntryFact(Set->Fact))
			{
				continue;
			}

			const FName Category = GetCategory(Set->Fact).GetTagName();
			if (Category != TEXT("Story.Collection.Document") && Category != TEXT("Story.Collection.Collectible"))
			{
				continue;
			}

			if (Result.IsValid() && Result != Set->Fact) { return {}; }
			Result = Set->Fact;
		}
	}

	return Result;
}

TArray<const UVBCollectionEntryDefinition*> VBCollection::BuildRows(
	const TArray<const UVBCollectionEntryDefinition*>& Definitions, const FVBStoryState& Story, FGameplayTag Filter)
{
	TArray<const UVBCollectionEntryDefinition*> Rows;
	TSet<FGameplayTag> Included;
	for (const UVBCollectionEntryDefinition* Definition : Definitions)
	{
		if (Definition && IsEntryFact(Definition->DiscoveryFact) && Story.HasFact(Definition->DiscoveryFact)
			&& (!Filter.IsValid() || Definition->DiscoveryFact.MatchesTag(Filter)) && !Included.Contains(Definition->DiscoveryFact))
		{
			Rows.Add(Definition);
			Included.Add(Definition->DiscoveryFact);
		}
	}

	Rows.Sort([](const UVBCollectionEntryDefinition& A, const UVBCollectionEntryDefinition& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder
			: A.DiscoveryFact.GetTagName().LexicalLess(B.DiscoveryFact.GetTagName());
	});
	return Rows;
}

#if WITH_EDITOR
bool VBCollection::ValidateItemReferences(const UVBItemDefinition& Item, FDataValidationContext& Context)
{
	const FVBItemFragment_AcquireEffects* Acquire = Item.FindFragment<FVBItemFragment_AcquireEffects>();
	if (!Acquire)
	{
		return true;
	}

	bool bValid = true;
	TSet<FGameplayTag> ReadingFacts;
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Story.Collection"), false);
	for (const FInstancedStruct& Effect : Acquire->Effects)
	{
		const FVBEffect_SetFact* Set = Effect.GetPtr<FVBEffect_SetFact>();
		if (!Set || !Root.IsValid() || !Set->Fact.MatchesTag(Root))
		{
			continue;
		}

		if (!Set->bOn || !IsEntryFact(Set->Fact)
			|| !UAssetManager::Get().GetPrimaryAssetPath(UVBCollectionEntryDefinition::MakeCollectionAssetId(Set->Fact)).IsValid())
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("%s: invalid/missing collection entry or revoking discovery (%s)"),
				*GetNameSafe(&Item), *Set->Fact.ToString())));
			bValid = false;
		}

		const FName Category = GetCategory(Set->Fact).GetTagName();
		if (Category == TEXT("Story.Collection.Document") || Category == TEXT("Story.Collection.Collectible"))
		{
			ReadingFacts.Add(Set->Fact);
		}
	}

	if (ReadingFacts.Num() > 1)
	{
		Context.AddError(FText::FromString(FString::Printf(TEXT("%s: multiple readable collection entries"), *GetNameSafe(&Item))));
		bValid = false;
	}

	return bValid;
}

EDataValidationResult UVBCollectionEntryDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Error = [&](const TCHAR* Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};
	if (!VBCollection::IsEntryFact(DiscoveryFact))
	{
		Error(TEXT("DiscoveryFact must be a leaf in one of the five collection categories."));
	}

	if (Title.IsEmpty() || Body.IsEmpty())
	{
		Error(TEXT("Collection Title and Body must not be empty."));
	}

	UAssetManager& Manager = UAssetManager::Get();
	const FSoftObjectPath Registered = Manager.GetPrimaryAssetPath(GetPrimaryAssetId());
	if (!Registered.IsValid())
	{
		Context.AddWarning(FText::FromString(TEXT("Collection AssetManager scan is missing; pairing/ID validation is incomplete.")));
	}
	else if (!HasAnyFlags(RF_Transient) && Registered != FSoftObjectPath(this))
	{
		Error(TEXT("Another registered collection asset has this DiscoveryFact."));
	}

	for (const TSoftObjectPtr<UTexture2D>* Texture : { &Icon, &Illustration })
	{
		if (!Texture->IsNull() && !Texture->LoadSynchronous()) { Error(TEXT("Collection soft texture cannot be resolved.")); }
	}

	TArray<FPrimaryAssetId> Items;
	Manager.GetPrimaryAssetIdList(UVBItemDefinition::GetItemAssetType(), Items);
	for (const FPrimaryAssetId& Id : Items)
	{
		const UVBItemDefinition* Item = Cast<UVBItemDefinition>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		if (!Item || !VBCollection::ValidateItemReferences(*Item, Context))
		{
			if (!Item) { Error(TEXT("A registered item cannot be resolved during collection validation.")); }
			Result = EDataValidationResult::Invalid;
		}
	}

	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif
