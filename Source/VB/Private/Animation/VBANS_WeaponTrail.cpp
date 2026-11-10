// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBANS_WeaponTrail.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBSwingTrailComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBSwingTrailProfile.h"
#include "Engine/World.h"

UVBANS_WeaponTrail::UVBANS_WeaponTrail()
{
	Shapes.AddTag(VBGameplayTags::Melee_Shape_Weapon);
#if WITH_EDITORONLY_DATA
	// 애니 편집기 미리보기 액터에는 판정 컴포넌트가 없어 날을 못 푼다.
	bShouldFireInEditor = false;
#endif
}

void UVBANS_WeaponTrail::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	const UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
	if (!Owner || !World || !World->IsGameWorld())
	{
		return;
	}
	// 열쇠는 이 노티 객체다. EventReference.GetNotify() 는 엔진이 매 프레임 복사본으로 바꿔 시작, 끝 주소가 다르다.
	if (UVBSwingTrailComponent* Trail = UVBSwingTrailComponent::FindOrAdd(Owner))
	{
		Trail->BeginTrails(this, Shapes, Profile, TotalDuration);
	}
}

void UVBANS_WeaponTrail::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UVBSwingTrailComponent* Trail = Owner ? Owner->FindComponentByClass<UVBSwingTrailComponent>() : nullptr)
	{
		Trail->EndTrail(this);
	}
}

FString UVBANS_WeaponTrail::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Weapon Trail (%s)"), *Shapes.ToStringSimple());
}

void UVBANS_WeaponTrail::PostLoad()
{
	Super::PostLoad();
	MigrateLegacyShape();
}

void UVBANS_WeaponTrail::MigrateLegacyShape()
{
	// 새 데이터는 옛 필드를 쓰지 않으므로 유효한 값은 옛 데이터뿐이다.
	if (Shape_DEPRECATED.IsValid())
	{
		Shapes = FGameplayTagContainer(Shape_DEPRECATED);
		Shape_DEPRECATED = FGameplayTag();
	}
}
