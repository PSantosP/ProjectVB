// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimMontage.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBComboSelectionPolicy.h"
#include "Data/VBSwingCombatProfile.h"
#include "GameplayTagContainer.h"

namespace VBFighterManualMovesTest
{
	// FabFighter 추가 동작. 묶음마다 줄을 더한다.
	struct FExpectedMove
	{
		const TCHAR* Montage;
		const TCHAR* Shape; // 판정 노티 하나의 모양 태그
		const TCHAR* CombatProfile; // 판정 노티의 서버 전투 프로필 이름, 없으면 nullptr
		float ReachMax;
		float StopCm;
		EVBComboRole Role; // opener band
	};
	const FExpectedMove Moves[] = {
		{ TEXT("AM_Fighter_Attack04"), TEXT("Melee.Shape.FootR"), nullptr, 180.0f, 120.0f, EVBComboRole::Neutral }, // A1a 옆차기
		{ TEXT("AM_Fighter_Attack02"), TEXT("Melee.Shape.HandR"), TEXT("DA_SwingCombat_FighterLauncher"), 260.0f, 85.0f, EVBComboRole::Neutral }, // A1a 띄우는 주먹
		{ TEXT("AM_Fighter_Attack01"), TEXT("Melee.Shape.HandR"), nullptr, 120.0f, 90.0f, EVBComboRole::Close }, // A1b 크게 휘두르는 주먹
		{ TEXT("AM_Fighter_Attack05"), TEXT("Melee.Shape.FootR"), TEXT("DA_SwingCombat_FighterFinisher"), 110.0f, 95.0f, EVBComboRole::Close }, // A1b 올려차기
	};
	// 사거리, 멈춤은 다른 데서 보는 Close opener - 여기서는 band 만 고정한다.
	const TCHAR* const CloseCombos[] = { TEXT("AM_Fighter_Combo03"), TEXT("AM_Fighter_Combo04"), TEXT("AM_Fighter_Combo05") };

	// 실제 자산에서 Montage 이름이 같은 변형 번호. 없으면 INDEX_NONE.
	int32 FindVariant(const UVBAttackConfig& Config, const TCHAR* Montage)
	{
		for (int32 i = 0; i < Config.GetMontageVariantCount(); ++i)
		{
			const UAnimMontage* M = Config.ResolveMontageVariant(i).Montage;
			if (M && M->GetName() == Montage)
			{
				return i;
			}
		}

		return INDEX_NONE;
	}

	// 판정 노티가 하나이고 모양, 전투 프로필이 기대와 같다.
	void CheckTraceWindow(FAutomationTestBase& Test, const UAnimMontage& Montage, const TCHAR* Shape, const TCHAR* CombatProfile)
	{
		int32 TraceWindows = 0;
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			const UVBANS_AttackTraceWindow* Trace = Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass);
			if (!Trace)
			{
				continue;
			}

			++TraceWindows;
			Test.TestTrue(*FString::Printf(TEXT("%s hits with %s"), *Montage.GetName(), Shape),
				Trace->Shapes.HasTagExact(FGameplayTag::RequestGameplayTag(Shape)));
			const FString Combat = Trace->SwingCombatProfile ? Trace->SwingCombatProfile->GetName() : FString();
			Test.TestEqual(*FString::Printf(TEXT("%s combat profile"), *Montage.GetName()), Combat,
				FString(CombatProfile ? CombatProfile : TEXT("")));
		}

		Test.TestEqual(*FString::Printf(TEXT("%s has one hit window"), *Montage.GetName()), TraceWindows, 1);
	}
}

