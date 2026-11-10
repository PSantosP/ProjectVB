#include "Misc/AutomationTest.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Data/VBComboSelectionPolicy.h"
#include "Data/VBAttackConfig.h"
#include "AbilitySystem/VBAttackActivationData.h"
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/Abilities/VBGA_Riposte.h"
#include "Enemy/VBGA_EnemyMeleeAttack.h"
#include "Animation/AnimMontage.h"
#include "Debug/VBCombatReadoutSubsystem.h"
#include "GameFramework/Character.h"

// 새 콤보를 무엇으로 시작할지: 거리 자격 -> 역할 순. 대역 목록은 자격 있는 변형을 차례로 돌려 모든 콤보가 나오고, Sprint, 일반 공격은 첫 것을 쓴다.
//   반대 대조: 기억을 무시하고 첫 것을 고르면 회전 순서 줄이 실패한다.
//   아래 변형 표는 Fighter 데이터(Manual 줄 포함)이고, 줄이 따로 말하지 않으면 빈 기억으로 쓴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComboSelectionPolicyTest, "VB.Unit.Combat.ComboSelectionPolicy",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBComboSelectionPolicyTest::RunTest(const FString& Parameters)
{
	using R = EVBComboRole;
	const float Close = 150.0f, Far = 280.0f;
	const auto Candidate = [](R Role, float Min, float Max) { return FVBComboCandidate{Role, {Min, Max}}; };
	// 변형 번호: 0 C02 Neutral(기본) / 1 C01 Close / 2 C03 Manual / 3 C04 Manual / 4 C05 Manual / 5 Run01 Sprint / 6 Run02 Sprint
	const TArray<FVBComboCandidate> Fighter = {
		Candidate(R::Neutral, 0, 280), Candidate(R::Close, 0, 150), Candidate(R::Manual, 0, 100),
		Candidate(R::Manual, 0, 120), Candidate(R::Manual, 0, 120), Candidate(R::Sprint, 220, 340), Candidate(R::Sprint, 340, 400)};
	const UVBAttackConfig* ConfigA = NewObject<UVBAttackConfig>(GetTransientPackage());
	const UVBAttackConfig* ConfigB = NewObject<UVBAttackConfig>(GetTransientPackage());
	const auto PickWith = [&](const FVBComboStartMemory& Memory, const UVBAttackConfig* Config, TConstArrayView<FVBComboCandidate> Set,
		bool bTarget, float Distance, bool bSprint)
	{
		FVBComboSelectContext Context;
		Context.bHasTarget = bTarget;
		Context.DistanceCm = Distance;
		Context.bSprinting = bSprint;
		return UVBComboSelectionPolicy::SelectVariant(Context, Set, Close, Far, Memory, Config);
	};
	const auto Pick = [&](TConstArrayView<FVBComboCandidate> Set, bool bTarget, float Distance, bool bSprint)
	{
		return PickWith(FVBComboStartMemory(), ConfigA, Set, bTarget, Distance, bSprint);
	};
	const auto Variant = [&](bool bTarget, float Distance, bool bSprint)
	{
		return Pick(Fighter, bTarget, Distance, bSprint).Variant;
	};

	// Close 대역: 늘 C01. Manual 변형은 거리 자격이 있어도 고르지 않는다.
	TestEqual(TEXT("<=100: C01"), Variant(true, 90.0f, false), 1);
	TestEqual(TEXT("100~120: C01"), Variant(true, 110.0f, false), 1);
	TestEqual(TEXT("120~150: C01"), Variant(true, 140.0f, false), 1);
	TestEqual(TEXT("Band edge 150 is close (C01)"), Variant(true, 150.0f, false), 1);
	// Neutral 대역과 그 너머.
	TestEqual(TEXT("150~280: neutral C02"), Variant(true, 200.0f, false), 0);
	TestEqual(TEXT("Band edge 280 is neutral and C02 reaches"), Variant(true, 280.0f, false), 0);
	const FVBComboChoice FarAway = Pick(Fighter, true, 320.0f, false);
	TestEqual(TEXT(">280 without far candidates: general attack C02"), FarAway.Variant, 0);
	TestFalse(TEXT(">280 general attack is not an in-reach choice"), FarAway.bTargetInReach);
	TestTrue(TEXT("In-band choice is in reach"), Pick(Fighter, true, 90.0f, false).bTargetInReach);
	TestEqual(TEXT("No target, not sprinting: neutral C02"), Variant(false, 0.0f, false), 0);
	TestTrue(TEXT("No target: nothing to reach"), Pick(Fighter, false, 0.0f, false).bTargetInReach);

	// Sprint: 번갈기가 아니라 대상 거리가 정한다.
	TestEqual(TEXT("Sprint 220~340: Run01"), Variant(true, 250.0f, true), 5);
	TestEqual(TEXT("Sprint 300: Run01"), Variant(true, 300.0f, true), 5);
	TestEqual(TEXT("Sprint edge 340: first qualified (Run01)"), Variant(true, 340.0f, true), 5);
	TestEqual(TEXT("Sprint 340~400: Run02"), Variant(true, 380.0f, true), 6);
	TestEqual(TEXT("Sprint but too close: distance branch (close)"), Variant(true, 90.0f, true), 1);
	const FVBComboChoice SprintTooFar = Pick(Fighter, true, 450.0f, true);
	TestEqual(TEXT("Sprint beyond 400: general attack C02"), SprintTooFar.Variant, 0);
	TestFalse(TEXT("Sprint beyond 400: not in reach"), SprintTooFar.bTargetInReach);
	TestEqual(TEXT("Sprint without target: first sprint move"), Variant(false, 0.0f, true), 5);

	// 빈 기억이면 자격 있는 첫 후보.
	const TArray<FVBComboCandidate> Overlap = {Candidate(R::Neutral, 0, 280), Candidate(R::Close, 0, 150), Candidate(R::Close, 0, 100)};
	const FVBComboChoice Repeat = Pick(Overlap, true, 90.0f, false);
	TestTrue(TEXT("Two close candidates, empty memory: the first, 2 qualified"), Repeat.Variant == 1 && Repeat.Qualified == 2);

	// 회전: 실행한 시작을 적고, 다음 누름은 그 목록의 다음 자격 줄을 고르며 끝에서 처음으로 돈다.
	//   변형 번호: 0 Close / 1 Neutral / 2 Far / 3 Neutral / 4 Sprint / 5 Neutral(0~200) / 6 Close
	const TArray<FVBComboCandidate> Rotating = {
		Candidate(R::Close, 0, 150), Candidate(R::Neutral, 0, 280), Candidate(R::Far, 280, 0), Candidate(R::Neutral, 0, 280),
		Candidate(R::Sprint, 0, 0), Candidate(R::Neutral, 0, 200), Candidate(R::Close, 0, 150)};
	const auto Starts = [&](FVBComboStartMemory& Memory, const UVBAttackConfig* Config, bool bTarget, float Distance, bool bSprint,
		int32 Presses)
	{
		TArray<int32> Out;
		for (int32 Press = 0; Press < Presses; ++Press)
		{
			const FVBComboChoice Choice = PickWith(Memory, Config, Rotating, bTarget, Distance, bSprint);
			Memory.NoteStart(Config, Choice.Variant, Rotating[Choice.Variant].Role);
			Out.Add(Choice.Variant);
		}

		return Out;
	};
	FVBComboStartMemory Neutral;
	TestEqual(TEXT("Three qualified Neutral rows [1,3,5]: 1, 3, 5, 1"), Starts(Neutral, ConfigA, true, 190.0f, false, 4), TArray<int32>({1, 3, 5, 1}));
	const FVBComboChoice AfterOne = PickWith(Neutral, ConfigA, Rotating, true, 190.0f, false);
	TestTrue(TEXT("A rotated pick says which list and after which start"),
		AfterOne.RotationRole == R::Neutral && AfterOne.RotationAfter == 1 && AfterOne.Variant == 3);

	FVBComboStartMemory Moved;
	Moved.NoteStart(ConfigA, 3, R::Neutral);
	TestEqual(TEXT("Last 3, qualified now [1,3] (target moved to 250): wraps to 1"),
		PickWith(Moved, ConfigA, Rotating, true, 250.0f, false).Variant, 1);
	Moved.NoteStart(ConfigA, 1, R::Neutral);
	TestEqual(TEXT("Last 1, qualified [1,3,5]: 3"), PickWith(Moved, ConfigA, Rotating, true, 190.0f, false).Variant, 3);
	Moved.NoteStart(ConfigA, 5, R::Neutral);
	TestEqual(TEXT("Last 5, qualified [1,3] (250): wraps to 1"), PickWith(Moved, ConfigA, Rotating, true, 250.0f, false).Variant, 1);

	FVBComboStartMemory Bands;
	TestEqual(TEXT("Close band rotates [0,6]: 0, 6, 0"), Starts(Bands, ConfigA, true, 90.0f, false, 3), TArray<int32>({0, 6, 0}));
	TestEqual(TEXT("Close starts did not move the Neutral list: still its first"),
		PickWith(Bands, ConfigA, Rotating, true, 190.0f, false).Variant, 1);
	TestEqual(TEXT("No target: the Neutral list rotates too"), Starts(Bands, ConfigA, false, 0.0f, false, 2), TArray<int32>({1, 3}));

	FVBComboStartMemory Other;
	Other.NoteStart(ConfigA, 1, R::Neutral);
	TestEqual(TEXT("Memory of config A is empty for config B (Select itself)"),
		PickWith(Other, ConfigB, Rotating, true, 190.0f, false).Variant, 1);
	TestEqual(TEXT("... and for the dodge resume read"), Other.VariantFor(ConfigB), INDEX_NONE);
	TestEqual(TEXT("Same config: the dodge resume reads the last start"), Other.VariantFor(ConfigA), 1);
	Other.NoteStart(ConfigB, 3, R::Neutral);
	TestEqual(TEXT("A start for B forgets A's band starts"), Other.LastStartFor(ConfigA, R::Neutral), INDEX_NONE);
	TestEqual(TEXT("Sprint and Manual never hold a rotation slot"), Other.LastStartFor(ConfigB, R::Sprint), INDEX_NONE);

	// 안 바뀐 갈래: sprint 첫 것 고르기와 닿지 않는 일반 공격은 기억을 무시한다.
	FVBComboStartMemory Kept;
	Kept.NoteStart(ConfigA, 5, R::Sprint);
	TestEqual(TEXT("Sprint after a Run01 start: Run01 again (first wins)"), PickWith(Kept, ConfigA, Fighter, false, 0.0f, true).Variant, 5);
	const TArray<FVBComboCandidate> TwoNeutral = {Candidate(R::Neutral, 0, 280), Candidate(R::Neutral, 0, 280)};
	FVBComboStartMemory Fallen;
	Fallen.NoteStart(ConfigA, 0, R::Neutral);
	const FVBComboChoice OutOfReach = PickWith(Fallen, ConfigA, TwoNeutral, true, 320.0f, false);
	TestTrue(TEXT("Out of reach after a Neutral start: the first general attack, no rotation"),
		OutOfReach.Variant == 0 && OutOfReach.Reason == EVBComboChoiceReason::NoneInReach && !OutOfReach.RotationRole.IsSet());

	// 대체 순서: 대역 역할 없음 -> 닿는 Neutral -> sprint, manual 아닌 첫 변형 -> 0.
	const TArray<FVBComboCandidate> NoClose = {Candidate(R::Neutral, 0, 280), Candidate(R::Far, 300, 0)};
	TestEqual(TEXT("Close band without close: reachable neutral"), Pick(NoClose, true, 100.0f, false).Variant, 0);
	TestEqual(TEXT("Far candidate with no upper limit"), Pick(NoClose, true, 900.0f, false).Variant, 1);
	const TArray<FVBComboCandidate> NoNeutral = {Candidate(R::Sprint, 0, 0), Candidate(R::Manual, 0, 0), Candidate(R::Close, 0, 100)};
	const FVBComboChoice Fallback = Pick(NoNeutral, true, 230.0f, false);
	TestEqual(TEXT("No neutral: first non-sprint, non-manual as general attack"), Fallback.Variant, 2);
	TestFalse(TEXT("That general attack is out of reach"), Fallback.bTargetInReach);
	TestEqual(TEXT("Only sprint and not sprinting: index 0"), Pick({Candidate(R::Sprint, 0, 0)}, true, 230.0f, false).Variant, 0);
	TestEqual(TEXT("No variants: index 0"), Pick({}, true, 230.0f, false).Variant, 0);

	// 개발 판독 화면이 찍는 이유는 실제로 탄 갈래여야 한다.
	using CR = EVBComboChoiceReason;
	const FVBComboChoice CloseChoice = Pick(Fighter, true, 90.0f, false);
	TestTrue(TEXT("Reason: band Close, only C01 qualifies (manual ones are not counted)"),
		CloseChoice.Reason == CR::Band && CloseChoice.Band == R::Close && CloseChoice.Qualified == 1);
	TestTrue(TEXT("Reason: sprint branch"), Pick(Fighter, true, 300.0f, true).Reason == CR::Sprint);
	TestTrue(TEXT("Reason: close band fell to neutral"), Pick(NoClose, true, 100.0f, false).Reason == CR::BandNeutral);
	TestTrue(TEXT("Reason: no target"), Pick(Fighter, false, 0.0f, false).Reason == CR::NoTarget);
	TestTrue(TEXT("Reason: target but none in reach"), FarAway.Reason == CR::NoneInReach && FarAway.Band == R::Far);
	TestTrue(TEXT("Reason: no candidates"), Pick({}, true, 230.0f, false).Reason == CR::NoCandidates);
	return true;
}

