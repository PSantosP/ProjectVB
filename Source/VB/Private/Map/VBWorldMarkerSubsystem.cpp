// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBWorldMarkerSubsystem.h"

#include "Map/VBWorldMarkerComponent.h"

void UVBWorldMarkerSubsystem::Register(UVBWorldMarkerComponent* Marker)
{
	if (Marker)
	{
		Markers.AddUnique(Marker);
	}
}

void UVBWorldMarkerSubsystem::Unregister(UVBWorldMarkerComponent* Marker)
{
	Markers.RemoveAll([Marker](const TWeakObjectPtr<UVBWorldMarkerComponent>& Entry) { return !Entry.IsValid() || Entry.Get() == Marker; });
}

void UVBWorldMarkerSubsystem::FindMarkers(const FGameplayTagContainer& Tags, TArray<UVBWorldMarkerComponent*>& OutMarkers) const
{
	OutMarkers.Reset();
	if (Tags.IsEmpty())
	{
		return;
	}

	for (const TWeakObjectPtr<UVBWorldMarkerComponent>& Entry : Markers)
	{
		UVBWorldMarkerComponent* Marker = Entry.Get();
		if (Marker && Tags.HasTagExact(Marker->GetMarkerTag()))
		{
			OutMarkers.Add(Marker);
		}
	}
}

bool UVBWorldMarkerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 마커는 플레이 중에만 등록된다(BeginPlay). 에디터 월드에 빈 등록부를 만들 까닭이 없다.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}
