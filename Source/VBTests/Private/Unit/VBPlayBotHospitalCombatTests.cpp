// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "PlayBot/VBPlayBotHospitalCombat.h"
#include "PlayBot/VBPlayBotDamageEvidence.h"
#include "PlayBot/VBPlayBotNativeSteering.h"
#include "Unit/VBPlayBotInputFixture.h"

namespace
{
	FVBPlayBotCombatPolicy Policy()
	{
		FVBPlayBotCombatPolicy P; P.AttackAction = TEXT("Input.Attack.Light"); P.AttackRangeCm = 180; P.AttackHeightCm = 80;
		P.AttackIntervalSeconds = 0.4; P.MinStaminaFraction = 0.15; P.NoProgressSeconds = 20; P.MaxAttacks = 40; return P;
	}
	FVBPlayBotHospitalCombatView View()
	{
		FVBPlayBotHospitalCombatView V; V.Movement.bReady = true; V.Movement.bGrounded = true; V.Movement.bWeaponOwned = true;
		V.PlayerHealth = V.PlayerMaxHealth = V.Stamina = V.MaxStamina = 100;
		FVBPlayBotCombatTargetView T; T.Id = TEXT("EnemyA"); T.Position = FVector(150,0,0); T.Health = T.MaxHealth = 100; T.bVisible = true;
		V.Targets.Add(T); return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotFightBindingsTest,"VB.Unit.PlayBot.Hospital.FightBindings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotFightBindingsTest::RunTest(const FString& Parameters)
{
	const FString A = TEXT("/Game/VB/Maps/Prologue/L_Prologue_Hospital.L_Prologue_Hospital:PersistentLevel.BP_EnemyInfected_C_0|/Game/VB/Blueprints/Enemy/BP_EnemyInfected.BP_EnemyInfected_C|Story.Prologue.FirstEnemyA.Defeated");
	const FString B = TEXT("/Game/VB/Maps/Prologue/L_Prologue_Hospital.L_Prologue_Hospital:PersistentLevel.BP_EnemyInfected_C_3|/Game/VB/Blueprints/Enemy/BP_EnemyInfected.BP_EnemyInfected_C|Story.Prologue.FirstEnemyB.Defeated");
	TArray<FVBPlayBotCombatTargetRef> Targets; FString Error;
	TestTrue(TEXT("actual stable target declarations parse"), FVBPlayBotHospitalFightSpec::ParseTargets({A,B},Targets,Error));
	TestFalse(TEXT("duplicate actor/fact cannot double-count a kill"), FVBPlayBotHospitalFightSpec::ParseTargets({A,A},Targets,Error));
	TestFalse(TEXT("empty declared fight refuses"), FVBPlayBotHospitalFightSpec::ParseTargets({},Targets,Error));
	TestFalse(TEXT("display labels cannot replace stable object paths"), FVBPlayBotHospitalFightSpec::ParseTargets({TEXT("ENC_FirstEnemy|Class|Story.Prologue.FirstEnemyA.Defeated")},Targets,Error));
	FVBPlayBotCombatPolicy P = Policy(); P.MinStaminaFraction = -1;
	TestFalse(TEXT("missing stamina policy is not silently defaulted"), P.IsValid(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotFightOracleTest,"VB.Unit.PlayBot.Hospital.FightOracle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotFightOracleTest::RunTest(const FString& Parameters)
{
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds); FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(),177);
	FVBPlayBotHospitalCombatActions Actions; FString Error; Actions.Configure(Policy(),Error);
	FVBPlayBotHospitalCombatView V = View(); V.Targets[0].bDead = V.Targets[0].bDefeatedFact = true;
	TestTrue(TEXT("pre-completed or unrelated deaths do not prove player combat"), Actions.Tick(V,1,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
	Actions.Configure(Policy(),Error); V = View(); V.bPlayerDead = true;
	TestTrue(TEXT("natural player death fails the attempt"), Actions.Tick(V,1,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
	Actions.Configure(Policy(),Error); V = View();
	TestTrue(TEXT("missing attack mapping fails rather than fabricate a hit"), Actions.Tick(V,1,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
	Actions.Configure(Policy(),Error); V = View(); V.Targets[0].Position.X = 1000;
	TestTrue(TEXT("approach alone remains incomplete"), Actions.Tick(V,1,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("no player-damage progress exhausts its budget"), Actions.Tick(V,22,Input,Human,Error) == EVBPlayBotOpeningResult::Failed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotFightInputTest,"VB.Unit.PlayBot.Hospital.FightInputEvidence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotFightInputTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	FMappingFixture Fixture; if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Attack = NewObject<UInputAction>(); UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>(); Pad->MapKey(Attack,EKeys::Gamepad_FaceButton_Left);
	if (!Fixture.Configure(*this,Pad,Keyboard)) { return false; }
	FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds); FString Error;
	if (!TestTrue(TEXT("fight resolves actual pad mapping"), Input.ResolveGamepadAction(Fixture.Player.Controller,TEXT("Input.Attack.Light"),Attack,Error))) { return false; }
	FVBPlayBotHospitalCombatActions Actions; Actions.Configure(Policy(),Error);
	FVBPlayBotHumanModel Human; Human.Configure(FVBPlayBotHumanProfile(),177);
	int32 Downs = 0;
	Input.OnButton = [&](const FVBPlayBotPress& Press,bool bDown) { if (bDown) { ++Downs; Actions.NotifyInjected(Press.Id,Press.InjectedSeconds); } };
	FVBPlayBotHospitalCombatView V = View(); V.Stamina = 0;
	Actions.Tick(V,1,Input,Human,Error); Input.TickNative(1,1,16,50);
	TestEqual(TEXT("low stamina sends no free attack"), Downs,0);
	V.Stamina = 100; V.Targets[0].Position.Z = 400; Actions.Tick(V,1.5,Input,Human,Error); Input.TickNative(2,1.5,500,50);
	TestEqual(TEXT("same XY on another floor sends no attack"), Downs,0);
	V.Targets[0].Position.Z = 0; Actions.Tick(V,2,Input,Human,Error); Input.TickNative(3,3,1500,50);
	TestEqual(TEXT("normal seconds schedule delivers one actual button edge"), Downs,1);
	V.Targets[0].bDead = V.Targets[0].bDefeatedFact = true; V.Targets[0].PlayerDamageHits = 1;
	TestTrue(TEXT("confirmed damage and defeat begins settle"), Actions.Tick(V,3.1,Input,Human,Error) == EVBPlayBotOpeningResult::Running);
	TestTrue(TEXT("input plus each-target damage/defeat can complete a partial fight"), Actions.Tick(V,3.5,Input,Human,Error) == EVBPlayBotOpeningResult::PartialComplete);
	Input.ReleaseAll(3,3.6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeSteeringTest,"VB.Unit.PlayBot.Hospital.NativeSteering",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeSteeringTest::RunTest(const FString& Parameters)
{
	FVBPlayBotHumanProfile Profile; Profile.MoveDirSdDeg = 0; Profile.MoveDirClipDeg = 0;
	FVBPlayBotHumanModel Human; Human.Configure(Profile,177); FVBPlayBotInputDriver Input(EVBPlayBotClockMode::NativeSeconds);
	FVBPlayBotNativeSteering Steering;
	Steering.Drive({FVector(100,0,0),0,0},1,1,Input,Human);
	TestTrue(TEXT("world forward becomes pad forward at yaw0"), Input.GetStickWant().Equals(FVector2D(0,1),1.e-6));
	Steering.Drive({FVector(100,0,0),90,90},1,2,Input,Human);
	TestTrue(TEXT("world forward becomes pad left at yaw90"), Input.GetStickWant().Equals(FVector2D(-1,0),1.e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotDamageEvidenceTest,"VB.Unit.PlayBot.Hospital.DamageProvenance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotDamageEvidenceTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	FMappingFixture Fixture; if (!Fixture.Initialize(*this)) { return false; }
	const AActor* Player = Fixture.Player.Controller->GetPawn();
	AActor* PlayerStateAlias = Fixture.Player.World->SpawnActor<AActor>();
	AActor* Enemy = Fixture.Player.World->SpawnActor<AActor>();
	TestTrue(TEXT("avatar damage is player-owned"), VBPlayBotDamageEvidence::IsFromPlayer(Player,Enemy,Player,PlayerStateAlias));
	TestTrue(TEXT("player-state alias is player-owned"), VBPlayBotDamageEvidence::IsFromPlayer(PlayerStateAlias,Enemy,Player,PlayerStateAlias));
	TestFalse(TEXT("unrelated attacker is not credited"), VBPlayBotDamageEvidence::IsFromPlayer(Enemy,Enemy,Player,PlayerStateAlias));
	TestFalse(TEXT("self damage is not an enemy hit"), VBPlayBotDamageEvidence::IsFromPlayer(Player,Player,Player,PlayerStateAlias));
	TestFalse(TEXT("absent instigator is not credited"), VBPlayBotDamageEvidence::IsFromPlayer(nullptr,Enemy,Player,PlayerStateAlias));
	return true;
}
#endif
