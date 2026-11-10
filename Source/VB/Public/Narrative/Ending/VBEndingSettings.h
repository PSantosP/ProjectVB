#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBEndingSettings.generated.h"

class UVBEndingCatalog;

UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Endings"))
class VB_API UVBEndingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category="VB|Ending")
	TSoftObjectPtr<UVBEndingCatalog> Catalog;

	// 피날레 시도마다 한 번 읽는다 - UI 갱신이나 Tick 에서 부르지 않는다.
	static const UVBEndingCatalog* LoadCatalog();
};
