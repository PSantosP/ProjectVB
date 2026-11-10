// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Environment/VBFogPlayerDriver.h"

#include "Engine/World.h"
#include "VB/VB.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBFogPlayerDriver"

namespace
{
	const FName PlayerPositionParameter(TEXT("PlayerPosition"));
}

AVBFogPlayerDriver::AVBFogPlayerDriver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.05f;
	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
}

void AVBFogPlayerDriver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateFogPosition(false);
}

void AVBFogPlayerDriver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UpdateFogPosition(true);
	Super::EndPlay(EndPlayReason);
}

void AVBFogPlayerDriver::UpdateFogPosition(bool bClear)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (!FogParameters)
	{
		if (!bWarnedMissingCollection)
		{
			VB_LOG(Warning, "FogPlayerDriver %s: FogParameters is missing", *GetName());
			bWarnedMissingCollection = true;
		}

		return;
	}

	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(FogParameters);
	if (!Instance)
	{
		if (!bWarnedMissingInstance)
		{
			VB_LOG(Warning, "FogPlayerDriver %s: parameter collection instance is missing", *GetName());
			bWarnedMissingInstance = true;
		}

		return;
	}

	const APlayerController* Controller = World->GetFirstPlayerController();
	const APawn* Pawn = !bClear && Controller && Controller->IsLocalController() ? Controller->GetPawn() : nullptr;
	const FVector Position = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	if (!Instance->SetVectorParameterValue(PlayerPositionParameter,
		FLinearColor(Position.X, Position.Y, Position.Z, Pawn ? 1.0f : 0.0f)) && !bWarnedMissingParameter)
	{
		VB_LOG(Warning, "FogPlayerDriver %s: PlayerPosition vector parameter is missing", *GetName());
		bWarnedMissingParameter = true;
	}
}

#if WITH_EDITOR
EDataValidationResult AVBFogPlayerDriver::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!FogParameters || !FogParameters->GetVectorParameterNames().Contains(PlayerPositionParameter))
	{
		Context.AddError(LOCTEXT("MissingPositionParameter", "Fog parameters require the PlayerPosition vector."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
