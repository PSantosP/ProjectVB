#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBStrideScale.h"
#include "Enemy/VBEnemyMovementRules.h"
#include "VBCombatAuditTestAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyMovementRulesTest, "VB.Unit.Enemy.MovementRules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyMovementRulesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("walk speed scales"), VBEnemyMovementRules::CalculateGaitSpeed(EVBGait::Walk, 150.0f, 400.0f, 2.0f), 300.0f);
	TestEqual(TEXT("run speed scales"), VBEnemyMovementRules::CalculateGaitSpeed(EVBGait::Run, 150.0f, 400.0f, 2.0f), 800.0f);
	TestEqual(TEXT("sprint folds into run"), VBEnemyMovementRules::CalculateGaitSpeed(EVBGait::Sprint, 150.0f, 400.0f, 2.0f), 800.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyMovementBodyTest, "VB.Unit.Enemy.MovementBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyMovementBodyTest::RunTest(const FString& Parameters)
{
	// 이미 있는 구체 fixture 를 쓴다 - 추상 할당, 월드 시작, 새 제품 접근이 없다
	const TStrongObjectPtr<AVBCombatAuditTestEnemy> Enemy(NewObject<AVBCombatAuditTestEnemy>(GetTransientPackage()));
	if (!TestNotNull(TEXT("concrete enemy body"), Enemy.Get()))
	{
		return false;
	}

	UVBEnemyConfig* Config = NewObject<UVBEnemyConfig>(Enemy.Get());
	Config->WalkSpeed = 150.0f;
	Config->RunSpeed = 400.0f;
	Enemy->Configure(Config);
	Enemy->GetMesh()->SetRelativeScale3D(FVector(2.0));
	Enemy->ApplyGait(EVBGait::Sprint);
	TestEqual(TEXT("body sprint uses run speed and mesh stride"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 800.0f);
	TestEqual(TEXT("body retains accepted gait"), Enemy->GetGait(), EVBGait::Sprint);
	Enemy->GetMesh()->SetRelativeScale3D(FVector::ZeroVector);
	TestEqual(TEXT("body zero mesh scale uses stride fallback"), VBStrideScale::Resolve(Enemy.Get()), 1.0f);
	Enemy->ApplyGait(EVBGait::Walk);
	TestEqual(TEXT("body fallback walk speed"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 150.0f);
	return true;
}
#endif
