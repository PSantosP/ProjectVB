// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBTargetLockConfig.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBWeaponStateConfig.h"
#include "Data/VBMedicalConfig.h"
#include "Data/VBFinisherWindowConfig.h"
#include "Data/VBMovementConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBCombatSettings.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h" // 공격 GA 자산 태그 계약 시험
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"   // 접근 목표, 싱크 점 시험
#include "UObject/UObjectIterator.h"                      // 파생 클래스 전수 열거
#include "Player/VBPlayerState.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"

// plain cpp(UHT 무관)라 가드 가능 - 펑셔널 트랙(UCLASS)과 달리 단위 트랙은 표준 가드를 쓴다.
#if WITH_AUTOMATION_TESTS

// LockConfig DataAsset 이 비어도 GetTargetLockConfig 는 CDO fallback 으로 null 을 돌려주지 않는다(월드 불필요).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTargetLockConfigFallbackTest,
	"VB.Unit.Config.TargetLockCDOFallback",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTargetLockConfigFallbackTest::RunTest(const FString& Parameters)
{
	UVBTargetLockComponent* Comp = NewObject<UVBTargetLockComponent>(GetTransientPackage());
	TestNotNull(TEXT("component"), Comp);
	if (!Comp)
	{
		return false;
	}

	const UVBTargetLockConfig* Config = Comp->GetTargetLockConfig();
	TestNotNull(TEXT("GetTargetLockConfig() must never return null (CDO fallback)"), Config);
	return true;
}

// AttackConfig 의 적중 trace 채널 기본값은 ECC_Pawn 이어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackConfigDefaultsTest,
	"VB.Unit.Config.AttackConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBAttackConfig* CDO = GetDefault<UVBAttackConfig>();
	TestNotNull(TEXT("UVBAttackConfig CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TestEqual(TEXT("HitTraceChannel default must stay ECC_Pawn"),
		CDO->HitTraceChannel.GetValue(), ECC_Pawn);
	TestEqual(TEXT("MinWarpStandoff default must stay 20"), CDO->MinWarpStandoff, 20.0f);
	// 답 기본값: 비면 예고 큐가 답 행을 못 찾아 경고를 찍고, DodgeOnly 가 기본이면 모든 공격이 가드를 뚫는다.
	TestTrue(TEXT("AttackAnswer default must be Attack.Answer.Parryable"),
		CDO->AttackAnswer == VBGameplayTags::Attack_Answer_Parryable);
	return true;
}

