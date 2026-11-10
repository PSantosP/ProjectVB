#include "Misc/AutomationTest.h"
#include "Enemy/VBEnemyBase.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBRagdollConfig.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKnightRagdollBindingTest, "VB.Unit.Ragdoll.KnightShippedBindings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBKnightRagdollBindingTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : {TEXT("Infected"), TEXT("InfectedHardened"), TEXT("Soldier"), TEXT("SoldierCombo")})
	{
		const FString Path = FString::Printf(TEXT("/Game/VB/Blueprints/Enemy/BP_Enemy%s.BP_Enemy%s_C"), Name, Name);
		const UClass* Class = LoadClass<AVBEnemyBase>(nullptr, *Path);
		const AVBEnemyBase* Enemy = Class ? Class->GetDefaultObject<AVBEnemyBase>() : nullptr;
		if (!TestNotNull(Path, Enemy)) { continue; }
		const UVBEnemyConfig* EnemyConfig = Enemy->GetEnemyConfig();
		if (!TestNotNull(FString(Name) + TEXT(" enemy config"), EnemyConfig)) { continue; }
		const UVBRagdollConfig* Config = EnemyConfig->PoweredRagdoll;
		const USkeletalMeshComponent* Mesh = Enemy->GetMesh();
		if (!TestNotNull(FString(Name) + TEXT(" powered config"), Config) || !TestNotNull(TEXT("mesh"), Mesh)) { continue; }
		const UPhysicsAsset* Physics = Mesh->GetPhysicsAsset();
		TestTrue(FString(Name) + TEXT(" physics matches authored powered contract"), Physics && Physics == Config->ExpectedPhysicsAsset);
		if (Physics)
		{
			TestTrue(FString(Name) + TEXT(" required spine body exists"), Physics->FindBodyIndex(Config->SpineBody) != INDEX_NONE);
			TestTrue(FString(Name) + TEXT(" required spine bone exists"), Mesh->GetBoneIndex(Config->SpineBody) != INDEX_NONE);
		}
	}
	return true;
}
#endif
