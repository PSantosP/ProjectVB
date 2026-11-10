// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Campaign/VBCampaignRules.h"

#include "Engine/World.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"
#include "VB/VB.h"

namespace
{
	// 회차 상태 컴포넌트가 없거나 태그가 비면 거짓이다(bNegate 와 무관) - 모르는 것을 참으로 처리하면 1회차 전용이나 2회차 전용이 아무 때나 열린다.
	const UVBCampaignStateComponent* FindCampaign(const FVBRuleContext& Context, const TCHAR* What, FGameplayTag Tag)
	{
		if (!Tag.IsValid())
		{
			VB_LOG(Warning, "조건(%s): 이름표가 비었다 - 거짓으로 친다", What);
			return nullptr;
		}

		const UVBCampaignStateComponent* Campaign = Context.World ? UVBCampaignStateComponent::Get(Context.World) : nullptr;
		if (!Campaign)
		{
			VB_LOG(Warning, "조건(%s %s): 이 월드에 회차 주인이 없다 - 거짓으로 친다", What, *Tag.ToString());
			return nullptr;
		}
		// 이름 없는 회차 = 아직 모른다(클라에 복제가 오기 전, 서버의 첫 읽기 전). 비어 있는 앞 회차를 1회차로 읽으면 bNegate 가 참이 된다.
		if (Campaign->GetRunIndex() < 1)
		{
			VB_LOG(Log, "조건(%s %s): 회차를 아직 모른다 - 거짓으로 친다", What, *Tag.ToString());
			return nullptr;
		}

		return Campaign;
	}
}

bool FVBCondition_PreviousEnding::Evaluate(const FVBRuleContext& Context) const
{
	const UVBCampaignStateComponent* Campaign = FindCampaign(Context, TEXT("앞 회차 결말"), Ending);
	return Campaign && Campaign->GetPrevious().Ending.MatchesTag(Ending) != bNegate;
}

bool FVBCondition_PreviousFact::Evaluate(const FVBRuleContext& Context) const
{
	const UVBCampaignStateComponent* Campaign = FindCampaign(Context, TEXT("앞 회차 사실"), Fact);
	return Campaign && Campaign->GetPrevious().Facts.HasTagExact(Fact) != bNegate;
}
