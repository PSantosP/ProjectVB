// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBLooseTagHold.h"

#include "AbilitySystemComponent.h"

bool FVBLooseTagHold::Hold(UAbilitySystemComponent* InASC, const FGameplayTagContainer& InTags, EGameplayTagReplicationState InReplication)
{
	if (bHeld || !InASC)
	{
		return false;
	}

	InASC->AddLooseGameplayTags(InTags, 1, InReplication);
	ASC = InASC;
	Tags = InTags;
	Replication = InReplication;
	bHeld = true;
	return true;
}

void FVBLooseTagHold::Release()
{
	if (!bHeld)
	{
		return;
	}

	if (UAbilitySystemComponent* HeldOn = ASC.Get())
	{
		HeldOn->RemoveLooseGameplayTags(Tags, 1, Replication);
	}

	ASC.Reset();
	Tags.Reset();
	bHeld = false;
}
