#include "CharacterCommon/VBStrideScale.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

float VBStrideScale::Sanitize(float ScaleX)
{
	return (ScaleX > KINDA_SMALL_NUMBER) ? ScaleX : 1.0f;
}

float VBStrideScale::Resolve(const AActor* Actor)
{
	const ACharacter* Character = Cast<ACharacter>(Actor);
	const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	return Mesh ? Sanitize(static_cast<float>(Mesh->GetComponentScale().X)) : 1.0f;
}