// 그룹 전투 기본값 가드. DA_Enemy_Default 가 대부분을 저작하지 않아 헤더 숫자가 곧 모든 적의 실효값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyConfigDefaultsTest,
	"VB.Unit.Config.EnemyConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBEnemyConfig* CDO = GetDefault<UVBEnemyConfig>();
	TestNotNull(TEXT("UVBEnemyConfig CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TestEqual(TEXT("AttackTokenCost default must stay 1"), CDO->AttackTokenCost, 1);
	TestEqual(TEXT("AttackCommitDistance default must stay 180"), CDO->AttackCommitDistance, 180.0f);
	TestEqual(TEXT("AttackReleaseDistance default must stay 200"), CDO->AttackReleaseDistance, 200.0f);
	TestEqual(TEXT("TelegraphDuration default must stay 0.6"), CDO->TelegraphDuration, 0.6f);
	TestEqual(TEXT("MaxApproachHoldSeconds default must stay 8"), CDO->MaxApproachHoldSeconds, 8.0f);

	// 걸음 계약. 값 자체보다 둘이 갈린다는 것이 요점이다 - 같아지면 둘러쌀 때와 쫓아올 때가 구분되지 않는다.
	TestTrue(TEXT("gait contract: WalkSpeed must be slower than RunSpeed"), CDO->WalkSpeed < CDO->RunSpeed);

	// 걸음 문턱은 쌍이라야 경계에서 안 떤다. 같거나 뒤집히면 매 주기 걸음이 왕복한다.
	TestTrue(TEXT("gait hysteresis: RunExitDistance must be shorter than RunEnterDistance"),
		CDO->RunExitDistance < CDO->RunEnterDistance);

	// 값들 사이의 계약이다. 뒤집히면 역할이 재계산 주기마다 왕복한다 - C++ 기본값은 IsDataValid 밖이라 여기서 본다.
	TestTrue(TEXT("hysteresis contract: Release must exceed Commit"),
		CDO->AttackReleaseDistance > CDO->AttackCommitDistance);

	// 접근 상한이 정상 접근 시간보다 짧으면 모든 적이 붙기 전에 토큰을 뺏긴다.
	// 식은 IsDataValid 와 함께 UVBEnemyConfig::GetNormalApproachSeconds() 하나만 쓴다.
	const float NormalApproachSeconds = CDO->GetNormalApproachSeconds();
	TestTrue(TEXT("approach budget must cover a normal approach from max sight range"),
		CDO->MaxApproachHoldSeconds > NormalApproachSeconds);

	return true;
}

// 그룹 전투 전역 노브의 불변식 가드. UVBCombatSettings 는 config=Game 이라 값의 홈이 ini 다 -
//   정확한 값을 단언하면 정상 튜닝이 테스트를 깨므로 어떤 값에서도 성립해야 하는 관계만 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatSettingsInvariantsTest,
	"VB.Unit.Config.CombatSettingsInvariants",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatSettingsInvariantsTest::RunTest(const FString& Parameters)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	TestNotNull(TEXT("UVBCombatSettings CDO"), Settings);
	if (!Settings)
	{
		return false;
	}

	// 0 이면 아무도 영원히 공격하지 않는다. ClampMin 이 0 이라 에디터가 막지 못하는 값이다.
	TestTrue(TEXT("MaxAttackTokens must be positive or nobody ever attacks"),
		Settings->MaxAttackTokens > 0);

	// 0 이면 타이머가 걸리지 않아 그룹이 영원히 정지한다.
	TestTrue(TEXT("SlotRecomputeInterval must be positive or groups never recompute"),
		Settings->SlotRecomputeInterval > 0.0f);

	// 공격 행동 상한이 재계산 주기보다 짧으면 정상 공격이 매 주기 만료로 잡힌다.
	TestTrue(TEXT("MaxTokenHoldSeconds must exceed the recompute interval"),
		Settings->MaxTokenHoldSeconds > Settings->SlotRecomputeInterval);

	// 슬롯이 0 이면 대기자 배정 자체가 성립하지 않는다.
	TestTrue(TEXT("SurroundSlotCount must be positive"), Settings->SurroundSlotCount > 0);

	return true;
}

