// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherParticipant.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Character/VBFinisherActorQueries.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void FVBFinisherParticipant::LinkVictim(USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot, AActor& Victim)
{
	if (OwnerMesh)
	{
		USkeletalMeshComponent* OtherMesh = VBFinisherActorQueries::FindBodyMesh(&Victim);
		bAddedTickPrerequisite = !OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
			FTickPrerequisite(OtherMesh, OtherMesh->PrimaryComponentTick));
		OwnerMesh->AddTickPrerequisiteComponent(OtherMesh);
	}

	if (UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Victim.GetRootComponent()); OwnerRoot && OtherRoot)
	{
		bAddedOwnerCollisionIgnore = !OwnerRoot->GetMoveIgnoreComponents().Contains(OtherRoot);
		bAddedVictimCollisionIgnore = !OtherRoot->GetMoveIgnoreComponents().Contains(OwnerRoot);
		OwnerRoot->IgnoreComponentWhenMoving(OtherRoot, true);
		OtherRoot->IgnoreComponentWhenMoving(OwnerRoot, true);
	}
}

void FVBFinisherParticipant::UnlinkVictim(USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot, AActor& Victim) const
{
	if (OwnerMesh && VBFinisherActorQueries::FindBodyMesh(&Victim) && bAddedTickPrerequisite)
	{
		OwnerMesh->RemoveTickPrerequisiteComponent(VBFinisherActorQueries::FindBodyMesh(&Victim));
	}

	if (UPrimitiveComponent* OtherRoot = Cast<UPrimitiveComponent>(Victim.GetRootComponent()); OwnerRoot && OtherRoot)
	{
		if (bAddedOwnerCollisionIgnore)
		{
			OwnerRoot->IgnoreComponentWhenMoving(OtherRoot, false);
		}

		if (bAddedVictimCollisionIgnore)
		{
			OtherRoot->IgnoreComponentWhenMoving(OwnerRoot, false);
		}
	}
}

void FVBFinisherParticipant::ReleaseMontageOwnership(UObject* Holder) const
{
	if (UVBAbilitySystemComponent* ASC = AbilitySystem.Get())
	{
		ASC->ReleaseExternalMontageOwnership(Holder);
	}
}
