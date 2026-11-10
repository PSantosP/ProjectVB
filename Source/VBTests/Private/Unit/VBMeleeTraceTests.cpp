#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Data/VBAttackConfig.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyEndDataContext.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Animation/VBANS_ComboWindow.h"
#include "Data/VBSwingCombatProfile.h"
#include "Animation/VBANS_WeaponTrail.h"
#include "AbilitySystem/VBGameplayTags.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceSamplingTest, "VB.Combat.MeleeTrace.Sampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceSamplingTest::RunTest(const FString& Parameters)
{
	FVBResolvedMeleeShape Shape;
	Shape.BaseLocal = FVector::ZeroVector;
	Shape.TipLocal = FVector(100.0, 0.0, 0.0);
	Shape.Radius = 10.0f;
	const FTransform Start = FTransform::Identity;
	TestEqual(TEXT("A stationary first pose still receives a query"),
		UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Start, 16), 1);
	const FTransform Turn(FQuat(FVector::UpVector, FMath::DegreesToRadians(60.0f)));
	const int32 Steps = UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Turn, 16);
	// 반경 100cm 의 60도 호는 약 104.72cm. 끝점 사이 100cm 만 재던 10단계로는 호 간격이 10cm 를 넘는다.
	TestTrue(TEXT("The sampled arc fits within the trace radius"), FMath::DegreesToRadians(60.0f) * 100.0f / Steps <= Shape.Radius);
	TestTrue(TEXT("Long blade rotation uses more than endpoint chord sampling"), Steps > 10);
	const FTransform LargeTurn(FQuat(FVector::UpVector, PI));
	TestEqual(TEXT("Query work remains bounded for a large frame jump"),
		UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, LargeTurn, 16), 16);
	const FTransform Translated(FVector(45.0, 0.0, 0.0));
	TestEqual(TEXT("Translation spacing follows radius"), UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Translated, 16), 5);
	const FTransform Scaled(FQuat::Identity, FVector::ZeroVector, FVector(1.25));
	TestTrue(TEXT("Scaling without bone translation also needs intermediate queries"),
		UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Scaled, 16) > 1);
	return true;
}

// 휘두름마다의 적중 폭. 월드 없는 순수 검사라 ApplicationContext 로 headless 실행이 덮는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceRadiusScaleTest, "VB.Unit.MeleeTrace.RadiusScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeTraceRadiusScaleTest::RunTest(const FString& Parameters)
{
	TestNearlyEqual(TEXT("Scale 2 doubles the radius"), UVBMeleeTraceComponent::ScaleShapeRadius(10.0f, 2.0f), 20.0f, 1e-4f);
	TestEqual(TEXT("Scale 1 keeps the radius"), UVBMeleeTraceComponent::ScaleShapeRadius(10.0f, 1.0f), 10.0f);
	TestEqual(TEXT("Zero scale is read as 1, the hit is not erased"), UVBMeleeTraceComponent::ScaleShapeRadius(10.0f, 0.0f), 10.0f);
	TestEqual(TEXT("Negative scale is read as 1"), UVBMeleeTraceComponent::ScaleShapeRadius(10.0f, -3.0f), 10.0f);
	TestEqual(TEXT("NaN scale is read as 1"), UVBMeleeTraceComponent::ScaleShapeRadius(10.0f, NAN), 10.0f);

	FVBSwingCombatScales Scales;
	TestEqual(TEXT("Radius scale default is 1"), Scales.TraceRadiusScale, 1.0f);
	Scales.TraceRadiusScale = 2.0f;
	TestTrue(TEXT("Radius scale alone is an effect"), Scales.HasAnyEffect());

#if WITH_EDITOR
	UVBSwingCombatProfile* Profile = NewObject<UVBSwingCombatProfile>();
	Profile->Scales.TraceRadiusScale = 0.0f;
	FDataValidationContext ZeroContext;
	TestEqual(TEXT("Zero radius scale fails validation"), Profile->IsDataValid(ZeroContext), EDataValidationResult::Invalid);
	Profile->Scales.TraceRadiusScale = 0.05f;
	FDataValidationContext TinyContext;
	TestEqual(TEXT("Radius scale below the 0.1 clamp fails validation"), Profile->IsDataValid(TinyContext), EDataValidationResult::Invalid);
	Profile->Scales.TraceRadiusScale = 2.0f;
	FDataValidationContext GoodContext;
	TestNotEqual(TEXT("Radius scale 2 alone passes validation"), Profile->IsDataValid(GoodContext), EDataValidationResult::Invalid);
#endif
	return true;
}

