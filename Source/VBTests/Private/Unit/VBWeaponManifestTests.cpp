// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBSummonedWeapon.h"
#include "Character/VBWeaponManifest.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/VBWeaponPickup.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// manifest material 은 기본으로 메시를 숨긴다(progress 0). 바닥 pickup 은 스스로 다 드러나고, 손의 소환 무기는 숨은 채 소환을 따른다.
//   둘 다 VBWeaponManifest 로 메시 bounds 에서 드러남 범위를 읽는다. 반대 대조: pickup 의 SetProgress(1) 을 빼면 fully formed 줄이 실패한다.
namespace VBWeaponManifestTest
{
	float Scalar(const UMaterialInterface* Material, const TCHAR* Name)
	{
		float Value = -1.0f;
		if (Material)
		{
			Material->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Name)), Value);
		}

		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponManifestTest, "VB.Unit.Weapon.Manifest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponManifestTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponManifestTest;
	UStaticMesh* Blade = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/VB/Weapons/Meshes/SM_SummonedBlade.SM_SummonedBlade"));
	UMaterialInterface* Manifest = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/VB/Weapons/Materials/M_WeaponManifest.M_WeaponManifest"));
	if (!TestNotNull(TEXT("shipped SM_SummonedBlade"), Blade) || !TestNotNull(TEXT("shipped M_WeaponManifest"), Manifest))
	{
		return false;
	}

	TestTrue(TEXT("the shipped weapon material is the manifest material"), VBWeaponManifest::UsesManifest(Manifest));
	TestTrue(TEXT("its default hides the mesh"), FMath::IsNearlyZero(Scalar(Manifest, TEXT("ManifestProgress"))));
	TestFalse(TEXT("an engine default material is not"), VBWeaponManifest::UsesManifest(UMaterial::GetDefaultMaterial(MD_Surface)));

	// 드러남 범위는 잰 축을 따라 메시 전체를 덮는다.
	const FBox Bounds = Blade->GetBoundingBox();
	TestTrue(TEXT("the katana's longest axis is its blade (Y)"), VBWeaponManifest::LongestAxis(Blade).Equals(FVector::RightVector));
	const VBWeaponManifest::FRevealRange Range = VBWeaponManifest::MeasureReveal(Blade, FVector::RightVector);
	TestTrue(TEXT("range starts at the mesh's near end"), FMath::IsNearlyEqual(Range.Min, Bounds.Min.Y, 0.01f));
	TestTrue(TEXT("range ends at the mesh's far end"), FMath::IsNearlyEqual(Range.Max, Bounds.Max.Y, 0.01f));

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	World->InitializeActorsForPlay(FURL());

	// manifest material 을 쓴 pickup 은 플레이에 들어가면 다 드러나고, 다른 material 칸은 저작한 그대로 둔다.
	AVBWeaponPickup* Pickup = World->SpawnActor<AVBWeaponPickup>();
	UStaticMeshComponent* PickupMesh = Pickup ? Pickup->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	AVBWeaponPickup* Plain = World->SpawnActor<AVBWeaponPickup>();
	UStaticMeshComponent* PlainMesh = Plain ? Plain->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	if (TestNotNull(TEXT("pickup mesh"), PickupMesh) && TestNotNull(TEXT("plain pickup mesh"), PlainMesh))
	{
		PickupMesh->SetStaticMesh(Blade);
		PlainMesh->SetStaticMesh(Blade);
		UMaterialInterface* EngineDefault = UMaterial::GetDefaultMaterial(MD_Surface);
		for (int32 Slot = 0; Slot < PlainMesh->GetNumMaterials(); ++Slot)
		{
			PlainMesh->SetMaterial(Slot, EngineDefault);
		}

		Pickup->DispatchBeginPlay();
		Plain->DispatchBeginPlay();
		const UMaterialInterface* Shown = PickupMesh->GetMaterial(0);
		TestNotNull(TEXT("pickup: its own dynamic instance"), Cast<UMaterialInstanceDynamic>(Shown));
		TestTrue(TEXT("pickup: fully formed (progress 1)"), FMath::IsNearlyEqual(Scalar(Shown, TEXT("ManifestProgress")), 1.0f));
		TestTrue(TEXT("pickup: range from the mesh bounds"), FMath::IsNearlyEqual(Scalar(Shown, TEXT("RevealMin")), Bounds.Min.Y, 0.01f));
		TestTrue(TEXT("plain pickup: another material is left as authored"), PlainMesh->GetMaterial(0) == EngineDefault);
	}

	// 소환 무기는 여전히 숨은 채 시작해서 소환을 따른다.
	AVBSummonedWeapon* Summoned = World->SpawnActor<AVBSummonedWeapon>();
	if (TestNotNull(TEXT("summoned weapon"), Summoned))
	{
		Summoned->VisualPart.Mesh = Blade;
		Summoned->VisualPart.Material = Manifest;
		Summoned->VisualPart.RevealAxis = FVector::RightVector;
		Summoned->RebuildVisual();
		const UMaterialInterface* Held = Summoned->WeaponMesh->GetMaterial(0);
		TestTrue(TEXT("summoned: starts hidden (progress 0)"), FMath::IsNearlyZero(Scalar(Held, TEXT("ManifestProgress"))));
		TestTrue(TEXT("summoned: same range as the pickup"), FMath::IsNearlyEqual(Scalar(Held, TEXT("RevealMin")), Bounds.Min.Y, 0.01f));
		Summoned->SetManifestProgress(1.0f);
		TestTrue(TEXT("summoned: summon drives it to 1"), FMath::IsNearlyEqual(Scalar(Held, TEXT("ManifestProgress")), 1.0f));
	}

	for (AActor* Actor : TArray<AActor*>{Pickup, Plain, Summoned}) if (Actor) Actor->Destroy();
	World->DestroyWorld(false);
	if (Package)
	{
		Package->SetDirtyFlag(false);
	}

	return true;
}
#endif
