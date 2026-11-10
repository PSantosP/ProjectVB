// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBMapSettings.h"

#include "Engine/World.h"
#include "Map/VBMapDefinition.h"
#include "UObject/Package.h"

const UVBMapDefinition* UVBMapSettings::FindForWorld(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	const FString PackageName = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return GetDefault<UVBMapSettings>()->FindForPackage(PackageName);
}

const UVBMapDefinition* UVBMapSettings::FindForPackage(const FString& PackageName) const
{
	for (const FVBMapEntry& Entry : Maps)
	{
		if (!Entry.World.IsNull() && Entry.World.ToSoftObjectPath().GetLongPackageName().Equals(PackageName, ESearchCase::IgnoreCase))
		{
			const UVBMapDefinition* Definition = Entry.Definition.LoadSynchronous();
			return Definition && Definition->IsUsable() ? Definition : nullptr;
		}
	}

	return nullptr;
}