// 단일 태그 -> 태그 셋 이전. 월드 없는 순수 객체라 ApplicationContext 로 headless 실행이 덮는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceShapeMigrationTest, "VB.Unit.MeleeTrace.ShapeMigration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeTraceShapeMigrationTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer Weapon(VBGameplayTags::Melee_Shape_Weapon);
	const FGameplayTagContainer FootL(VBGameplayTags::Melee_Shape_FootL);

	UVBANS_AttackTraceWindow* Old = NewObject<UVBANS_AttackTraceWindow>();
	Old->Shape_DEPRECATED = VBGameplayTags::Melee_Shape_FootL;
	Old->MigrateLegacyShape();
	TestTrue(TEXT("A saved single tag becomes the only shape"), Old->Shapes == FootL);
	TestFalse(TEXT("The old field is cleared after the move"), Old->Shape_DEPRECATED.IsValid());

	UVBANS_AttackTraceWindow* Default = NewObject<UVBANS_AttackTraceWindow>();
	Default->MigrateLegacyShape();
	TestTrue(TEXT("A notify that kept the old default (nothing saved) reads Weapon"), Default->Shapes == Weapon);

	UVBANS_AttackTraceWindow* Cleared = NewObject<UVBANS_AttackTraceWindow>();
	Cleared->Shapes.Reset();
	Cleared->MigrateLegacyShape();
	TestTrue(TEXT("An explicitly emptied set is not refilled (runtime warns instead)"), Cleared->Shapes.IsEmpty());

	UVBANS_WeaponTrail* OldTrail = NewObject<UVBANS_WeaponTrail>();
	OldTrail->Shape_DEPRECATED = VBGameplayTags::Melee_Shape_FootL;
	OldTrail->MigrateLegacyShape();
	TestTrue(TEXT("Trail notify migrates the same way"), OldTrail->Shapes == FootL && !OldTrail->Shape_DEPRECATED.IsValid());

	FGameplayTagContainer EventTags(VBGameplayTags::Melee_Window);
	EventTags.AddTag(VBGameplayTags::Melee_Shape_FootL);
	EventTags.AddTag(VBGameplayTags::Melee_Shape_FootR);
	const FGameplayTagContainer Read = UVBMeleeTraceComponent::ShapesFromEventTags(EventTags);
	TestEqual(TEXT("Event tags yield both feet and not the window marker"), Read.Num(), 2);
	TestTrue(TEXT("Event tags keep FootR"), Read.HasTagExact(VBGameplayTags::Melee_Shape_FootR));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceNotifyEndTest, "VB.Combat.MeleeTrace.NotifyEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceNotifyEndTest::RunTest(const FString& Parameters)
{
	FAnimNotifyEventReference Interrupted;
	TestFalse(TEXT("An end without a natural-end context cannot add final damage"), UAnimNotifyLibrary::NotifyStateReachedEnd(Interrupted));
	FAnimNotifyEventReference Cancelled;
	Cancelled.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(false);
	TestFalse(TEXT("Explicit interruption cannot add final damage"), UAnimNotifyLibrary::NotifyStateReachedEnd(Cancelled));
	FAnimNotifyEventReference Completed;
	Completed.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
	TestTrue(TEXT("Natural completion retains the last pose query"), UAnimNotifyLibrary::NotifyStateReachedEnd(Completed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceAssetsTest, "VB.Combat.MeleeTrace.AuthoredWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceAssetsTest::RunTest(const FString& Parameters)
{
	const UVBAttackConfig* Config = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_BigSword_Light.DA_Attack_BigSword_Light"));
	if (!TestNotNull(TEXT("Greatsword attack config"), Config))
	{
		return false;
	}

	TestFalse(TEXT("Repeated input preserves greatsword follow-through"), Config->bChainAtWindowOpen);
	for (const TCHAR* Name : {TEXT("AM_LightAttack"), TEXT("AM_BigSword_LightAttack"), TEXT("AM_Fighter_LightAttack")})
	{
		const FString Path = FString::Printf(TEXT("/Game/VB/Animations/Montage/%s.%s"), Name, Name);
		const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
		if (!TestNotNull(Name, Montage))
		{
			continue;
		}

		TArray<const FAnimNotifyEvent*> Traces;
		TArray<const FAnimNotifyEvent*> Combos;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UVBANS_AttackTraceWindow>())
			{
				TestTrue(TEXT("Trace window reads finalized bones through queued notifies"),
					Event.MontageTickType == EMontageNotifyTickType::Queued);
				Traces.Add(&Event);
			}

			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UVBANS_ComboWindow>())
			{
				Combos.Add(&Event);
			}
		}

		Traces.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTriggerTime() < B.GetTriggerTime(); });
		Combos.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTriggerTime() < B.GetTriggerTime(); });
		if (FCString::Strcmp(Name, TEXT("AM_BigSword_LightAttack")) == 0)
		{
			TestEqual(TEXT("All seven greatsword swings have a trace window"), Traces.Num(), 7);
			TestEqual(TEXT("All seven greatsword swings have a combo window"), Combos.Num(), 7);
			if (Traces.Num() == 7 && Combos.Num() == 7)
			{
				for (int32 Swing = 0; Swing < 7; ++Swing)
				{
					TestTrue(FString::Printf(TEXT("Swing %d cannot chain before its trace finishes"), Swing + 1),
						Combos[Swing]->GetTriggerTime() + Combos[Swing]->GetDuration() >= Traces[Swing]->GetTriggerTime() + Traces[Swing]->GetDuration());
				}
			}
		}
		else
		{
			TestEqual(TEXT("Every katana and fist swing retains its trace window"), Traces.Num(), 5);
		}
	}

	return true;
}

#endif
