#include "Narrative/Ending/VBEndingSettings.h"

#include "Narrative/Ending/VBEndingCatalog.h"
#include "VB/VB.h"

const UVBEndingCatalog* UVBEndingSettings::LoadCatalog()
{
	const UVBEndingSettings* Settings = GetDefault<UVBEndingSettings>();
	const UVBEndingCatalog* Catalog = Settings->Catalog.LoadSynchronous();
	static bool bWarned = false;
	if (!Catalog && !bWarned)
	{
		bWarned = true;
		VB_LOG(Warning, "Ending catalog missing (Project Settings > VB Endings > Catalog = %s); finale unavailable",
			*Settings->Catalog.ToString());
	}

	return Catalog;
}
