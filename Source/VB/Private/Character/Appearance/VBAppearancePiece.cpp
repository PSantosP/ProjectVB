// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/Appearance/VBAppearancePiece.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/SkeletalMesh.h"
#include "Inventory/VBItemDefinition.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAppearancePiece"

TSoftObjectPtr<USkeletalMesh> UVBAppearancePiece::FindFittedMesh(FName CharacterId) const
{
	const TSoftObjectPtr<USkeletalMesh>* Found = FittedMeshes.Find(CharacterId);
	return Found ? *Found : TSoftObjectPtr<USkeletalMesh>();
}

#if WITH_EDITOR
EDataValidationResult UVBAppearancePiece::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const auto Fail = [&](const FText& Problem)
	{
		Context.AddError(Problem);
		Result = EDataValidationResult::Invalid;
	};

	if (!AppearanceSlot.MatchesTag(VBGameplayTags::Appearance_Slot) || AppearanceSlot == VBGameplayTags::Appearance_Slot)
	{
		Fail(LOCTEXT("NoSlot", "AppearanceSlot 이 Appearance.Slot 아래 칸 하나가 아니다."));
	}

	if (FittedMeshes.IsEmpty())
	{
		Fail(LOCTEXT("NoMeshes", "FittedMeshes 가 비었다 - 어느 캐릭터도 이 조각을 입을 수 없다."));
	}

	for (const TPair<FName, TSoftObjectPtr<USkeletalMesh>>& Pair : FittedMeshes)
	{
		// V1(조각 쪽): 경로가 있는 것만으로는 부족하다 - 실제로 읽혀야 한다.
		if (Pair.Key.IsNone() || Pair.Value.IsNull() || !Pair.Value.LoadSynchronous())
		{
			Fail(FText::Format(LOCTEXT("BadMesh", "캐릭터 '{0}' 의 메시가 비었거나 읽히지 않는다."), FText::FromName(Pair.Key)));
		}
	}

	for (const FGameplayTag& Section : HidesSections)
	{
		if (!Section.MatchesTag(VBGameplayTags::Appearance_Section) || Section == VBGameplayTags::Appearance_Section)
		{
			Fail(FText::Format(LOCTEXT("BadSection", "HidesSections 의 {0} 은 Appearance.Section 아래 구역이 아니다."),
					FText::FromString(Section.ToString())));
		}
	}

	return Result;
}
#endif

namespace VBAppearancePieceRules
{
	FPieceProjection ProjectEquippedPieces(const TArray<FEquippedPiece>& Equipped)
	{
		FPieceProjection Result;
		TSet<FGameplayTag> Seen;
		for (const FEquippedPiece& Entry : Equipped)
		{
			if (!Entry.Item || !Entry.Item->FindFragment<FVBItemFragment_Appearance>())
			{
				continue;
			}

			const UVBAppearancePiece* Piece = Entry.Piece;
			if (!Piece || !Piece->AppearanceSlot.MatchesTag(VBGameplayTags::Appearance_Slot)
				|| Piece->AppearanceSlot == VBGameplayTags::Appearance_Slot)
			{
				Result.Errors.Add(FText::FromString(FString::Printf(
					TEXT("Equipment item %s has an unresolved piece or invalid appearance slot."),
					*GetNameSafe(Entry.Item))));
				continue;
			}

			if (Seen.Contains(Piece->AppearanceSlot))
			{
				Result.Errors.Add(FText::FromString(FString::Printf(
					TEXT("Equipment repeats appearance slot %s; the first equipment slot wins (invalid V7 data)."),
					*Piece->AppearanceSlot.ToString())));
				continue;
			}

			Seen.Add(Piece->AppearanceSlot);
			Result.Pieces.Add(Piece);
		}

		return Result;
	}

