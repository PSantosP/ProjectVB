#pragma once

#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBWeaponVisualComponent.h"
#include "Data/VBMeleeTraceShapeSet.h"
#include "VBAnimTrailTestTypes.generated.h"

UCLASS()
class UVBAnimTrailFixtureTrace : public UVBMeleeTraceComponent
{
	GENERATED_BODY()
public:
	void Configure(UVBMeleeTraceShapeSet* Shapes) { ShapeSet = Shapes; }
};

UCLASS()
class UVBAnimTrailFixtureVisual : public UVBWeaponVisualComponent
{
	GENERATED_BODY()
public:
	void StartFixture() { RegisterAllComponentTickFunctions(true); Super::BeginPlay(); SetComponentTickEnabled(false); }
	void StopFixture() { Super::EndPlay(EEndPlayReason::Destroyed); }
};
