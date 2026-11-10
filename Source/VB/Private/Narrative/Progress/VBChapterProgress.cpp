// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Progress/VBChapterProgress.h"

#include "Narrative/Progress/VBChapterDefinition.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "Narrative/Story/VBStoryState.h"

namespace VBChapterProgress
{
	const UVBChapterDefinition* ResolveCurrent(TConstArrayView<TObjectPtr<UVBChapterDefinition>> Chapters, const FVBStoryState& Story)
	{
		const UVBChapterDefinition* Best = nullptr;
		for (const TObjectPtr<UVBChapterDefinition>& Chapter : Chapters)
		{
			if (!Chapter || !Chapter->ReachedFact.IsValid() || !Story.HasFact(Chapter->ReachedFact))
			{
				continue;
			}

			if (!Best || Chapter->SortOrder > Best->SortOrder
				|| (Chapter->SortOrder == Best->SortOrder && Best->ChapterId.GetTagName().LexicalLess(Chapter->ChapterId.GetTagName())))
			{
				Best = Chapter;
			}
		}

		return Best;
	}

	bool IsRegionUnlocked(const UVBRegionDefinition& Region, const FVBStoryState& Story)
	{
		return !Region.UnlockFact.IsValid() || Story.HasFact(Region.UnlockFact);
	}

	const UVBChapterDefinition* StepBaseline(FBaseline& Baseline, const UVBChapterDefinition* Current, int32 SaveReadSerial)
	{
		const FGameplayTag CurrentId = Current ? Current->ChapterId : FGameplayTag();
		const bool bAnnounce = Baseline.bSeen && SaveReadSerial == Baseline.SaveReadSerial && Current && CurrentId != Baseline.LastChapter;
		Baseline.LastChapter = CurrentId;
		Baseline.SaveReadSerial = SaveReadSerial;
		Baseline.bSeen = true;
		return bAnnounce ? Current : nullptr;
	}
}
