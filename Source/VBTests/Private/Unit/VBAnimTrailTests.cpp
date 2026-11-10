#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBSwingTrailProfile.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Materials/MaterialInterface.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Data/VBMeleeTraceShapeSet.h"
#include "Data/VBWeaponVisualConfig.h"
#include "VBAnimTrailTestTypes.h"
#include "VBIsolatedGameWorld.h"
#include "Character/VBCharacter.h"
#include "Character/VBSwingTrailComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "ParticleEmitterInstances.h"
#include "ProceduralMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include <limits>

namespace VBAnimTrailTest
{
	struct FFixture
	{
		FVBIsolatedGameWorld Scope;
		AVBCharacter* Body = nullptr;
		UVBAnimTrailFixtureVisual* Visual = nullptr;
		UVBSwingTrailComponent* Trail = nullptr;
		UVBSwingTrailProfile* Profile = nullptr;
		UParticleSystem* System = nullptr;
		IConsoleVariable* RenderSwitch = nullptr;
		int32 PreviousRender = 1;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Scope.Init(Test, true))
			{
				return false;
			}

			RenderSwitch = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Combat.SwingTrailMesh"));
			if (!Test.TestNotNull(TEXT("trail rendering switch"), RenderSwitch))
			{
				return false;
			}

			PreviousRender = RenderSwitch->GetInt();
			RenderSwitch->Set(1, ECVF_SetByCode);
			System = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/Mixed_Magic_VFX_Pack/VFX/P_Big_Sword_Trail.P_Big_Sword_Trail"));
			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
			const UVBSwingTrailProfile* Legacy = LoadObject<UVBSwingTrailProfile>(nullptr,
				TEXT("/Game/VB/Data/MeleeTrace/DA_SwingTrail_Unarmed.DA_SwingTrail_Unarmed"));
			if (!Test.TestNotNull(TEXT("selected system"), System) || !Test.TestNotNull(TEXT("actual body mesh"), Mesh)
				|| !Test.TestNotNull(TEXT("existing foot profile"), Legacy))
			{
				return false;
			}

			Body = Scope.World->SpawnActor<AVBCharacter>();
			if (!Test.TestNotNull(TEXT("trail owner"), Body))
			{
				return false;
			}

			Body->SetActorTickEnabled(false);
			Body->GetCharacterMovement()->SetComponentTickEnabled(false);
			Body->GetMesh()->SetSkeletalMeshAsset(Mesh);
			Body->GetMesh()->RegisterAllComponentTickFunctions(true);
			Profile = NewObject<UVBSwingTrailProfile>(Body);
			Profile->TrailMaterial = Legacy->TrailMaterial;
			Profile->DistortionMaterial = Legacy->DistortionMaterial;
			UVBMeleeTraceShapeSet* Shapes = NewObject<UVBMeleeTraceShapeSet>(Body);
			const FGameplayTag Tags[] = {VBGameplayTags::Melee_Shape_HandL, VBGameplayTags::Melee_Shape_HandR, VBGameplayTags::Melee_Shape_FootR};
			const FName Bones[] = {TEXT("hand_l"), TEXT("hand_r"), TEXT("calf_r")};
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
			{
				FVBMeleeTraceShapeDef Shape;
				Shape.Bone = Bones[Index];
				Shape.TipLocal = FVector(14.5f, 0.0f, 0.0f);
				Shape.TrailProfile = Profile;
				Shapes->Shapes.Add(Tags[Index], Shape);
				if (Index >= 2)
				{
					continue;
				}

				FVBAnimTrailBinding Binding;
				Binding.System = System;
				Binding.FirstSocket = Bones[Index];
				Binding.SecondSocket = Index == 0 ? FName(TEXT("middle_01_l")) : FName(TEXT("middle_01_r"));
				Profile->AnimTrails.Add(Tags[Index], Binding);
			}