// 서버가 준비한 공격만 실행 원본이 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparedAttackTest, "VB.Unit.Combat.PreparedAttack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBPreparedAttackTest::RunTest(const FString& Parameters)
{
	// 입구: 클라의 기본 GAS 활성화 요청은 엔진 입구에서 거절된다 - 공격은 서버 누름 수락 -> 준비 경로로만 켜진다.
	const auto RejectsClientRequests = [](const UGameplayAbility* Ability)
	{
		return Ability && Ability->GetNetSecurityPolicy() == EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
	};
	TestTrue(TEXT("Light attack rejects client activation requests"), RejectsClientRequests(GetDefault<UVBGA_LightAttack>()));
	TestTrue(TEXT("Riposte rejects client activation requests"), RejectsClientRequests(GetDefault<UVBGA_Riposte>()));
	TestTrue(TEXT("Enemy melee rejects client activation requests"), RejectsClientRequests(GetDefault<UVBGA_EnemyMeleeAttack>()));

	// 실행 가능한 선택: 정책 있는 설정 + 그 설정의 변형 번호. 아니면 다른 몽타주로 대신하지 않는다.
	UVBAttackConfig* Config = NewObject<UVBAttackConfig>(GetTransientPackage());
	Config->AttackMontage = NewObject<UAnimMontage>(Config);
	FVBAttackMontageVariant Second;
	Second.Montage = NewObject<UAnimMontage>(Config);
	Config->MontageVariants.Add(Second);

	FVBAttackActivationData Data;
	Data.Config = Config;
	Data.VariantIndex = 1;
	TestFalse(TEXT("A config without a selection policy is never prepared"), Data.IsExecutable());

	Config->ComboSelection = NewObject<UVBComboSelectionPolicy>(Config);
	TestTrue(TEXT("Policy config and an in-range variant is executable"), Data.IsExecutable());
	Data.VariantIndex = 2;
	TestFalse(TEXT("Variant past the end is refused"), Data.IsExecutable());
	Data.VariantIndex = -1;
	TestFalse(TEXT("Negative variant is refused"), Data.IsExecutable());
	Data.VariantIndex = 0;
	Data.Config = nullptr;
	TestFalse(TEXT("Unresolved config (late or failed replication) is refused"), Data.IsExecutable());

	Config->MontageVariants.Reset();
	Data.Config = Config;
	TestTrue(TEXT("Only the base montage: index 0 is the one choice"), Data.IsExecutable());
	Config->AttackMontage = nullptr;
	TestFalse(TEXT("No montage at all: nothing to execute, refuse"), Data.IsExecutable());

	// 다음 타 섹션은 그 몽타주에 있어야 한다 - 한 구간짜리 질주 공격은 공통 설정이 4타여도 2타로 뛰지 않는다.
	Config->ComboSectionNames = {TEXT("Default"), TEXT("Swing2"), TEXT("Swing3"), TEXT("Swing4")};
	UAnimMontage* SingleSection = NewObject<UAnimMontage>(Config);
	SingleSection->CompositeSections.Add(FCompositeSection());
	SingleSection->CompositeSections.Last().SectionName = TEXT("Default");
	UAnimMontage* FourSections = NewObject<UAnimMontage>(Config);
	for (const FName& Name : Config->ComboSectionNames)
	{
		FourSections->CompositeSections.Add(FCompositeSection());
		FourSections->CompositeSections.Last().SectionName = Name;
	}

	TestFalse(TEXT("Single-section montage has no second swing"), VBMeleeAttackRules::HasComboSection(SingleSection, Config, 1));
	TestTrue(TEXT("Four-section montage has the second swing"), VBMeleeAttackRules::HasComboSection(FourSections, Config, 1));
	TestFalse(TEXT("No montage, no section"), VBMeleeAttackRules::HasComboSection(nullptr, Config, 1));
	TestEqual(TEXT("Past the name list falls back to legacy SwingN"),
		VBMeleeAttackRules::ComboSectionName(Config, 4), FName(TEXT("Swing5")));
	return true;
}

