// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitFlashSettings.h"

#include "Data/VBHitFlashConfig.h"

UVBHitFlashConfig* UVBHitFlashSettings::ResolveConfig(UVBHitFlashConfig* Explicit)
{
	if (Explicit)
	{
		return Explicit;
	}

	return GetDefault<UVBHitFlashSettings>()->DefaultConfig.LoadSynchronous();
}