	FPieceChoice ChoosePieces(FName CharacterId, const TArray<const UVBAppearancePiece*>& Defaults,
		const TArray<const UVBAppearancePiece*>& Equipped)
	{
		FPieceChoice Result;
		if (CharacterId.IsNone())
		{
			return Result;
		}

		TArray<FGameplayTag> Order;
		TMap<FGameplayTag, const UVBAppearancePiece*> DefaultBySlot, EquippedBySlot;
		const auto Index = [&](const TArray<const UVBAppearancePiece*>& Input, TMap<FGameplayTag, const UVBAppearancePiece*>& BySlot)
		{
			for (const UVBAppearancePiece* Piece : Input)
			{
				if (!Piece || !Piece->AppearanceSlot.MatchesTag(VBGameplayTags::Appearance_Slot)
					|| Piece->AppearanceSlot == VBGameplayTags::Appearance_Slot)
				{
					Result.Errors.Add(FText::FromString(TEXT("Appearance choice contains an unresolved piece or invalid slot.")));
					continue;
				}

				if (BySlot.Contains(Piece->AppearanceSlot))
				{
					Result.Errors.Add(FText::FromString(FString::Printf(TEXT("Appearance choice repeats slot %s; the first piece wins."),
						*Piece->AppearanceSlot.ToString())));
					continue;
				}

				BySlot.Add(Piece->AppearanceSlot, Piece);
				Order.AddUnique(Piece->AppearanceSlot);
			}
		};
		Index(Defaults, DefaultBySlot);
		Index(Equipped, EquippedBySlot);
		for (const FGameplayTag& Slot : Order)
		{
			const UVBAppearancePiece* const* Default = DefaultBySlot.Find(Slot);
			const UVBAppearancePiece* const* Worn = EquippedBySlot.Find(Slot);
			if (Worn && !(*Worn)->FindFittedMesh(CharacterId).IsNull())
			{
				Result.Pieces.Add(*Worn);
			}
			else
			{
				if (Worn)
				{
					Result.Warnings.Add(FText::FromString(FString::Printf(
						TEXT("Piece %s has no fitted mesh for %s; using the default for %s if available."),
					*GetNameSafe(*Worn), *CharacterId.ToString(), *Slot.ToString())));
				}

				if (Default)
				{
					if (!(*Default)->FindFittedMesh(CharacterId).IsNull())
					{
						Result.Pieces.Add(*Default);
					}
					else
					{
						Result.Errors.Add(FText::FromString(FString::Printf(TEXT("Default piece %s has no fitted mesh for %s."),
							*GetNameSafe(*Default), *CharacterId.ToString())));
					}
				}
			}
		}

		return Result;
	}

	const TArray<FName>& BodySectionNames()
	{
		static const TArray<FName> Names = []
		{
			TArray<FName> Out;
			for (const FGameplayTag& Tag : {VBGameplayTags::Appearance_Section_Head.GetTag(),
				VBGameplayTags::Appearance_Section_Hair.GetTag(),
				VBGameplayTags::Appearance_Section_Hands.GetTag(), VBGameplayTags::Appearance_Section_TorsoUpperArms.GetTag(),
				VBGameplayTags::Appearance_Section_Forearms.GetTag(), VBGameplayTags::Appearance_Section_HipsLegs.GetTag(),
				VBGameplayTags::Appearance_Section_Feet.GetTag()})
			{
				Out.Add(Tag.GetTagLeafName());
			}

			return Out;
		}();
		return Names;
	}

	int32 EffectiveMaterialIndex(int32 SectionMaterialIndex, const TArray<int32>& LODMaterialMap, int32 SectionIndex)
	{
		return LODMaterialMap.IsValidIndex(SectionIndex) && LODMaterialMap[SectionIndex] != INDEX_NONE
			? LODMaterialMap[SectionIndex] : SectionMaterialIndex;
	}

	TArray<TArray<FName>> SectionSlotNamesPerLod(const USkeletalMesh& Mesh)
	{
		TArray<TArray<FName>> PerLod;
		const FSkeletalMeshRenderData* RenderData = Mesh.GetResourceForRendering();
		if (!RenderData)
		{
			return PerLod;
		}

		const TArray<FSkeletalMaterial>& Materials = Mesh.GetMaterials();
		for (int32 LOD = 0; LOD < RenderData->LODRenderData.Num(); ++LOD)
		{
			const FSkeletalMeshLODInfo* LODInfo = Mesh.GetLODInfo(LOD);
			const TArray<int32> NoMap;
			const TArray<int32>& Map = LODInfo ? LODInfo->LODMaterialMap : NoMap;
			TArray<FName>& Names = PerLod.AddDefaulted_GetRef();
			const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[LOD].RenderSections;
			for (int32 Section = 0; Section < Sections.Num(); ++Section)
			{
				const int32 Material = EffectiveMaterialIndex(Sections[Section].MaterialIndex, Map, Section);
				Names.Add(Materials.IsValidIndex(Material) ? Materials[Material].MaterialSlotName : NAME_None);
			}
		}

		return PerLod;
	}