// 개발 전투 판독 기록. 월드 없이 돈다 - class default 가 액터 신원을 대신한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatReadoutLogTest, "VB.Unit.Combat.CombatReadoutLog",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBCombatReadoutLogTest::RunTest(const FString& Parameters)
{
	const AActor* Attacker = GetDefault<APawn>();
	const AActor* Enemy = GetDefault<ACharacter>();
	const AActor* Other = GetDefault<AActor>();
	const auto LineAt = [](const FVBCombatReadoutLog& Book, int32 Index) { const auto Lines = Book.BuildLines(); return Lines.IsValidIndex(Index) ? Lines[Index].Key : FString(); };

	FVBCombatReadoutLog Book;
	Book.AddCombo(Attacker, TEXT("COMBO test"), 0.0);
	Book.WindowOpen(Attacker, 1, 1, TEXT("HandR"), 110.0f, 100.0f, Enemy);
	Book.WindowHit(Attacker, 1, Enemy);
	TestTrue(TEXT("Hit window reads HIT"), Book.WindowClosed(Attacker, 1, false).Contains(TEXT("HIT")));
	Book.Knockback(Enemy, Attacker, 300.0f, 62.0f, TEXT("launch"), 0.0);
	TestTrue(TEXT("Knockback lands on the line that hit the victim"), LineAt(Book, 1).Contains(TEXT("kb 300 launch est 62cm")));

	Book.WindowOpen(Attacker, 2, 2, TEXT("HandL"), 190.0f, 100.0f, Enemy);
	TestTrue(TEXT("Missing the enemy hit 1 connected with is flagged"),
		Book.WindowClosed(Attacker, 2, false).Contains(TEXT("enemy outside reach after hit 1")));
	Book.WindowOpen(Attacker, 3, 3, TEXT("HandR"), 190.0f, 100.0f, Other);
	const FString OtherMiss = Book.WindowClosed(Attacker, 3, false);
	TestTrue(TEXT("Missing a different target is a plain MISS"), OtherMiss.Contains(TEXT("MISS"))
		&& !OtherMiss.Contains(TEXT("outside reach")));
	Book.WindowOpen(Attacker, 4, 4, TEXT("HandL"), 190.0f, 100.0f, Enemy);
	const FString Cut = Book.WindowClosed(Attacker, 4, true);
	TestTrue(TEXT("An interrupted window says cut, not out of reach"), Cut.Contains(TEXT("cut")) && !Cut.Contains(TEXT("outside reach")));
	TestTrue(TEXT("Closing twice reports nothing"), Book.WindowClosed(Attacker, 4, true).IsEmpty());

	for (int32 Index = 0; Index < FVBCombatReadoutLog::MaxCombos + 1; ++Index)
	{
		Book.AddCombo(Attacker, FString::Printf(TEXT("COMBO %d"), Index), 0.0);
	}

	int32 Headers = 0;
	for (const TPair<FString, FColor>& Line : Book.BuildLines())
	{
		Headers += Line.Key.StartsWith(TEXT("COMBO")) ? 1 : 0;
	}

	TestEqual(TEXT("Only the newest combos are kept"), Headers, FVBCombatReadoutLog::MaxCombos);

	FVBComboSelectContext Context;
	Context.bHasTarget = true;
	Context.DistanceCm = 90.0f;
	FVBComboChoice Choice;
	Choice.Variant = 2;
	Choice.Reason = EVBComboChoiceReason::Band;
	Choice.Band = EVBComboRole::Close;
	Choice.Qualified = 4;
	const FString Header = UVBCombatReadoutSubsystem::DescribeChoice(Attacker, TEXT("AM_Fighter_Combo03"), Choice, Context);
	TestTrue(TEXT("Header names band, distance and count"),
		Header.Contains(TEXT("band Close (dist 90)")) && Header.Contains(TEXT("4 qualified")));
	TestFalse(TEXT("No rotation field when the branch does not rotate"), Header.Contains(TEXT("rotation")));
	Choice.RotationRole = EVBComboRole::Close;
	TestTrue(TEXT("A rotated pick with no earlier start says first"),
		UVBCombatReadoutSubsystem::DescribeChoice(Attacker, TEXT("AM_Fighter_Combo03"), Choice,
			Context).Contains(TEXT("rotation Close first")));
	Choice.RotationAfter = 1;
	TestTrue(TEXT("A rotated pick names the start it came after"),
		UVBCombatReadoutSubsystem::DescribeChoice(Attacker, TEXT("AM_Fighter_Combo03"), Choice,
			Context).Contains(TEXT("rotation Close after #1")));

	// 밀림 추적: 밀림은 공격자의 다음 창에 끊기지 않고 멈출 때까지 따라가며, 같은 피해자의 다음 밀림이 PUSH 줄로 끝낸다.
	FVBCombatReadoutLog Pushes;
	Pushes.AddCombo(Attacker, TEXT("COMBO push"), 0.0);
	Pushes.WindowOpen(Attacker, 11, 1, TEXT("HandR"), 80.0f, 68.0f, Enemy);
	Pushes.WindowHit(Attacker, 11, Enemy);
	Pushes.WindowClosed(Attacker, 11, false);
	Pushes.Knockback(Enemy, Attacker, 150.0f, 22.0f, TEXT("launch"), 0.0);
	Pushes.WindowOpen(Attacker, 12, 2, TEXT("HandL"), 80.0f, 68.0f, Enemy);
	TestTrue(TEXT("Opening the next window does not end the first push"), LineAt(Pushes, 1).Contains(TEXT("moving..")));
	TestTrue(TEXT("No PUSH line before anything ended"), Pushes.TakePushLines().IsEmpty());
	Pushes.WindowHit(Attacker, 12, Enemy);
	Pushes.Knockback(Enemy, Attacker, 150.0f, 22.0f, TEXT("launch"), 0.10);
	const TArray<FString> CutPushes = Pushes.TakePushLines();
	TestTrue(TEXT("The next push on the same victim ends the first with a reason"),
		CutPushes.Num() == 1 && CutPushes[0].Contains(TEXT("hit 1")) && CutPushes[0].Contains(TEXT("cut by the next push")));
	Pushes.TickTracking(1.0); // 기본 액터는 속도 0 - 최소 추적 시간 뒤에 멈춘 것으로 본다
	const TArray<FString> Settled = Pushes.TakePushLines();
	TestTrue(TEXT("The second push reports when it settles"),
		Settled.Num() == 1 && Settled[0].Contains(TEXT("hit 2")) && Settled[0].Contains(TEXT("settled")));
	TestTrue(TEXT("Lines are taken once"), Pushes.TakePushLines().IsEmpty());
	return true;
}

