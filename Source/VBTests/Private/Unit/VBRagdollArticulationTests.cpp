// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Data/VBRagdollConfig.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRagdollArticulationTest, "VB.Unit.Ragdoll.PlayerArticulation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBRagdollArticulationTest::RunTest(const FString& Parameters)
{
	const UVBRagdollConfig* Config = LoadObject<UVBRagdollConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Ragdoll_PlayerUEFN.DA_Ragdoll_PlayerUEFN"));
	if (!TestNotNull(TEXT("player config"), Config) || !TestNotNull(TEXT("physics asset"), Config->ExpectedPhysicsAsset.Get()))
	{
		return false;
	}
	int32 LockedJoints = 0;
	int32 MissingProfiles = 0;
	int32 TestedJoints = 0;
	for (const UPhysicsConstraintTemplate* Joint : Config->ExpectedPhysicsAsset->ConstraintSetup)
	{
		if (!TestNotNull(TEXT("constraint"), Joint))
		{
			return false;
		}
		MissingProfiles += !Config->ConstraintProfile.IsNone() && !Joint->ContainsConstraintProfile(Config->ConstraintProfile);
		const FConstraintProfileProperties& Profile = Joint->GetConstraintProfilePropertiesOrDefault(Config->ConstraintProfile);
		LockedJoints += Profile.ConeLimit.Swing1Motion == ACM_Locked && Profile.ConeLimit.Swing2Motion == ACM_Locked &&
			Profile.TwistLimit.TwistMotion == ACM_Locked;
		++TestedJoints;
	}
	TestTrue(TEXT("shipped physics asset has joints"), TestedJoints > 0);
	TestEqual(TEXT("every joint resolves the selected profile without fallback"), MissingProfiles, 0);
	TestEqual(TEXT("shipped player joints permit articulation"), LockedJoints, 0);
	const UVBRagdollConfig* EnemyConfig = LoadObject<UVBRagdollConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Ragdoll_Manny.DA_Ragdoll_Manny"));
	if (!TestNotNull(TEXT("enemy config"), EnemyConfig) ||
		!TestNotNull(TEXT("enemy physics asset"), EnemyConfig->ExpectedPhysicsAsset.Get()))
	{
		return false;
	}
	const TObjectPtr<UPhysicsConstraintTemplate>* Spine = EnemyConfig->ExpectedPhysicsAsset->ConstraintSetup.FindByPredicate(
		[EnemyConfig](const UPhysicsConstraintTemplate* Joint)
		{
			return Joint && Joint->DefaultInstance.ConstraintBone1 == EnemyConfig->SpineBody;
		});
	if (!TestNotNull(TEXT("enemy spine joint"), Spine))
	{
		return false;
	}
	TestTrue(TEXT("enemy spine resolves its selected profile"), EnemyConfig->ConstraintProfile.IsNone() ||
		(*Spine)->ContainsConstraintProfile(EnemyConfig->ConstraintProfile));
	const FConstraintProfileProperties& EnemyProfile = (*Spine)->GetConstraintProfilePropertiesOrDefault(EnemyConfig->ConstraintProfile);
	TestFalse(TEXT("enemy spine is not completely welded"),
		EnemyProfile.ConeLimit.Swing1Motion == ACM_Locked && EnemyProfile.ConeLimit.Swing2Motion == ACM_Locked &&
		EnemyProfile.TwistLimit.TwistMotion == ACM_Locked);
	return true;
}
#endif
