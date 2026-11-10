// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBFinisherActorQueries.h"

#include "Animation/AnimInstance.h"
#include "Animation/VBInteractionAnimInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

namespace VBFinisherActorQueries
{
	const FName RoleAttacker(TEXT("Attacker"));
	const FName RoleVictim(TEXT("Victim"));

	USkeletalMeshComponent* FindBodyMesh(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}

		if (const ACharacter* Character = Cast<ACharacter>(Actor))
		{
			return Character->GetMesh();
		}

		return Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	}

	UAnimInstance* FindAnimInstance(const AActor* Actor)
	{
		const USkeletalMeshComponent* Mesh = FindBodyMesh(Actor);
		return Mesh ? Mesh->GetAnimInstance() : nullptr;
	}

	IVBInteractionAnimInterface* FindInteractionAnim(const AActor* Actor)
	{
		return Cast<IVBInteractionAnimInterface>(FindAnimInstance(Actor));
	}
}
