// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Passive/VBPassiveRules.h"

#include "GameFramework/PlayerState.h"
#include "Passive/VBPassiveComponent.h"
#include "VB/VB.h"

bool FVBEffect_GrantPassive::CanApply(const FVBRuleContext& Context) const
{
	const UVBPassiveComponent* Passives = UVBPassiveComponent::FindFor(Context.Instigator);
	if (!Passives)
	{
		VB_LOG(Error, "효과 패시브 사전 검사(%s): 부른 플레이어에게 패시브 컴포넌트가 없다", *PassiveTag.ToString());
		return false;
	}

	const EVBPassiveGrantResult Result = Passives->CanGrant(PassiveTag);
	return Result == EVBPassiveGrantResult::Acquired || Result == EVBPassiveGrantResult::AlreadyAcquired;
}

void FVBEffect_GrantPassive::Apply(const FVBRuleContext& Context) const
{
	UVBPassiveComponent* Passives = UVBPassiveComponent::FindFor(Context.Instigator);
	if (!Passives)
	{
		VB_LOG(Error, "효과 패시브 주기(%s): 부른 플레이어에게 패시브 컴포넌트가 없다", *PassiveTag.ToString());
		Context.ReportEffectFailure();
		return;
	}

	const EVBPassiveGrantResult Result = Passives->Grant(PassiveTag);
	if (Result != EVBPassiveGrantResult::Acquired && Result != EVBPassiveGrantResult::AlreadyAcquired)
	{
		Context.ReportEffectFailure();
	}
}