// vb.Combat.ForceCombo: 끔, 범위 밖은 보통 고르기를 두고, 번호는 그 변형을 강제한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComboForceVariantTest, "VB.Unit.Combat.ForceCombo",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBComboForceVariantTest::RunTest(const FString& Parameters)
{
	FVBComboChoice Normal;
	Normal.Variant = 1;
	Normal.Reason = EVBComboChoiceReason::Band;
	Normal.bTargetInReach = false;

	FVBComboChoice Choice = Normal;
	TestFalse(TEXT("-1 is off"), UVBComboSelectionPolicy::ApplyForcedVariant(-1, 6, true, Choice));
	TestEqual(TEXT("off keeps the normal variant"), Choice.Variant, 1);
	TestTrue(TEXT("off keeps the normal reason"), Choice.Reason == EVBComboChoiceReason::Band);
	TestFalse(TEXT("index past the list is ignored"), UVBComboSelectionPolicy::ApplyForcedVariant(6, 6, true, Choice));
	TestEqual(TEXT("ignored index keeps the normal variant"), Choice.Variant, 1);

	TestTrue(TEXT("3 forces"), UVBComboSelectionPolicy::ApplyForcedVariant(3, 6, true, Choice));
	TestEqual(TEXT("forced variant is 3 (a Manual combo is allowed)"), Choice.Variant, 3);
	TestTrue(TEXT("reason says forced"), Choice.Reason == EVBComboChoiceReason::Forced);
	TestTrue(TEXT("forced pick approaches a present target"), Choice.bTargetInReach);
	FVBComboChoice NoTarget = Normal;
	UVBComboSelectionPolicy::ApplyForcedVariant(3, 6, false, NoTarget);
	TestFalse(TEXT("without a target it does not stretch"), NoTarget.bTargetInReach);
	return true;
}

