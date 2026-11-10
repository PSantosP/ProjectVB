#if WITH_DEV_AUTOMATION_TESTS

#include "VBEffectAuthoring.h"
#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Misc/AutomationTest.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/ParticleSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEffectDistributionCacheTest,
	"VB.Editor.EffectAuthoring.DistributionCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBEffectDistributionCacheTest::RunTest(const FString& Parameters)
{
	UParticleSystem* System = NewObject<UParticleSystem>();
	UParticleModuleLifetime* Lifetime = NewObject<UParticleModuleLifetime>(System);
	UDistributionFloatConstant* FloatSource = NewObject<UDistributionFloatConstant>(Lifetime);
	FloatSource->Constant = 0.4f;
	Lifetime->Lifetime.Distribution = FloatSource;
	Lifetime->Lifetime.Initialize();
	auto CachedLifetime = [Lifetime]()
	{
		FRawDistributionFloat Raw = Lifetime->Lifetime;
		Raw.Distribution = nullptr;
		return Raw.GetValue();
	};
	TestEqual(TEXT("initial serialized lifetime"), CachedLifetime(), 0.4f);
	FloatSource->Constant = 0.12f;
	FloatSource->bIsDirty = false;
	TestNotEqual(TEXT("old cache fails the changed lifetime"), CachedLifetime(), FloatSource->Constant);
	TestTrue(TEXT("rebuild actual lifetime owner"), UVBEffectAuthoring::RebuildCascadeDistribution(Lifetime, TEXT("Lifetime")));
	TestEqual(TEXT("serialized lifetime follows its source"), CachedLifetime(), FloatSource->Constant);

	UParticleModuleColorOverLife* Color = NewObject<UParticleModuleColorOverLife>(System);
	UDistributionVectorConstant* VectorSource = NewObject<UDistributionVectorConstant>(Color);
	VectorSource->Constant = FVector(0.2f, 0.3f, 0.4f);
	Color->ColorOverLife.Distribution = VectorSource;
	Color->ColorOverLife.Initialize();
	auto CachedColor = [Color]()
	{
		FRawDistributionVector Raw = Color->ColorOverLife;
		Raw.Distribution = nullptr;
		return Raw.GetValue();
	};
	TestTrue(TEXT("initial serialized color"), CachedColor().Equals(VectorSource->Constant, 1.e-6));
	VectorSource->Constant = FVector(0.1f, 0.5f, 0.9f);
	VectorSource->bIsDirty = false;
	TestFalse(TEXT("old cache fails the changed color"), CachedColor().Equals(VectorSource->Constant, 1.e-6));
	TestTrue(TEXT("rebuild actual color owner"), UVBEffectAuthoring::RebuildCascadeDistribution(Color, TEXT("ColorOverLife")));
	TestTrue(TEXT("serialized color follows its source"), CachedColor().Equals(VectorSource->Constant, 1.e-6));

	AddExpectedError(TEXT("Cascade distribution rebuild requires a valid particle module"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("null module rejects"), UVBEffectAuthoring::RebuildCascadeDistribution(nullptr, TEXT("Lifetime")));
	AddExpectedError(TEXT("Cascade distribution rebuild requires a valid float/vector distribution"),
		EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("missing property rejects"), UVBEffectAuthoring::RebuildCascadeDistribution(Lifetime, TEXT("Missing")));
	TestFalse(TEXT("non-distribution property rejects"), UVBEffectAuthoring::RebuildCascadeDistribution(Lifetime, TEXT("bEnabled")));
	UParticleModuleLifetime* MissingSource = NewObject<UParticleModuleLifetime>(System);
	MissingSource->Lifetime.Distribution = nullptr;
	TestFalse(TEXT("missing distribution rejects"), UVBEffectAuthoring::RebuildCascadeDistribution(MissingSource, TEXT("Lifetime")));
	TestEqual(TEXT("invalid requests preserve the valid lifetime cache"), CachedLifetime(), FloatSource->Constant);
	TestTrue(TEXT("invalid requests preserve the valid color cache"), CachedColor().Equals(VectorSource->Constant, 1.e-6));
	return true;
}

#endif
