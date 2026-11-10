#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEnemySetup.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// vb.Combat.EnemyHealthScale - 1 과 잘못된 값은 체력을 그대로 둔다.
//   실제 배수는 MaxHealth 와 Health 를 함께 옮긴다 - 아니면 Health 가 새 최대치 아래로 잘려 다친 채 나온다.
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyDevHealthScaleTest, "VB.Unit.Enemy.DevHealthScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyDevHealthScaleTest::RunTest(const FString& Parameters)
{
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

	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
	Pawn->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Pawn, Pawn);
	UVBHealthAttributeSet* Health = NewObject<UVBHealthAttributeSet>(Pawn);
	Health->InitMaxHealth(100.0f);
	Health->InitHealth(100.0f);
	ASC->AddAttributeSetSubobject(Health);

	const auto Max = [ASC]() { return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute()); };
	const auto Cur = [ASC]() { return ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()); };

	VBEnemySetup::ApplyDevHealthScale(ASC, 1.0f);
	TestEqual(TEXT("scale 1 keeps max health"), Max(), 100.0f);
	TestEqual(TEXT("scale 1 keeps health"), Cur(), 100.0f);
	for (const float Bad : {0.0f, -2.0f, NAN})
	{
		VBEnemySetup::ApplyDevHealthScale(ASC, Bad);
	}

	TestEqual(TEXT("zero, negative and NaN scales keep max health"), Max(), 100.0f);
	TestEqual(TEXT("zero, negative and NaN scales keep health"), Cur(), 100.0f);
	VBEnemySetup::ApplyDevHealthScale(nullptr, 5.0f); // No ASC: nothing to do, no crash.

	VBEnemySetup::ApplyDevHealthScale(ASC, 5.0f);
	TestEqual(TEXT("scale 5 multiplies max health"), Max(), 500.0f);
	TestEqual(TEXT("scale 5 multiplies health too, so the enemy spawns at full"), Cur(), 500.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 40.0f);
	VBEnemySetup::ApplyDevHealthScale(ASC, 5.0f);
	TestEqual(TEXT("partial health scales max"), Max(), 500.0f);
	TestEqual(TEXT("partial health keeps its ratio"), Cur(), 200.0f);
	return true;
}
#endif