// 추가 동작은 band opener 다 - 실제 자산의 행 값, band, 1배 재생, 판정 창 모양을 고정하고,
//   같은 거리에서 거듭 시작하면 band 를 돌아 모든 행이 나오는지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterManualMovesTest, "VB.Unit.Combat.FighterOpenerRows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFighterManualMovesTest::RunTest(const FString& Parameters)
{
	using namespace VBFighterManualMovesTest;
	const UVBAttackConfig* Light = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"));
	if (!TestNotNull(TEXT("DA_Attack_Fighter_Light"), Light))
	{
		return false;
	}

	const TArray<FVBComboCandidate> Candidates = Light->GetComboCandidates();

	TArray<int32> NewIndices;
	for (const FExpectedMove& Move : Moves)
	{
		const int32 Index = FindVariant(*Light, Move.Montage);
		if (!TestTrue(*FString::Printf(TEXT("%s is a variant"), Move.Montage), Index != INDEX_NONE))
		{
			continue;
		}

		NewIndices.Add(Index);
		const FVBAttackMontageVariant Variant = Light->ResolveMontageVariant(Index);
		TestEqual(*FString::Printf(TEXT("%s band"), Move.Montage), Variant.Role, Move.Role);
		TestEqual(*FString::Printf(TEXT("%s reach max"), Move.Montage), Variant.TargetReach.MaxDistance, Move.ReachMax, 0.01f);
		TestEqual(*FString::Printf(TEXT("%s stop"), Move.Montage), Light->GetVariantApproachOffset(Index, 1.0f), Move.StopCm, 0.01f);
		// 명세는 클립 원래 템포(1배) - 공용 배율(MontagePlayRateScale)은 그대로 두고 이 행만 보정한다.
		TestEqual(*FString::Printf(TEXT("%s plays at 1x"), Move.Montage), Light->MontagePlayRateScale * Variant.PlayRateScale, 1.0f, 1e-4f);

		CheckTraceWindow(*this, *Variant.Montage, Move.Shape, Move.CombatProfile);
	}

	for (const TCHAR* Combo : CloseCombos)
	{
		const int32 Index = FindVariant(*Light, Combo);
		if (!TestTrue(*FString::Printf(TEXT("%s is a variant"), Combo), Index != INDEX_NONE))
		{
			continue;
		}

		NewIndices.Add(Index);
		TestEqual(*FString::Printf(TEXT("%s band"), Combo), Light->ResolveMontageVariant(Index).Role, EVBComboRole::Close);
	}

	// 같은 거리에서 거듭 시작한다 - GA 가 적는 방식 그대로(ExecuteAbility -> NoteStart).
	const UVBComboSelectionPolicy* Policy = Light->ComboSelection;
	if (!TestNotNull(TEXT("light combo selection policy"), Policy))
	{
		return false;
	}

	TSet<int32> Started;
	bool bCloseRotates = false;
	bool bNeutralRotates = false;
	for (float Distance = 0.0f; Distance <= 500.0f; Distance += 10.0f)
	{
		FVBComboSelectContext Context;
		Context.bHasTarget = true;
		Context.DistanceCm = Distance;
		FVBComboStartMemory Memory;
		TSet<int32> AtDistance;
		for (int32 Press = 0; Press < Candidates.Num(); ++Press)
		{
			const FVBComboChoice Choice = UVBComboSelectionPolicy::SelectVariant(Context, Candidates, Policy->CloseMaxDistance,
				Policy->FarMinDistance,
				Memory, Light);
			if (Choice.Variant == INDEX_NONE)
			{
				break;
			}

			Memory.NoteStart(Light, Choice.Variant, Light->ResolveMontageVariant(Choice.Variant).Role);
			AtDistance.Add(Choice.Variant);
		}

		Started.Append(AtDistance);
		bCloseRotates |= Distance < Policy->CloseMaxDistance && AtDistance.Num() >= 2;
		bNeutralRotates |= Distance >= Policy->CloseMaxDistance && Distance < Policy->FarMinDistance && AtDistance.Num() >= 2;
	}
	// 질주는 Sprint 행을 지킨다 - 옮긴 행은 어떤 거리, 대상 유무에서도 질주 공격을 시작하면 안 된다.
	for (const bool bTarget : { false, true })
	{
		for (float Distance = 0.0f; Distance <= 500.0f; Distance += 10.0f)
		{
			FVBComboSelectContext Context;
			Context.bHasTarget = bTarget;
			Context.DistanceCm = Distance;
			Context.bSprinting = true;
			const FVBComboChoice Choice = UVBComboSelectionPolicy::SelectVariant(Context, Candidates, Policy->CloseMaxDistance,
				Policy->FarMinDistance,
				FVBComboStartMemory(), Light);
			if (NewIndices.Contains(Choice.Variant))
			{
				AddError(FString::Printf(TEXT("sprint picked moved row %d at %.0f cm (target %d)"), Choice.Variant, Distance, bTarget));
			}
		}
	}

	TestTrue(TEXT("a close distance starts two or more combos"), bCloseRotates);
	TestTrue(TEXT("a neutral distance starts two or more combos"), bNeutralRotates);
	for (const int32 Index : NewIndices)
	{
		TestTrue(*FString::Printf(TEXT("row %d starts at some distance"), Index), Started.Contains(Index));
	}

	for (const int32 Index : NewIndices)
	{
		FVBComboChoice Forced;
		TestTrue(*FString::Printf(TEXT("ForceCombo %d reaches the move"), Index),
			UVBComboSelectionPolicy::ApplyForcedVariant(Index, Candidates.Num(), true, Forced) && Forced.Variant == Index);
	}

	return true;
}

// 반격(A2): 반격 설정은 콤보 선택 정책이 없고 GA 의 순환 커서가 변형을 차례로 돈다 - 세 번째 반격은 행을 더하는 것만으로 일반 플레이(패링 -> 반격)에 나온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterRiposteVariantsTest, "VB.Unit.Combat.FighterRiposteVariants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFighterRiposteVariantsTest::RunTest(const FString& Parameters)
{
	using namespace VBFighterManualMovesTest;
	const UVBAttackConfig* Riposte = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_Fighter_Riposte.DA_Attack_Fighter_Riposte"));
	if (!TestNotNull(TEXT("DA_Attack_Fighter_Riposte"), Riposte))
	{
		return false;
	}

	TestNull(TEXT("riposte has no combo selection policy (the cursor cycles)"), Riposte->ComboSelection.Get());
	TestEqual(TEXT("three riposte variants"), Riposte->GetMontageVariantCount(), 3);
	const int32 Index = FindVariant(*Riposte, TEXT("AM_Fighter_Riposte_R"));
	if (!TestTrue(TEXT("AM_Fighter_Riposte_R is a variant"), Index != INDEX_NONE))
	{
		return false;
	}

	const FVBAttackMontageVariant Variant = Riposte->ResolveMontageVariant(Index);
	TestEqual(TEXT("right counter stop"), Riposte->GetVariantApproachOffset(Index, 1.0f), 85.0f, 0.01f);
	TestEqual(TEXT("right counter plays at 1x"), Riposte->MontagePlayRateScale * Variant.PlayRateScale, 1.0f, 1e-4f);
	CheckTraceWindow(*this, *Variant.Montage, TEXT("Melee.Shape.HandR"), TEXT("DA_SwingCombat_FighterRiposteFinal"));
	// GA 의 커서는 끝없이 오르고 ResolveMontageVariant 가 변형 수로 감싼다 - 오른 반격은 세 번에 한 번씩 계속 나온다.
	for (int32 Cursor = Index; Cursor < Index + 9; Cursor += 3)
	{
		TestEqual(*FString::Printf(TEXT("cursor %d plays the right counter"), Cursor),
			Riposte->ResolveMontageVariant(Cursor).Montage.Get(), Variant.Montage.Get());
	}

	return true;
}
#endif
