// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBWorldMarkerComponent.h"

#include "Engine/World.h"
#include "Map/VBWorldMarkerSubsystem.h"
#include "VB/VB.h"

UVBWorldMarkerComponent::UVBWorldMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBWorldMarkerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!MarkerTag.IsValid())
	{
		// 태그 없는 마커는 아무도 못 찾는다 - 경고 없이 빠지면 마커가 안 뜨는 원인을 퀘스트 쪽에서 찾게 된다.
		VB_LOG(Warning, "표지 %s(%s): MarkerTag 가 비어 등록하지 않는다", *GetName(), *GetNameSafe(GetOwner()));
		return;
	}

	if (UVBWorldMarkerSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBWorldMarkerSubsystem>() : nullptr)
	{
		Registry->Register(this);
	}
}

void UVBWorldMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBWorldMarkerSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBWorldMarkerSubsystem>() : nullptr)
	{
		Registry->Unregister(this);
	}

	Super::EndPlay(EndPlayReason);
}