// UVBAttackConfig::IsDataValid 뒤의 저장 시 검사. 반대 대조: Close 하나, Neutral 하나, Manual 일곱 줄이면 두 대역을 보고한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComboLoneBandTest, "VB.Unit.Combat.ComboLoneBands",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBComboLoneBandTest::RunTest(const FString& Parameters)
{
	using R = EVBComboRole;
	const auto Rows = [](std::initializer_list<R> Roles)
	{
		TArray<FVBComboCandidate> Out;
		for (const R Role : Roles) { Out.Add(FVBComboCandidate{Role, {0.0f, 150.0f}}); }
		return Out;
	};
	const TArray<R> R243 = UVBComboSelectionPolicy::FindLoneBandsBesideManual(Rows({R::Neutral, R::Close, R::Manual, R::Manual, R::Manual,
		R::Sprint, R::Sprint, R::Manual, R::Manual, R::Manual, R::Manual}));
	TestTrue(TEXT("r243: Close is lone beside Manual"), R243.Contains(R::Close));
	TestTrue(TEXT("r243: Neutral is lone beside Manual"), R243.Contains(R::Neutral));
	TestFalse(TEXT("r243: an empty Far band is not lone"), R243.Contains(R::Far));

	TestEqual(TEXT("10-25 roles: no lone band"), UVBComboSelectionPolicy::FindLoneBandsBesideManual(Rows({R::Neutral, R::Close, R::Close,
		R::Close, R::Close, R::Sprint, R::Sprint, R::Neutral, R::Neutral, R::Close, R::Close})).Num(), 0);
	TestEqual(TEXT("one row per band without Manual rows is allowed (placeholder weapons)"),
		UVBComboSelectionPolicy::FindLoneBandsBesideManual(Rows({R::Neutral, R::Close, R::Far})).Num(), 0);
	TestEqual(TEXT("one Manual row next to a lone band reports it"),
		UVBComboSelectionPolicy::FindLoneBandsBesideManual(Rows({R::Neutral, R::Neutral, R::Close, R::Manual})).Num(), 1);
	return true;
}

// 출하된 모든 attack config 가 같은 검사를 통과해야 외톨이 대역이 몰래 남지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackConfigLoneBandAssetsTest, "VB.Unit.Combat.AttackConfigsNoLoneBands",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackConfigLoneBandAssetsTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.ScanPathsSynchronous({ TEXT("/Game/VB") }, /*bForceRescan=*/false);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(UVBAttackConfig::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);
	if (!TestTrue(TEXT("attack configs found"), Assets.Num() > 0))
	{
		return false;
	}

	for (const FAssetData& Asset : Assets)
	{
		const UVBAttackConfig* Config = Cast<UVBAttackConfig>(Asset.GetAsset());
		if (!Config)
		{
			AddError(FString::Printf(TEXT("%s did not load as an attack config"), *Asset.GetObjectPathString()));
			continue;
		}

		if (!Config->ComboSelection)
		{
			continue;
		}

		for (const EVBComboRole Band : UVBComboSelectionPolicy::FindLoneBandsBesideManual(Config->GetComboCandidates()))
		{
			AddError(FString::Printf(TEXT("%s: band %s has one row beside Manual rows"), *Asset.AssetName.ToString(),
				*UEnum::GetValueAsString(Band)));
		}
	}

	return true;
}