	TArray<FText> FindBodySectionProblems(const TArray<TArray<FName>>& NamesPerLod)
	{
		TArray<FText> Problems;
		const TArray<FName>& Required = BodySectionNames();
		if (NamesPerLod.IsEmpty())
		{
			Problems.Add(LOCTEXT("NoLod", "몸 메시에 LOD 가 없다(렌더 데이터 없음)."));
			return Problems;
		}

		for (const FName& Name : Required)
		{
			if (!NamesPerLod[0].Contains(Name))
			{
				Problems.Add(FText::Format(LOCTEXT("MissingSection", "LOD0 에 몸 구역 '{0}' 의 섹션이 없다."), FText::FromName(Name)));
			}
		}

		for (int32 LOD = 0; LOD < NamesPerLod.Num(); ++LOD)
		{
			TSet<FName> Seen;
			for (const FName& Name : NamesPerLod[LOD])
			{
				if (!Required.Contains(Name))
				{
					Problems.Add(FText::Format(LOCTEXT("UnknownSection", "LOD{0} 의 섹션이 일곱 구역 밖의 슬롯 '{1}' 을 쓴다."), LOD,
						FText::FromName(Name)));
					continue;
				}

				bool bAlreadySeen = false;
				Seen.Add(Name, &bAlreadySeen);
				if (bAlreadySeen)
				{
					Problems.Add(FText::Format(LOCTEXT("MergedSection", "LOD{0} 에서 섹션 둘이 같은 구역 '{1}' 을 쓴다 - 따로 숨길 수 없다."), LOD,
						FText::FromName(Name)));
				}
			}
		}

		return Problems;
	}

	TArray<TArray<int32>> SectionsToHide(const TArray<TArray<FName>>& NamesPerLod, const TSet<FName>& HiddenNames)
	{
		TArray<TArray<int32>> PerLod;
		for (const TArray<FName>& Names : NamesPerLod)
		{
			TArray<int32>& Hide = PerLod.AddDefaulted_GetRef();
			for (int32 Section = 0; Section < Names.Num(); ++Section)
			{
				if (HiddenNames.Contains(Names[Section]))
				{
					Hide.Add(Section);
				}
			}
		}

		return PerLod;
	}

	TArray<FText> FindDefaultPieceProblems(const TArray<FPieceFacts>& Pieces)
	{
		TArray<FText> Problems;
		TMap<FGameplayTag, FString> BySlot;
		for (const FPieceFacts& Piece : Pieces)
		{
			if (!Piece.Slot.IsValid())
			{
				Problems.Add(FText::Format(LOCTEXT("PieceNoSlot", "기본 조각 '{0}' 에 칸이 없다."), FText::FromString(Piece.Name)));
				continue;
			}

			if (const FString* Other = BySlot.Find(Piece.Slot))
			{
				Problems.Add(FText::Format(LOCTEXT("SameSlot", "기본 조각 '{0}' 과 '{1}' 이 같은 칸 {2} 을 차지한다."),
					FText::FromString(*Other), FText::FromString(Piece.Name), FText::FromString(Piece.Slot.ToString())));
				continue;
			}

			BySlot.Add(Piece.Slot, Piece.Name);

			const auto RequireHide = [&](const FGameplayTag& Section)
			{
				if (!Piece.Hides.HasTagExact(Section))
				{
					Problems.Add(FText::Format(LOCTEXT("MustHide", "조각 '{0}'({1}) 은 {2} 를 숨겨야 한다."),
						FText::FromString(Piece.Name), FText::FromString(Piece.Slot.ToString()), FText::FromString(Section.ToString())));
				}
			};
			if (Piece.Slot == VBGameplayTags::Appearance_Slot_Top)
			{
				RequireHide(VBGameplayTags::Appearance_Section_TorsoUpperArms);
			}
			else if (Piece.Slot == VBGameplayTags::Appearance_Slot_Bottom)
			{
				RequireHide(VBGameplayTags::Appearance_Section_HipsLegs);
				RequireHide(VBGameplayTags::Appearance_Section_Feet);
			}
		}

		for (const FGameplayTag& Required : {VBGameplayTags::Appearance_Slot_Top.GetTag(), VBGameplayTags::Appearance_Slot_Bottom.GetTag()})
		{
			if (!BySlot.Contains(Required))
			{
				Problems.Add(FText::Format(LOCTEXT("NeedSlot", "칸 있는 캐릭터는 기본 {0} 조각이 있어야 한다(맨몸 메시는 목 아래가 비었다)."),
					FText::FromString(Required.ToString())));
			}
		}

		return Problems;
	}

	FText FindUnwearableReason(bool bHasLoadedLook, bool bLookRequested, FName CharacterId, int32 EquippedCount)
	{
		if (EquippedCount <= 0)
		{
			return FText::GetEmpty();
		}

		if (!bHasLoadedLook)
		{
			return bLookRequested ? FText::GetEmpty() : LOCTEXT("DriverBodyNoSlots", "본체에는 조각 칸이 없다(칸 있는 겉모습으로 바꿔야 보인다)");
		}

		return CharacterId.IsNone() ? LOCTEXT("LookNoCharacterId", "이 겉모습은 CharacterId 가 비어 조각 칸이 없다") : FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