			FVBMeleeTraceShapeDef WeaponFallback;
			WeaponFallback.Source = EVBMeleeShapeSource::WeaponVisual;
			WeaponFallback.FallbackShape = VBGameplayTags::Melee_Shape_HandR;
			Shapes->Shapes.Add(VBGameplayTags::Melee_Shape_Weapon, WeaponFallback);
			UVBAnimTrailFixtureTrace* Trace = NewObject<UVBAnimTrailFixtureTrace>(Body);
			Body->AddInstanceComponent(Trace);
			Trace->Configure(Shapes);
			Trace->RegisterComponent();
			Visual = NewObject<UVBAnimTrailFixtureVisual>(Body);
			Body->AddInstanceComponent(Visual);
			Visual->VisualConfig = NewObject<UVBWeaponVisualConfig>(Body);
			Visual->RegisterComponent();
			Visual->StartFixture();
			Trail = UVBSwingTrailComponent::FindOrAdd(Body);
			if (!Test.TestNotNull(TEXT("production trail owner"), Trail))
			{
				return false;
			}

			Trail->RegisterAllComponentTickFunctions(true);
			Trail->BeginPlay();
			return true;
		}

		TArray<UParticleSystemComponent*> Particles() const
		{
			TInlineComponentArray<UParticleSystemComponent*> Components(Body);
			TArray<UParticleSystemComponent*> Result;
			for (UParticleSystemComponent* Particle : Components)
			{
				if (IsValid(Particle) && Particle->IsRegistered() && Particle->Template == System)
				{
					Result.Add(Particle);
				}
			}

			return Result;
		}

		void BeginTrails(const void* Key, const FGameplayTagContainer& Shapes, float Duration)
		{
			Trail->BeginTrails(Key, Shapes, nullptr, Duration);
			// 격리 몸은 BeginPlay를 돌리지 않으므로 생성된 컴포넌트의 틱을 등록한다.
			for (UParticleSystemComponent* Particle : Particles())
			{
				Particle->RegisterAllComponentTickFunctions(true);
			}
		}

		void Step(int32 Frames, float DeltaTime = 0.05f)
		{
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Body->AddActorWorldOffset(FVector(2.0f, 0.0f, 0.0f));
				++GFrameCounter;
				Scope.World->Tick(LEVELTICK_All, DeltaTime);
			}
		}

		~FFixture()
		{
			if (IsValid(Visual) && Visual->HasBegunPlay())
			{
				Visual->StopFixture();
			}

			if (RenderSwitch)
			{
				RenderSwitch->Set(PreviousRender, ECVF_SetByCode);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAnimTrailProfileTest, "VB.Unit.MeleeTrail.AnimTrail.ProfileContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FVBAnimTrailProfileTest::RunTest(const FString& Parameters)
{
	UParticleSystem* Selected = LoadObject<UParticleSystem>(nullptr,
		TEXT("/Game/Mixed_Magic_VFX_Pack/VFX/P_Big_Sword_Trail.P_Big_Sword_Trail"));
	if (!TestNotNull(TEXT("actual selected Cascade donor"), Selected))
	{
		return false;
	}

	FVBAnimTrailBinding Binding;
	Binding.System = Selected;
	Binding.FirstSocket = TEXT("TrailBase");
	Binding.SecondSocket = TEXT("TrailTip");
	TestTrue(TEXT("measured AnimTrail template passes the profile contract"), Binding.GatherValidationErrors().IsEmpty());
	FVBAnimTrailBinding Invalid = Binding;
	Invalid.System = nullptr;
	TestFalse(TEXT("missing mapped system is rejected"), Invalid.GatherValidationErrors().IsEmpty());
	Invalid = Binding;
	Invalid.SecondSocket = Invalid.FirstSocket;
	TestFalse(TEXT("identical endpoints are rejected"), Invalid.GatherValidationErrors().IsEmpty());
	Invalid = Binding;
	Invalid.WidthScale = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("non-finite width is rejected"), Invalid.GatherValidationErrors().IsEmpty());
	Invalid = Binding;
	Invalid.MaxTailSeconds = 0.0f;
	TestFalse(TEXT("unbounded tail is rejected"), Invalid.GatherValidationErrors().IsEmpty());

	UParticleSystem* Owned = DuplicateObject<UParticleSystem>(Selected, GetTransientPackage());
	if (!TestNotNull(TEXT("isolated negative fixture"), Owned)
		|| !TestTrue(TEXT("fixture has a first emitter and LOD"), Owned->Emitters.Num() > 0
			&& Owned->Emitters[0] && Owned->Emitters[0]->LODLevels.Num() > 0 && Owned->Emitters[0]->LODLevels[0]))
	{
		return false;
	}

	UParticleLODLevel* LOD = Owned->Emitters[0]->LODLevels[0];
	if (!TestTrue(TEXT("negative does not mutate vendor emitter"), Owned->Emitters[0] != Selected->Emitters[0])
		|| !TestTrue(TEXT("negative does not mutate vendor LOD"), LOD != Selected->Emitters[0]->LODLevels[0]))
	{
		return false;
	}

	Invalid = Binding;
	Invalid.System = Owned;
	const auto OriginalType = LOD->TypeDataModule;
	LOD->TypeDataModule = nullptr;
	TestFalse(TEXT("a sprite LOD cannot silently replace AnimTrail"), Invalid.GatherValidationErrors().IsEmpty());
	LOD->TypeDataModule = OriginalType;
	TestTrue(TEXT("restoring type restores the same valid fixture"), Invalid.GatherValidationErrors().IsEmpty());
	const auto OriginalMaterial = LOD->RequiredModule->Material;
	LOD->RequiredModule->Material = nullptr;
	TestFalse(TEXT("missing trail material is rejected"), Invalid.GatherValidationErrors().IsEmpty());
	LOD->RequiredModule->Material = OriginalMaterial;
	TestTrue(TEXT("restored material restores the fixture"), Invalid.GatherValidationErrors().IsEmpty());
	TestTrue(TEXT("vendor donor remains valid after owned negatives"), Binding.GatherValidationErrors().IsEmpty());

	UVBSwingTrailProfile* Profile = NewObject<UVBSwingTrailProfile>();
	TestTrue(TEXT("existing procedural profile remains valid"), Profile->GatherValidationErrors().IsEmpty());
	Profile->AnimTrails.Add(VBGameplayTags::Melee_Shape_HandR, Binding);
	TestTrue(TEXT("per-shape binding validates"), Profile->GatherValidationErrors().IsEmpty());
	Profile->AnimTrails.Add(VBGameplayTags::State_Dead, Binding);
	TestFalse(TEXT("non-shape tag cannot select a particle trail"), Profile->GatherValidationErrors().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAnimTrailResolvedSourceTest, "VB.Unit.MeleeTrail.AnimTrail.ResolvedSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FVBAnimTrailResolvedSourceTest::RunTest(const FString& Parameters)
{
	const UVBMeleeTraceShapeSet* Shapes = LoadObject<UVBMeleeTraceShapeSet>(nullptr,
		TEXT("/Game/VB/Data/MeleeTrace/DA_MeleeShapes_Player.DA_MeleeShapes_Player"));
	const UVBWeaponVisualConfig* Visuals = LoadObject<UVBWeaponVisualConfig>(nullptr,
		TEXT("/Game/VB/Weapons/DA_WeaponVisuals.DA_WeaponVisuals"));
	if (!TestNotNull(TEXT("actual shape owner"), Shapes) || !TestNotNull(TEXT("actual visual owner"), Visuals))
	{
		return false;
	}

	FVBResolvedMeleeShape Resolved;
	for (EVBWeaponType Type : {EVBWeaponType::Blade, EVBWeaponType::BigSword})
	{
		TestTrue(TEXT("actual sword shape resolves"), UVBMeleeTraceComponent::ResolveShapeFromData(
			Shapes, VBGameplayTags::Melee_Shape_Weapon, Visuals, Type, Resolved));
		TestEqual(TEXT("resolved source preserves authored part identity"), Resolved.VisualPartIndex,
			Shapes->Shapes.FindChecked(VBGameplayTags::Melee_Shape_Weapon).VisualPartIndex);
	}

	TestTrue(TEXT("Fighter weapon shape resolves its authored body fallback"), UVBMeleeTraceComponent::ResolveShapeFromData(
		Shapes, VBGameplayTags::Melee_Shape_Weapon, Visuals, EVBWeaponType::Fighter, Resolved));
	TestEqual(TEXT("fallback reports the body source, not the previous sword part"), Resolved.VisualPartIndex, INDEX_NONE);
	TestTrue(TEXT("fallback reports the actual shape identity"), Resolved.ShapeTag == VBGameplayTags::Melee_Shape_HandR);
	TestEqual(TEXT("fallback retains the real hand bone"), Resolved.Bone,
		Shapes->Shapes.FindChecked(VBGameplayTags::Melee_Shape_HandR).Bone);

	UVBWeaponVisualConfig* OwnedVisuals = DuplicateObject<UVBWeaponVisualConfig>(Visuals, GetTransientPackage());
	UVBMeleeTraceShapeSet* OwnedShapes = DuplicateObject<UVBMeleeTraceShapeSet>(Shapes, GetTransientPackage());
	FVBWeaponVisualStyle& Style = OwnedVisuals->Styles.FindChecked(EVBWeaponType::Blade);
	if (!TestTrue(TEXT("fixture has a real sword part"), Style.Parts.Num() > 0))
	{
		return false;
	}

	const FVBWeaponVisualPart SecondPart = Style.Parts[0];
	Style.Parts.Add(SecondPart);
	OwnedShapes->Shapes.FindChecked(VBGameplayTags::Melee_Shape_Weapon).VisualPartIndex = 1;
	TestTrue(TEXT("second part resolves without first-part selection"), UVBMeleeTraceComponent::ResolveShapeFromData(
		OwnedShapes, VBGameplayTags::Melee_Shape_Weapon, OwnedVisuals, EVBWeaponType::Blade, Resolved));
	TestEqual(TEXT("second part identity survives resolution"), Resolved.VisualPartIndex, 1);
	Style.Parts[1].Mesh = nullptr;
	TestTrue(TEXT("same request falls back after its selected part loses the mesh"), UVBMeleeTraceComponent::ResolveShapeFromData(
		OwnedShapes, VBGameplayTags::Melee_Shape_Weapon, OwnedVisuals, EVBWeaponType::Blade, Resolved));
	TestEqual(TEXT("fallback clears even a nonzero previous part index"), Resolved.VisualPartIndex, INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAnimTrailLifecycleTest, "VB.Unit.MeleeTrail.AnimTrail.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)
bool FVBAnimTrailLifecycleTest::RunTest(const FString& Parameters)
{
	VBAnimTrailTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	const int32 Window = 1;
	FGameplayTagContainer Hands(VBGameplayTags::Melee_Shape_HandL);
	Hands.AddTag(VBGameplayTags::Melee_Shape_HandR);
	Fixture.BeginTrails(&Window, Hands, 1.0f);
	TArray<UParticleSystemComponent*> Particles = Fixture.Particles();
	if (!TestEqual(TEXT("one selected instance for each fist"), Particles.Num(), 2))
	{
		return false;
	}

	for (UParticleSystemComponent* Particle : Particles)
	{
		TestTrue(TEXT("selected template is active"), Particle->IsActive());
		TestTrue(TEXT("selected trail samples the actual body mesh"), Particle->GetAttachParent() == Fixture.Body->GetMesh());
		TestEqual(TEXT("owner dilation is not multiplied twice"), Particle->CustomTimeDilation, 1.0f);
	}

	TInlineComponentArray<UProceduralMeshComponent*> Ribbons(Fixture.Body);
	TestEqual(TEXT("mapped fists have no procedural underlay"), Ribbons.Num(), 0);
	Fixture.Step(3);
	TestTrue(TEXT("selected CPU trail really emits particles"), Particles[0]->GetNumActiveParticles() > 0);
	Fixture.Trail->EndTrail(&Window);
	TestTrue(TEXT("normal end suppresses new particles"), Particles[0]->bSuppressSpawning != 0);
	TestTrue(TEXT("existing particles remain active during the tail"), Particles[0]->IsActive()
		&& Particles[0]->GetNumActiveParticles() > 0);
	TestTrue(TEXT("normal end retains a live tail"), IsValid(Particles[0]) && Particles[0]->IsRegistered());
	Fixture.Step(20);
	TestEqual(TEXT("completed or bounded tails are removed"), Fixture.Particles().Num(), 0);

	Fixture.BeginTrails(&Window, Hands, 1.0f);
	Fixture.BeginTrails(&Window, Hands, 1.0f);
	int32 Active = 0;
	for (UParticleSystemComponent* Particle : Fixture.Particles())
	{
		Active += Particle->IsActive() && !Particle->bSuppressSpawning ? 1 : 0;
	}

	TestEqual(TEXT("repeat begin leaves only one emitting instance per shape"), Active, 2);
	Fixture.Trail->RetireVisualSources();
	TestEqual(TEXT("source retirement removes active and tail instances before source removal"), Fixture.Particles().Num(), 0);

	Fixture.Profile->AnimTrails.FindChecked(VBGameplayTags::Melee_Shape_HandR).SecondSocket = TEXT("MissingTrailSocket");
	AddExpectedError(TEXT("requires visible mesh sockets"), EAutomationExpectedErrorFlags::Contains, 1);
	Fixture.BeginTrails(&Window, FGameplayTagContainer(VBGameplayTags::Melee_Shape_HandR), 1.0f);
	TestEqual(TEXT("missing source socket produces zero particle fallback"), Fixture.Particles().Num(), 0);
	TInlineComponentArray<UProceduralMeshComponent*> FailedRibbons(Fixture.Body);
	TestEqual(TEXT("missing source socket produces zero procedural fallback"), FailedRibbons.Num(), 0);
	Fixture.Profile->AnimTrails.FindChecked(VBGameplayTags::Melee_Shape_HandR).SecondSocket = TEXT("middle_01_r");
	Fixture.BeginTrails(&Window, Hands, 1.0f);
	TestEqual(TEXT("restoring the same source restores selected rendering"), Fixture.Particles().Num(), 2);
	Fixture.Trail->RetireVisualSources();
	Fixture.BeginTrails(&Window, FGameplayTagContainer(VBGameplayTags::Melee_Shape_Weapon), 1.0f);
	TestEqual(TEXT("weapon-to-hand fallback uses the resolved hand binding"), Fixture.Particles().Num(), 1);
	Fixture.Trail->DestroyComponent();
	TestEqual(TEXT("EndPlay removes owned instances"), Fixture.Particles().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAnimTrailClockTest, "VB.Unit.MeleeTrail.AnimTrail.ClockAndRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)
bool FVBAnimTrailClockTest::RunTest(const FString& Parameters)
{
	VBAnimTrailTest::FFixture Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	const int32 HandWindow = 1;
	Fixture.BeginTrails(&HandWindow, FGameplayTagContainer(VBGameplayTags::Melee_Shape_HandR), 0.05f);
	TArray<UParticleSystemComponent*> Particles = Fixture.Particles();
	if (!TestEqual(TEXT("clock fixture selected instance"), Particles.Num(), 1))
	{
		return false;
	}

	Fixture.Body->CustomTimeDilation = 0.1f;
	Fixture.Step(10);
	TestTrue(TEXT("owner-time watchdog remains open during hitstop"), Particles[0]->IsActive());
	if (TestTrue(TEXT("real CPU emitter exists"), Particles[0]->EmitterInstances.Num() > 0 && Particles[0]->EmitterInstances[0]))
	{
		TestTrue(TEXT("particle age advances by owner time once"), FMath::IsNearlyEqual(
			Particles[0]->EmitterInstances[0]->SecondsSinceCreation, 0.05f, 0.02f));
	}

	Fixture.Body->CustomTimeDilation = 1.0f;
	Fixture.Step(30);
	TestEqual(TEXT("missing end watchdog and tail finish after time resumes"), Fixture.Particles().Num(), 0);

	const int32 FootWindow = 2;
	Fixture.BeginTrails(&FootWindow, FGameplayTagContainer(VBGameplayTags::Melee_Shape_FootR), 0.2f);
	TestEqual(TEXT("unmapped foot never spawns selected punch trail"), Fixture.Particles().Num(), 0);
	TInlineComponentArray<UProceduralMeshComponent*> Ribbons(Fixture.Body);
	TestEqual(TEXT("unmapped foot retains its original procedural renderer"), Ribbons.Num(), 1);
	Fixture.Trail->EndTrail(&FootWindow);
	Fixture.Step(10);
	if (Ribbons.Num() == 1)
	{
		TestFalse(TEXT("original foot tail returns to hidden pool"), Ribbons[0]->IsVisible());
	}

	return true;
}
#endif
