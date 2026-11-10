// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 국소 피격 플래시와 그 접촉점(VBBodyContact::ResolveCuePoint). 몸에 닿지 않은 HitResult(액터 없음)는 원래 접점 그대로다.
// 음성 대조 -> 실패해야 하는 단언: HitResult 보다 Location 을 먼저 보기 -> CuePoint.HitWins,
//   원점 Location 을 접촉으로 받기 -> CuePoint.NoneIsNoContact, 원점 접점을 받기 -> CuePoint.ZeroImpactFallsBack.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CharacterCommon/VBBodyContact.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBHitFlashConfig.h"
#include "Data/VBHitFlashSettings.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffectTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace VBHitFlashTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FGameplayCueParameters WithHit(const FVector& ImpactPoint)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = ImpactPoint;
		Hit.Location = ImpactPoint;
		FGameplayCueParameters Parameters;
		Parameters.EffectContext = FGameplayEffectContextHandle(new FGameplayEffectContext());
		Parameters.EffectContext.AddHitResult(Hit);
		return Parameters;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashContactHitWinsTest, "VB.Unit.BodyContact.CuePoint.HitWins", VBHitFlashTests::Flags)
bool FVBHitFlashContactHitWinsTest::RunTest(const FString& Parameters)
{
	FGameplayCueParameters Cue = VBHitFlashTests::WithHit(FVector(10.0, 20.0, 130.0));
	Cue.Location = FVector(500.0, 0.0, 0.0);
	FVector Point;
	TestTrue(TEXT("a hit result gives a contact point"), VBBodyContact::ResolveCuePoint(Cue, Point));
	TestEqual(TEXT("the hit's impact point wins over the cue location"), Point, FVector(10.0, 20.0, 130.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashContactLocationTest, "VB.Unit.BodyContact.CuePoint.LocationFallback", VBHitFlashTests::Flags)
bool FVBHitFlashContactLocationTest::RunTest(const FString& Parameters)
{
	FGameplayCueParameters Cue;
	Cue.EffectContext = FGameplayEffectContextHandle(new FGameplayEffectContext());
	Cue.Location = FVector(-40.0, 5.0, 90.0);
	FVector Point;
	TestTrue(TEXT("a context without a hit falls back to the cue location"), VBBodyContact::ResolveCuePoint(Cue, Point));
	TestEqual(TEXT("the cue location is the contact point"), Point, FVector(-40.0, 5.0, 90.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashContactZeroImpactTest, "VB.Unit.BodyContact.CuePoint.ZeroImpactFallsBack", VBHitFlashTests::Flags)
bool FVBHitFlashContactZeroImpactTest::RunTest(const FString& Parameters)
{
	FGameplayCueParameters Cue = VBHitFlashTests::WithHit(FVector::ZeroVector);
	FVector Point;
	TestFalse(TEXT("a hit with an unfilled (origin) impact point and no location is no contact"),
		VBBodyContact::ResolveCuePoint(Cue, Point));
	Cue.Location = FVector(7.0, 8.0, 9.0);
	TestTrue(TEXT("an unfilled impact point falls back to the cue location"), VBBodyContact::ResolveCuePoint(Cue, Point));
	TestEqual(TEXT("the cue location is used"), Point, FVector(7.0, 8.0, 9.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashContactNoneTest, "VB.Unit.BodyContact.CuePoint.NoneIsNoContact", VBHitFlashTests::Flags)
bool FVBHitFlashContactNoneTest::RunTest(const FString& Parameters)
{
	FGameplayCueParameters Cue;
	FVector Point(1.0, 1.0, 1.0);
	TestFalse(TEXT("no context and a zero location is no contact"), VBBodyContact::ResolveCuePoint(Cue, Point));
	return true;
}

// 접촉점 없는 피해는 번쩍임을 그리지 않는다 - 반지름 0(온몸)이 아니다. 실제 DA_HitFlash_Default 를 쓴다.
// 음성 대조: TriggerFlash 에 null 점 ApplyProfile 폴백을 되살리면 "no contact" 두 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashNoContactNoFlashTest, "VB.Unit.HitFlash.NoContactDrawsNothing", VBHitFlashTests::Flags)
bool FVBHitFlashNoContactNoFlashTest::RunTest(const FString& Parameters)
{
	UVBHitFlashConfig* Config = LoadObject<UVBHitFlashConfig>(nullptr, TEXT("/Game/VB/Data/DA_HitFlash_Default.DA_HitFlash_Default"));
	if (!TestNotNull(TEXT("shipped flash config"), Config)
		|| !TestNotNull(TEXT("its overlay material"), Config->DefaultProfile.OverlayMaterial.Get()))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());

	ACharacter* Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("victim"), Victim))
	{
		return false;
	}

	UVBHitFlashComponent* Flash = NewObject<UVBHitFlashComponent>(Victim);
	// config 는 저작 전용(EditDefaultsOnly, protected)이라 BP 기본값처럼 넣는다
	FObjectProperty* ConfigProperty = FindFProperty<FObjectProperty>(UVBHitFlashComponent::StaticClass(), TEXT("HitFlashConfig"));
	if (!TestNotNull(TEXT("HitFlashConfig property"), ConfigProperty))
	{
		return false;
	}

	ConfigProperty->SetObjectPropertyValue_InContainer(Flash, Config);
	Victim->AddInstanceComponent(Flash);
	Flash->RegisterComponent();
	USkeletalMeshComponent* Mesh = Victim->GetMesh();

	FGameplayCueParameters NoContact;
	NoContact.EffectContext = FGameplayEffectContextHandle(new FGameplayEffectContext());
	Flash->TriggerFlash(NoContact);
	TestNull(TEXT("no contact: nothing is drawn"), Mesh->GetOverlayMaterial());

	const FVector Contact = Victim->GetActorLocation() + FVector(0.0, 0.0, 40.0);
	Flash->TriggerFlash(VBHitFlashTests::WithHit(Contact));
	const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetOverlayMaterial());
	if (!TestNotNull(TEXT("a contact draws the flash"), MID))
	{
		return false;
	}

	float Radius = 0.0f;
	TestTrue(TEXT("the flash material has FlashLocalRadius"),
		MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FlashLocalRadius")), Radius));
	TestTrue(TEXT("a contact flash is local (radius > 0)"), Radius > 0.0f);

	// 번쩍이는 중 접촉점 없는 타격이 와도 온몸으로 다시 칠하지 않아야 한다
	Flash->TriggerFlash(NoContact);
	Radius = 0.0f;
	MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("FlashLocalRadius")), Radius);
	TestTrue(TEXT("no contact after a contact: radius stays local, never 0"), Radius > 0.0f);
	return true;
}

// flash config 를 안 적은 적 BP(BP_BossTroll)도 번쩍인다 - 프로젝트 기본값(UVBHitFlashSettings)이 대신 답한다.
// 음성 대조: GetHitFlashConfig 에 class-default 폴백을 되살리면 "an unwired component flashes" 가 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitFlashProjectDefaultTest, "VB.Unit.HitFlash.ProjectDefault", VBHitFlashTests::Flags)
bool FVBHitFlashProjectDefaultTest::RunTest(const FString& Parameters)
{
	const UVBHitFlashConfig* Default = UVBHitFlashSettings::ResolveConfig(nullptr);
	if (!TestNotNull(TEXT("the project default is set"), Default)
		|| !TestNotNull(TEXT("its overlay material"), Default->DefaultProfile.OverlayMaterial.Get()))
	{
		return false;
	}

	UVBHitFlashConfig* Explicit = GetMutableDefault<UVBHitFlashConfig>();
	TestTrue(TEXT("an explicit config wins"), UVBHitFlashSettings::ResolveConfig(Explicit) == Explicit);

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());

	ACharacter* Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("victim"), Victim))
	{
		return false;
	}

	UVBHitFlashComponent* Flash = NewObject<UVBHitFlashComponent>(Victim);
	Victim->AddInstanceComponent(Flash);
	Flash->RegisterComponent();

	Flash->TriggerFlash(VBHitFlashTests::WithHit(Victim->GetActorLocation() + FVector(0.0, 0.0, 40.0)));
	TestNotNull(TEXT("an unwired component flashes"), Victim->GetMesh()->GetOverlayMaterial());
	return true;
}

#endif