// 데이터화 P0 회귀 가드: 흡수된 리터럴 기본값이 현행값과 동일해야 한다(FEEL 드리프트 방지).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponStateConfigDefaultsTest,
	"VB.Unit.Config.WeaponStateConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponStateConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBWeaponStateConfig* CDO = GetDefault<UVBWeaponStateConfig>();
	TestNotNull(TEXT("UVBWeaponStateConfig CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TestEqual(TEXT("CombatExitIdleSpeedThreshold default must stay 10"), CDO->CombatExitIdleSpeedThreshold, 10.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTargetLockConfigDefaultsTest,
	"VB.Unit.Config.TargetLockConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTargetLockConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBTargetLockConfig* CDO = GetDefault<UVBTargetLockConfig>();
	TestNotNull(TEXT("UVBTargetLockConfig CDO"), CDO);
	if (!CDO)
	{
		return false;
	}
	// VBCharacter 에서 이동된 값 - 드리프트 시 타겟 전환 FEEL 이 바뀐다.
	TestEqual(TEXT("SwitchTargetMouseThreshold default must stay 1.5"), CDO->SwitchTargetMouseThreshold, 1.5f);
	TestEqual(TEXT("SwitchTargetCooldown default must stay 0.3"), CDO->SwitchTargetCooldown, 0.3f);
	TestEqual(TEXT("SearchTraceChannel default must stay ECC_Pawn"), CDO->SearchTraceChannel.GetValue(), ECC_Pawn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMovementConfigDefaultsTest,
	"VB.Unit.Config.MovementConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMovementConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBMovementConfig* CDO = GetDefault<UVBMovementConfig>();
	TestNotNull(TEXT("UVBMovementConfig CDO (fallback 원천)"), CDO);
	if (!CDO)
	{
		return false;
	}

	// 전역 CMC 물리 - 현행값 보존.
	TestEqual(TEXT("BrakingDecelerationWalking default 750"), CDO->BrakingDecelerationWalking, 750.0f);
	TestEqual(TEXT("MaxAcceleration default 800"), CDO->MaxAcceleration, 800.0f);
	TestEqual(TEXT("GroundFriction default 5"), CDO->GroundFriction, 5.0f);
	TestEqual(TEXT("SprintRampDuration default 0.6"), CDO->SprintRampDuration, 0.6f);
	TestEqual(TEXT("Original GASP slide entry threshold"), CDO->SlideStartSpeed, 380.0f);
	TestTrue(TEXT("SlideEndSpeed below start"), CDO->SlideEndSpeed < CDO->SlideStartSpeed);
	TestEqual(TEXT("Slope slide has no forced duration limit"), CDO->SlideMaxDuration, 0.0f);
	// 회전율 - BUG-010 라이브 튜닝 대상. 기본값 보존. (FRotator.Yaw 는 UE5 에서 double -> double 리터럴)
	TestEqual(TEXT("DefaultRotationRate.Yaw -1 (즉시 snap)"), CDO->DefaultRotationRate.Yaw, -1.0);
	TestEqual(TEXT("AirborneRotationRate.Yaw 200"), CDO->AirborneRotationRate.Yaw, 200.0);
	TestEqual(TEXT("ArmedGroundedRotationRate.Yaw 360"), CDO->ArmedGroundedRotationRate.Yaw, 360.0);

	// ResolveProfile - 저작된 무장 루프 속도와 CMC 프로파일이 함께 바뀌어야 한다.
	// (FVector 성분은 UE5 에서 double -> double 리터럴. CrouchSpeed 는 float -> f 리터럴.)
	const FVBMovementProfile& Unarmed = CDO->ResolveProfile(EVBWeaponType::None, /*bArmed=*/false);
	TestEqual(TEXT("Unarmed Run.X 500"), Unarmed.RunSpeeds.X, 500.0);
	TestEqual(TEXT("Unarmed Armed-diagonal 보정 미사용"), Unarmed.RunDiagonalSpeed, 0.0f);
	TestEqual(TEXT("Unarmed Walk.X 200"), Unarmed.WalkSpeeds.X, 200.0);
	TestEqual(TEXT("Unarmed CrouchSpeed 150"), Unarmed.CrouchSpeed, 150.0f);
	TestFalse(TEXT("Unarmed sprint 램프 off(단일 650)"), Unarmed.bSprintRampEnabled);
	TestEqual(TEXT("Unarmed SprintRun.X 650"), Unarmed.SprintRunSpeeds.X, 650.0);

	// Armed 기본(Fighter/Magic) - override 미등록 무기는 이 base 로 폴백.
	const FVBMovementProfile& Armed = CDO->ResolveProfile(EVBWeaponType::Fighter, /*bArmed=*/true);
	TestEqual(TEXT("Fighter Run.X 250"), Armed.RunSpeeds.X, 250.0);
	TestEqual(TEXT("Fighter RunDiagonal 225"), Armed.RunDiagonalSpeed, 225.0f);
	TestEqual(TEXT("Armed Walk.X 165"), Armed.WalkSpeeds.X, 165.0);
	TestEqual(TEXT("Armed CrouchSpeed 165"), Armed.CrouchSpeed, 165.0f);
	TestTrue(TEXT("Armed sprint 램프 on"), Armed.bSprintRampEnabled);
	TestEqual(TEXT("Armed SprintRun.X 550"), Armed.SprintRunSpeeds.X, 550.0);
	TestEqual(TEXT("Armed Sprint.X 640"), Armed.SprintSpeeds.X, 640.0);
	const FVBMovementProfile& Blade = CDO->ResolveProfile(EVBWeaponType::Blade, /*bArmed=*/true);
	TestEqual(TEXT("Blade RunDiagonal 240"), Blade.RunDiagonalSpeed, 240.0f);
	TestEqual(TEXT("Blade Sprint.X 680"), Blade.SprintSpeeds.X, 680.0);
	// Magic(override 미등록) 도 ArmedDefault 로 폴백.
	const FVBMovementProfile& Magic = CDO->ResolveProfile(EVBWeaponType::Magic, /*bArmed=*/true);
	TestEqual(TEXT("Magic(override 미등록) Run.X 250 폴백"), Magic.RunSpeeds.X, 250.0);

	// BigSword override - jog 대각선 240, Fast 485. 고속 2단 램프는 없다.
	const FVBMovementProfile& Big = CDO->ResolveProfile(EVBWeaponType::BigSword, /*bArmed=*/true);
	TestEqual(TEXT("BigSword Run.X 255"), Big.RunSpeeds.X, 255.0);
	TestEqual(TEXT("BigSword RunDiagonal 240"), Big.RunDiagonalSpeed, 240.0f);
	TestFalse(TEXT("BigSword sprint 램프 off(485 캡)"), Big.bSprintRampEnabled);
	TestEqual(TEXT("BigSword SprintRun.X 485"), Big.SprintRunSpeeds.X, 485.0);
	TestEqual(TEXT("BigSword Walk.X 165(Armed 동일)"), Big.WalkSpeeds.X, 165.0);
	return true;
}

// 처형 창 설정의 기본값 가드 - 전투 규칙을 정하는 숫자가 조용히 바뀌지 않게 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherWindowConfigDefaultsTest,
	"VB.Unit.Config.FinisherWindowConfigDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFinisherWindowConfigDefaultsTest::RunTest(const FString& Parameters)
{
	const UVBFinisherWindowConfig* CDO = GetDefault<UVBFinisherWindowConfig>();
	TestNotNull(TEXT("UVBFinisherWindowConfig CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	TestEqual(TEXT("처형 창 체력 문턱 디폴트 0.30"), CDO->HealthThreshold, 0.30f);
	TestTrue(TEXT("처형 창 길이는 양수여야 한다 - 0 이면 열리자마자 닫혀 처형이 영영 안 걸린다"),
		CDO->WindowDuration > 0.0f);
	return true;
}

// 핵심 네이티브 GameplayTag 가 모듈 로드 시점에 등록되는지. extern 심볼의 GetTag().IsValid() 가 직접 증거다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGameplayTagsRegisteredTest,
	"VB.Unit.Tags.CoreTagsRegistered",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGameplayTagsRegisteredTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Ability_Attack_Light"), VBGameplayTags::Ability_Attack_Light.GetTag().IsValid());
	// 강공격 대신 RMB 는 가드다 - 가드 태그를 본다.
	TestTrue(TEXT("Input_Combat_Guard"), VBGameplayTags::Input_Combat_Guard.GetTag().IsValid());
	TestTrue(TEXT("State_Combat_Guarding"), VBGameplayTags::State_Combat_Guarding.GetTag().IsValid());
	TestTrue(TEXT("Ability_Defense_Dodge"), VBGameplayTags::Ability_Defense_Dodge.GetTag().IsValid());
	TestTrue(TEXT("Ability_Medical_Purify"), VBGameplayTags::Ability_Medical_Purify.GetTag().IsValid());
	TestTrue(TEXT("State_Dead"), VBGameplayTags::State_Dead.GetTag().IsValid());
	TestTrue(TEXT("Ability_Attack_Riposte"), VBGameplayTags::Ability_Attack_Riposte.GetTag().IsValid());
	return true;
}

// 다음 에디터 차례의 데이터 단계가 이름으로 쓰는 이야기 태그가 ini 에 등록됐나.
//   왜: 스크립트가 문자열로 넣는 태그는 등록 전이면 빈 태그로 조용히 들어간다 - 조건이 늘 거짓, 사실이 안 켜짐으로만 보인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQueuedStoryTagsRegisteredTest,
	"VB.Unit.Tags.QueuedStoryTagsRegistered",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQueuedStoryTagsRegisteredTest::RunTest(const FString& Parameters)
{
	const TCHAR* Names[] = {
		TEXT("Story.Prologue.AtriumCleared"), TEXT("Story.Prologue.BossDefeated"), TEXT("Story.Prologue.BossEntranceSeen"), TEXT("Story.Prologue.TrayShieldAcquired"),
		TEXT("Story.Prologue.GolemDefeated"), TEXT("Story.Prologue.GolemEntranceSeen"), TEXT("Enemy.Boss.Golem"),
		TEXT("Story.Prologue.TeuthisanDefeated"), TEXT("Story.Prologue.TeuthisanEntranceSeen"), TEXT("Enemy.Boss.Teuthisan"),
		TEXT("Item.Document.HospitalNurseMap"), TEXT("Item.Document.HospitalSupply"), TEXT("Item.Document.HospitalWardA"),
		TEXT("Item.Document.HospitalWardB"), TEXT("Item.Document.HospitalRoomC"), TEXT("Item.Document.HospitalRoomD"),
		TEXT("Item.Document.HospitalRoomE"), TEXT("Item.Document.HospitalRoomF"), TEXT("Item.Document.HospitalPower"),
		TEXT("Item.Document.HospitalStair"), TEXT("Item.Document.HospitalAtrium"), TEXT("Item.Document.HospitalUpper"),
		TEXT("Story.Collection.Document.HospitalNurseMap"), TEXT("Story.Collection.Document.HospitalSupply"), TEXT("Story.Collection.Document.HospitalWardA"),
		TEXT("Story.Collection.Document.HospitalWardB"), TEXT("Story.Collection.Document.HospitalRoomC"), TEXT("Story.Collection.Document.HospitalRoomD"),
		TEXT("Story.Collection.Document.HospitalRoomE"), TEXT("Story.Collection.Document.HospitalRoomF"), TEXT("Story.Collection.Document.HospitalPower"),
		TEXT("Story.Collection.Document.HospitalStair"), TEXT("Story.Collection.Document.HospitalAtrium"), TEXT("Story.Collection.Document.HospitalUpper"),
		TEXT("Story.Prologue.FirstEnemyA.Defeated"), TEXT("Story.Prologue.FirstEnemyB.Defeated"),
		TEXT("Story.Prologue.O01EnemyA.Defeated"), TEXT("Story.Prologue.O01EnemyB.Defeated"),
		TEXT("Story.Prologue.E03EnemyA.Defeated"), TEXT("Story.Prologue.E03EnemyB.Defeated"), TEXT("Story.Prologue.E03EnemyC.Defeated"),
		TEXT("Story.Prologue.E04EnemyA.Defeated"), TEXT("Story.Prologue.E04EnemyB.Defeated"), TEXT("Story.Prologue.E04EnemyC.Defeated"),
		TEXT("Story.Prologue.E04EnemyD.Defeated"), TEXT("Story.Prologue.E04EnemyE.Defeated")};
	for (const TCHAR* Name : Names)
	{
		TestTrue(Name, FGameplayTag::RequestGameplayTag(FName(Name), false).IsValid());
	}
	// 게이지 입력 큐와 HUD 행이 이름으로 쓰는 네이티브 태그.
	TestTrue(TEXT("Input.CinematicSkill"), VBGameplayTags::Input_CinematicSkill.GetTag().IsValid());
	TestTrue(TEXT("UI.Slot.HUD.CinematicSkill"), VBGameplayTags::UI_Slot_HUD_CinematicSkill.GetTag().IsValid());
	return true;
}

// 공격 GA 는 전부 Ability.Attack 아래 자산 태그를 들어야 한다 - 안 달면 경직의 공격 차단을 조용히 비껴간다.
// 이름 나열이 아니라 전수 검사라 다음에 추가되는 공격 GA 도 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackAbilityAssetTagsTest,
	"VB.Unit.Tags.AttackAbilitiesCarryAttackTag",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackAbilityAssetTagsTest::RunTest(const FString& Parameters)
{
	int32 Checked = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (!Cls->IsChildOf(UVBGA_MeleeAttackBase::StaticClass()))
		{
			continue;
		}
		// 추상 베이스와 BP 생성 클래스는 제외한다 - 베이스는 태그를 들 이유가 없고,
		//   BP 클래스는 이 테스트 프로세스에 로드돼 있지 않을 수 있어 결과가 실행마다 흔들린다.
		if (Cls->HasAnyClassFlags(CLASS_Abstract) || Cls->ClassGeneratedBy != nullptr)
		{
			continue;
		}

		const UGameplayAbility* CDO = Cls->GetDefaultObject<UGameplayAbility>();
		if (!CDO)
		{
			continue;
		}

		++Checked;
		TestTrue(*FString::Printf(TEXT("%s 는 Ability.Attack 아래 자산 태그를 들어야 한다"), *Cls->GetName()),
			CDO->GetAssetTags().HasTag(VBGameplayTags::Ability_Attack));
	}
	// 열거가 0 이면 시험이 조용히 아무것도 안 본 것이다 - 그 침묵을 통과로 읽지 않는다.
	TestTrue(TEXT("공격 GA 를 하나 이상 검사했다"), Checked > 0);
	return true;
}

// 세이브 키 계약: SavedAttributes 는 저장/복원이 공유하는 유일한 목록이고, 그 키는 .sav 안에 문자열로 굳는다.
// rename 으로 인한 키 변경, 목록 누락, 상한(Max*) 재유입을 막는다 - 셋 다 런타임에는 조용하다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveAttributeKeysTest,
	"VB.Unit.Save.AttributeKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveAttributeKeysTest::RunTest(const FString& Parameters)
{
	// 배열의 홈은 AVBPlayerState 생성자라 CDO 로 읽는다(월드/ASC 등록 불요).
	const AVBPlayerState* CDO = GetDefault<AVBPlayerState>();
	TestNotNull(TEXT("AVBPlayerState CDO"), CDO);
	if (!CDO)
	{
		return false;
	}

	const TArray<FGameplayAttribute>& Saved = CDO->GetSavedAttributes();
	TestTrue(TEXT("SavedAttributes 가 비어 있지 않다"), Saved.Num() > 0);

	TArray<FName> Keys;
	Keys.Reserve(Saved.Num());
	for (const FGameplayAttribute& Attribute : Saved)
	{
		TestTrue(TEXT("SavedAttributes 원소가 전부 유효하다"), Attribute.IsValid());
		if (Attribute.IsValid())
		{
			Keys.Add(AVBPlayerState::MakeAttributeSaveKey(Attribute));
		}
	}

	// 구 세이브 호환 계약 - 이 세 키는 기존 .sav 에 이미 들어 있다. 바뀌면 값이 유실된다.
	TestTrue(TEXT("키 'Health' 보존"), Keys.Contains(FName(TEXT("Health"))));
	TestTrue(TEXT("키 'Shield' 보존"), Keys.Contains(FName(TEXT("Shield"))));
	TestTrue(TEXT("키 'Reputation' 포함"), Keys.Contains(FName(TEXT("Reputation"))));

	// 상한은 세이브가 아니라 GE 가 소유한다. 로드가 GE 적용보다 나중에 돌아 다시 넣으면 GE 튜닝을 되돌린다.
	TestFalse(TEXT("키 'MaxHealth' 부재"), Keys.Contains(FName(TEXT("MaxHealth"))));
	TestFalse(TEXT("키 'MaxShield' 부재"), Keys.Contains(FName(TEXT("MaxShield"))));

	// 상한이 있는 어트리뷰트는 복원 시 자를 대상이 지정돼 있어야 한다 - 빠지면 상한을 내렸을 때
	// base 가 상한을 넘은 채 남아 화면값과 다음 저장값이 갈린다.
	const TMap<FGameplayAttribute, FGameplayAttribute>& Caps = CDO->GetSavedAttributeCaps();
	TestEqual(TEXT("Health 의 상한은 MaxHealth"),
		Caps.FindRef(UVBHealthAttributeSet::GetHealthAttribute()), UVBHealthAttributeSet::GetMaxHealthAttribute());
	TestEqual(TEXT("Shield 의 상한은 MaxShield"),
		Caps.FindRef(UVBHealthAttributeSet::GetShieldAttribute()), UVBHealthAttributeSet::GetMaxShieldAttribute());
	return true;
}

// SkewWarp Facing 이 보는 점은 어떤 거리에서도 공격자보다 대상 쪽으로 MinLead 이상 앞서야 한다.
//   옛 식(목표 = 대상 + (공격자-대상) 방향 x 멈춤)은 멈추는 거리에서 공격자와 겹치고(월드 0도), 안쪽이면 뒤를 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBApproachSyncPointTest,
	"VB.Unit.Combat.ApproachSyncPoint",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBApproachSyncPointTest::RunTest(const FString& Parameters)
{
	const float MinLead = GetDefault<UVBAttackConfig>()->WarpSyncMinLead;
	TestTrue(TEXT("WarpSyncMinLead default is positive"), MinLead > 0.0f);
	const FVector Target(100.0, 0.0, 90.0);
	const FVector Forward(1.0, 0.0, 0.0);
	const float Stop = 71.7f; // 실측: 91.7cm 에서 세운 멈춤
	// 따라가는 목표 - 공격자 위치로 매 프레임 다시 계산된다. 제품 코드와 같은 함수로 만든다(식이 두 벌이 되지 않게).
	auto FollowGoal = [&](const FVector& Owner)
	{
		FVector Goal, Aim;
		VBMeleeAttackRules::ResolveApproachGoal(true, Owner, Target, Stop, FVector::ZeroVector, FVector::ZeroVector, Goal, Aim);
		return Goal;
	};
	auto Lead = [&](const FVector& Owner, const FVector& Sync) { return FVector::DotProduct(Sync - Owner, (Target - Owner).GetSafeNormal2D()); };

	struct FCase { const TCHAR* Name; FVector Owner; bool bHold; };
	const FCase Cases[] = {
		{ TEXT("outside the stop (91.7cm)"), Target - FVector(91.7, 0.0, 0.0), false },
		{ TEXT("on the stop (71.7cm)"), Target - FVector(71.7, 0.0, 0.0), true },
		{ TEXT("inside the stop (60cm)"), Target - FVector(60.0, 0.0, 0.0), true },
		{ TEXT("just outside by half the lead"), Target - FVector(Stop + MinLead * 0.5, 0.0, 0.0), true },
	};
	for (const FCase& Case : Cases)
	{
		bool bHold = false;
		const FVector Sync = VBMeleeAttackRules::ResolveApproachSyncPoint(Case.Owner, Forward, Target, FollowGoal(Case.Owner),
			MinLead, bHold);
		TestTrue(FString::Printf(TEXT("%s: hold is %s"), Case.Name, Case.bHold ? TEXT("on") : TEXT("off")), bHold == Case.bHold);
		TestTrue(FString::Printf(TEXT("%s: sync leads toward the target by >= MinLead (got %.3f)"), Case.Name, Lead(Case.Owner, Sync)),
			Lead(Case.Owner, Sync) >= MinLead - KINDA_SMALL_NUMBER);
	}

	{
		// 앞서 있는 목표는 건드리지 않는다 - 따라가는 추격(넉백 뒤)과 고정 목표의 거동이 그대로다.
		const FVector Owner = Target - FVector(91.7, 0.0, 0.0);
		bool bHold = true;
		const FVector Goal = FollowGoal(Owner);
		TestTrue(TEXT("outside: goal unchanged"),
			VBMeleeAttackRules::ResolveApproachSyncPoint(Owner, Forward, Target, Goal, MinLead, bHold).Equals(Goal));
	}

	{
		// 옆으로 비킨 고정 목표(적이 정한 한 점)도 앞서 있으면 그 점을 본다.
		const FVector Owner(0.0, 0.0, 90.0);
		const FVector Lateral(40.0, 50.0, 90.0);
		bool bHold = true;
		TestTrue(TEXT("lateral fixed goal ahead: kept"),
			VBMeleeAttackRules::ResolveApproachSyncPoint(Owner, Forward, Target, Lateral, MinLead, bHold).Equals(Lateral) && !bHold);
	}

	{
		// 대상과 겹쳤다 - 방향이 없으니 지금 정면으로 MinLead.
		bool bHold = false;
		const FVector Sync = VBMeleeAttackRules::ResolveApproachSyncPoint(Target, Forward, Target, Target, MinLead, bHold);
		TestTrue(TEXT("coincident: hold"), bHold);
		TestTrue(TEXT("coincident: sync is MinLead along the current forward"), Sync.Equals(Target + Forward * MinLead));
	}

	{
		// 높이가 달라도 앞섬은 수평으로 잰다 - 계단 위 대상에서도 같은 답.
		const FVector Owner = Target - FVector(60.0, 0.0, 40.0);
		bool bHold = false;
		const FVector Sync = VBMeleeAttackRules::ResolveApproachSyncPoint(Owner, Forward, Target, FollowGoal(Owner), MinLead, bHold);
		TestTrue(TEXT("height offset inside the stop: hold"), bHold);
		TestTrue(TEXT("height offset: sync is MinLead toward the target in 2D"), Sync.Equals(Owner + FVector(MinLead, 0.0, 0.0)));
	}

	{
		// 제자리 클립의 고정 목표(적): 목표 = 공격자 자리라 붙잡히지만, 겨냥은 세울 때 대상이 있던 점이다.
		//   대상이 옆으로 비켜도 그쪽으로 돌지 않는다 - 적은 한 점에 박는다(VBGA_EnemyMeleeAttack.cpp).
		const FVector Owner(0.0, 0.0, 90.0);
		const FVector StartAim(100.0, 0.0, 90.0);
		const FVector MovedTarget(60.0, 80.0, 90.0);
		FVector Goal, Aim;
		VBMeleeAttackRules::ResolveApproachGoal(false, Owner, MovedTarget, Stop, Owner, StartAim, Goal, Aim);
		TestTrue(TEXT("fixed: aim stays at the start point"), Aim.Equals(StartAim));
		bool bHold = false;
		const FVector Sync = VBMeleeAttackRules::ResolveApproachSyncPoint(Owner, Forward, Aim, Goal, MinLead, bHold);
		TestTrue(TEXT("fixed in-place: hold"), bHold);
		TestTrue(TEXT("fixed in-place: sync toward the start point, not the moved target"),
			Sync.Equals(Owner + FVector(MinLead, 0.0, 0.0)));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
