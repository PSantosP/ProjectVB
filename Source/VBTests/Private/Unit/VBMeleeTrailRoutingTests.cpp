#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Animation/VBANS_WeaponTrail.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Data/VBMeleeTraceShapeSet.h"
#include "Data/VBWeaponVisualConfig.h"
#include "Data/VBSwingTrailProfile.h"
#include "AbilitySystem/VBGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTrailRoutingTest, "VB.Unit.MeleeTrail.AttackingBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTrailRoutingTest::RunTest(const FString& Parameters)
{
	const UVBMeleeTraceShapeSet* Shapes = LoadObject<UVBMeleeTraceShapeSet>(nullptr,
		TEXT("/Game/VB/Data/MeleeTrace/DA_MeleeShapes_Player.DA_MeleeShapes_Player"));
	const UVBWeaponVisualConfig* Visuals = LoadObject<UVBWeaponVisualConfig>(nullptr,
		TEXT("/Game/VB/Weapons/DA_WeaponVisuals.DA_WeaponVisuals"));
	const UVBSwingTrailProfile* BodyProfile = LoadObject<UVBSwingTrailProfile>(nullptr,
		TEXT("/Game/VB/Data/MeleeTrace/DA_SwingTrail_Unarmed.DA_SwingTrail_Unarmed"));
	if (!TestNotNull(TEXT("Shapes"), Shapes) || !TestNotNull(TEXT("Visuals"), Visuals) || !TestNotNull(TEXT("Body trail"), BodyProfile))
	{
		return false;
	}

	TestNotNull(TEXT("Body ribbon"), BodyProfile->TrailMaterial.Get());
	TestNotNull(TEXT("Body distortion"), BodyProfile->DistortionMaterial.Get());
	TestNotNull(TEXT("Body whoosh"), BodyProfile->WhooshSound.Get());
	const FGameplayTag BodyShapes[] = {VBGameplayTags::Melee_Shape_HandL, VBGameplayTags::Melee_Shape_HandR, VBGameplayTags::Melee_Shape_FootL, VBGameplayTags::Melee_Shape_FootR};
	for (const EVBWeaponType Weapon : {EVBWeaponType::Blade, EVBWeaponType::BigSword, EVBWeaponType::Fighter})
	{
		for (const FGameplayTag ShapeTag : BodyShapes)
		{
			FVBResolvedMeleeShape Resolved;
			if (!TestTrue(TEXT("Body shape resolves with every equipped weapon"),
				UVBMeleeTraceComponent::ResolveShapeFromData(Shapes, ShapeTag, Visuals, Weapon, Resolved)))
			{
				continue;
			}

			TestTrue(TEXT("Body attack uses body trail, not equipped blade"), Resolved.TrailProfile.Get() == BodyProfile);
			TestTrue(TEXT("Body anchor is authored shape bone"), Resolved.Bone == Shapes->Shapes.FindChecked(ShapeTag).Bone);
		}
	}

	for (const TCHAR* Path : {TEXT("/Game/VB/Animations/Montage/AM_LightAttack.AM_LightAttack"),
		TEXT("/Game/VB/Animations/Montage/AM_Fighter_LightAttack.AM_Fighter_LightAttack"),
		TEXT("/Game/VB/Animations/Montage/AM_BigSword_LightAttack.AM_BigSword_LightAttack")})
	{
		const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Path);
		if (!TestNotNull(Path, Montage) || !TestTrue(TEXT("Montage slot exists"), Montage->SlotAnimTracks.Num() > 0))
		{
			continue;
		}

		int32 CheckedSwings = 0;
		for (const FAnimSegment& Segment : Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
		{
			const UVBANS_AttackTraceWindow* Window = nullptr;
			const UVBANS_WeaponTrail* Trail = nullptr;
			int32 Windows = 0;
			int32 Trails = 0;
			for (const FAnimNotifyEvent& Event : Montage->Notifies)
			{
				if (Event.GetTriggerTime() < Segment.StartPos || Event.GetTriggerTime() >= Segment.StartPos + Segment.GetLength())
				{
					continue;
				}

				if (const auto* Found = Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass))
				{
					Window = Found;
					++Windows;
				}

				if (const auto* Found = Cast<UVBANS_WeaponTrail>(Event.NotifyStateClass))
				{
					Trail = Found;
					++Trails;
				}
			}

			TestEqual(TEXT("One attack window per authored swing"), Windows, 1);
			TestEqual(TEXT("One trail per authored swing"), Trails, 1);
			if (Window && Trail)
			{
				if (Montage->GetName() == TEXT("AM_LightAttack") && (CheckedSwings == 2 || CheckedSwings == 3))
				{
					TestTrue(TEXT("Blade third and fourth kicks use the right leg"),
						Window->Shapes == FGameplayTagContainer(VBGameplayTags::Melee_Shape_FootR));
				}

				TestTrue(*FString::Printf(TEXT("%s swing %d same attacking part"), *Montage->GetName(), CheckedSwings),
					Window->Shapes == Trail->Shapes);
				if (!Trail->Shapes.HasTagExact(VBGameplayTags::Melee_Shape_Weapon))
				{
					TestTrue(TEXT("Body swing cannot override with blade profile"), !Trail->Profile || Trail->Profile == BodyProfile);
				}

				++CheckedSwings;
			}
		}

		TestTrue(TEXT("Actual swings checked"), CheckedSwings > 0);
	}

	return true;
}
#endif
