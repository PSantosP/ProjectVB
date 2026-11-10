// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBBossEncounterSubsystem.h"

#include "Enemy/VBBossBase.h"

void UVBBossEncounterSubsystem::SetEngaged(AVBBossBase* Boss, bool bEngaged)
{
	if (!Boss)
	{
		return;
	}

	const int32 Existing = Engaged.IndexOfByPredicate([Boss](const TWeakObjectPtr<AVBBossBase>& Entry) { return Entry.Get() == Boss; });
	if (bEngaged == (Existing != INDEX_NONE))
	{
		return;
	}

	if (bEngaged)
	{
		Engaged.Add(Boss);
	}
	else
	{
		Engaged.RemoveAt(Existing);
	}

	AnnounceIfChanged();
}

AVBBossBase* UVBBossEncounterSubsystem::GetEngagedBoss()
{
	Engaged.RemoveAll([](const TWeakObjectPtr<AVBBossBase>& Entry) { return !Entry.IsValid(); });
	return Engaged.IsEmpty() ? nullptr : Engaged.Last().Get();
}

void UVBBossEncounterSubsystem::AnnounceIfChanged()
{
	AVBBossBase* Current = GetEngagedBoss();
	if (Current != Announced.Get() || (!Current && !Announced.IsExplicitlyNull()))
	{
		Announced = Current;
		OnEngagedBossChanged.Broadcast(Current);
	}
}
