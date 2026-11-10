// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "PlayBot/VBPlayBotHospitalCombat.h"
#include "PlayBot/VBPlayBotDamageEvidence.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	bool ReadHealth(const UAbilitySystemComponent* ASC, double& Health, double& MaxHealth, FString& Error)
	{
		const UVBHealthAttributeSet* Set = ASC ? ASC->GetSet<UVBHealthAttributeSet>() : nullptr;
		if (!Set) { Error = TEXT("native combat actor has no health attribute owner"); return false; }
		Health = Set->GetHealth(); MaxHealth = Set->GetMaxHealth();
		if (!FMath::IsFinite(Health) || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0)
		{ Error = TEXT("native combat health values are invalid"); return false; }
		return true;
	}
}

bool FVBPlayBotCombatPolicy::IsValid(FString& Error) const
{
	if (AttackAction.IsNone() || !FMath::IsFinite(AttackRangeCm) || AttackRangeCm <= 0
		|| !FMath::IsFinite(AttackHeightCm) || AttackHeightCm <= 0
		|| !FMath::IsFinite(AttackIntervalSeconds) || AttackIntervalSeconds <= 0
		|| !FMath::IsFinite(MinStaminaFraction) || MinStaminaFraction < 0 || MinStaminaFraction >= 1
		|| !FMath::IsFinite(NoProgressSeconds) || NoProgressSeconds <= 0 || MaxAttacks <= 0 || MaxAttacks > 100)
	{ Error = TEXT("native fight policy requires explicit positive budgets/range and valid stamina fraction"); return false; }
	return true;
}

bool FVBPlayBotHospitalFightSpec::ParseTargets(const TArray<FString>& Rows, TArray<FVBPlayBotCombatTargetRef>& Out, FString& Error)
{
	TArray<FVBPlayBotCombatTargetRef> Parsed;
	TSet<FString> Paths; TSet<FGameplayTag> Facts;
	if (Rows.IsEmpty() || Rows.Num() > 8) { Error = TEXT("native fight needs one to eight declared targets"); return false; }
	for (const FString& Row : Rows)
	{
		TArray<FString> Fields; Row.ParseIntoArray(Fields,TEXT("|"),false);
		if (Fields.Num() != 3) { Error = TEXT("native enemy needs actor path, class and defeat fact"); return false; }
		FVBPlayBotCombatTargetRef Ref;
		Ref.ActorPath = Fields[0]; Ref.ClassPath = Fields[1];
		Ref.DefeatedFact = FGameplayTag::RequestGameplayTag(FName(*Fields[2]),false);
		if (!FSoftObjectPath(Ref.ActorPath).IsValid() || !Ref.ActorPath.Contains(TEXT(":PersistentLevel."))
			|| Ref.ClassPath.IsEmpty() || !Ref.DefeatedFact.IsValid() || Paths.Contains(Ref.ActorPath) || Facts.Contains(Ref.DefeatedFact))
		{ Error = TEXT("native enemy binding is missing, duplicate or unregistered"); return false; }
		Paths.Add(Ref.ActorPath); Facts.Add(Ref.DefeatedFact); Parsed.Add(MoveTemp(Ref));
	}
	Out = MoveTemp(Parsed); return true;
}

bool FVBPlayBotHospitalFightSpec::Load(const FVBPlayBotRunConfig& Config, FName Group, FString& Error)
{
	const FString Prefix = Group.ToString()+TEXT(".");
	Policy.AttackAction = FName(*Config.GetString(*(Prefix+TEXT("AttackAction"))));
	Policy.AttackRangeCm = Config.GetDouble(*(Prefix+TEXT("AttackRangeCm")),-1);
	Policy.AttackHeightCm = Config.GetDouble(*(Prefix+TEXT("AttackHeightCm")),-1);
	Policy.AttackIntervalSeconds = Config.GetDouble(*(Prefix+TEXT("AttackIntervalSeconds")),-1);
	Policy.MinStaminaFraction = Config.GetDouble(*(Prefix+TEXT("MinStaminaFraction")),-1);
	Policy.NoProgressSeconds = Config.GetDouble(*(Prefix+TEXT("NoProgressSeconds")),-1);
	Policy.MaxAttacks = Config.GetInt(*(Prefix+TEXT("MaxAttacks")),-1);
	return Policy.IsValid(Error) && ParseTargets(Config.GetArray(*(TEXT("Enemy.")+Group.ToString())),Targets,Error);
}

FVBPlayBotHospitalCombatObservation::~FVBPlayBotHospitalCombatObservation() { Unbind(); }
void FVBPlayBotHospitalCombatObservation::Unbind()
{
	for (FTarget& Target : Targets)
	{
		if (UAbilitySystemComponent* ASC = Target.ASC.Get())
		{
			if (FGameplayEventMulticastDelegate* Event = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_Hit)) { Event->Remove(Target.HitHandle); }
		}
	}
	Targets.Reset(); Player.Reset(); Controller.Reset(); PlayerState.Reset();
}

bool FVBPlayBotHospitalCombatObservation::Bind(UWorld* World, const FVBPlayBotHospitalFightSpec& Spec, FString& Error)
{
	Unbind();
	if (!World || World->GetNetMode() != NM_Standalone) { Error = TEXT("native first fight requires standalone damage-event ownership"); return false; }
	FVBPlayBotOpeningStep Common; Common.Kind = EVBPlayBotOpeningAction::Walk;
	if (!PlayerObservation.Bind(World,Common,Error)) { return false; }
	APlayerController* PC = World->GetFirstPlayerController();
	ACharacter* Pawn = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	const UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(World);
	if (!Pawn || !Story || Spec.Targets.IsEmpty()) { Error = TEXT("native first fight owners/targets are absent"); return false; }
	Player = Pawn; Controller = PC; PlayerState = Pawn->GetPlayerState();
	TSet<AActor*> Seen;
	for (const FVBPlayBotCombatTargetRef& Ref : Spec.Targets)
	{
		AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(FSoftObjectPath(Ref.ActorPath).ResolveObject());
		UClass* Expected = FSoftClassPath(Ref.ClassPath).TryLoadClass<AVBEnemyBase>();
		if (!Enemy || !Expected || !Enemy->IsA(Expected) || Enemy->GetWorld() != World || Seen.Contains(Enemy)
			|| Enemy->IsDead() || Story->HasFact(Ref.DefeatedFact))
		{ Error = TEXT("native enemy is missing, wrong, duplicated or already defeated: ")+Ref.ActorPath; Unbind(); return false; }
		FTarget Target; Target.Ref = Ref; Target.Actor = Enemy;
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy);
		double Health = 0;
		if (!ReadHealth(ASC,Health,Target.MaxHealth,Error)) { Unbind(); return false; }
		Target.ASC = ASC; Target.LastPosition = Enemy->GetActorLocation(); Targets.Add(MoveTemp(Target)); Seen.Add(Enemy);
	}
	for (int32 Index = 0; Index < Targets.Num(); ++Index)
	{
		Targets[Index].HitHandle = Targets[Index].ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit)
			.AddLambda([this,Index](const FGameplayEventData* Payload) { OnHit(Payload,Index); });
	}
	return true;
}

void FVBPlayBotHospitalCombatObservation::OnHit(const FGameplayEventData* Payload, int32 Index)
{
	if (!Payload || !Targets.IsValidIndex(Index) || !Player.IsValid() || !FMath::IsFinite(Payload->EventMagnitude) || Payload->EventMagnitude <= 0) { return; }
	const AActor* Instigator = Payload->Instigator.Get();
	if (VBPlayBotDamageEvidence::IsFromPlayer(Instigator,Targets[Index].Actor.Get(),Player.Get(),PlayerState.Get())) { ++Targets[Index].PlayerDamageHits; }
}

bool FVBPlayBotHospitalCombatObservation::Read(UWorld* World, FVBPlayBotHospitalCombatView& Out, FString& Error)
{
	if (!Player.IsValid() || !Controller.IsValid()) { Error = TEXT("native combat player/controller binding changed"); return false; }
	if (!PlayerObservation.Read(World,Out.Movement,Error)) { return false; }
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Player.Get());
	if (!ReadHealth(ASC,Out.PlayerHealth,Out.PlayerMaxHealth,Error)) { return false; }
	const UVBCombatAttributeSet* Combat = ASC->GetSet<UVBCombatAttributeSet>();
	const UVBHealthComponent* Health = UVBHealthComponent::FindFor(Player.Get());
	const UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(World);
	if (!Combat || !Health || !Story) { Error = TEXT("native player stamina/death/story owner is absent"); return false; }
	Out.Stamina = Combat->GetStamina(); Out.MaxStamina = Combat->GetMaxStamina(); Out.bPlayerDead = Health->HasStartedDeath();
	if (!FMath::IsFinite(Out.Stamina) || !FMath::IsFinite(Out.MaxStamina) || Out.MaxStamina <= 0)
	{ Error = TEXT("native player stamina values are invalid"); return false; }
	Out.Targets.Reset();
	for (FTarget& Target : Targets)
	{
		FVBPlayBotCombatTargetView View; View.Id = Target.Ref.DefeatedFact.GetTagName(); View.PlayerDamageHits = Target.PlayerDamageHits;
		View.bDefeatedFact = Story->HasFact(Target.Ref.DefeatedFact); View.Position = Target.LastPosition; View.MaxHealth = Target.MaxHealth;
		AVBEnemyBase* Enemy = Target.Actor.Get();
		if (!Enemy)
		{
			if (!View.bDefeatedFact) { Error = TEXT("native enemy vanished without its production defeat fact"); return false; }
			View.bDead = true;
		}
		else
		{
			if (!ReadHealth(Target.ASC.Get(),View.Health,View.MaxHealth,Error)) { return false; }
			View.Position = Target.LastPosition = Enemy->GetActorLocation(); View.bDead = Enemy->IsDead();
			FVector2D Screen;
			int32 Width = 0, Height = 0; Controller->GetViewportSize(Width,Height);
			View.bVisible = Controller->ProjectWorldLocationToScreen(View.Position,Screen,true) && Width > 0 && Height > 0
				&& Screen.X >= 0 && Screen.X <= Width && Screen.Y >= 0 && Screen.Y <= Height && Controller->LineOfSightTo(Enemy);
		}
		Out.Targets.Add(MoveTemp(View));
	}
	return true;
}

bool FVBPlayBotHospitalCombatActions::Configure(const FVBPlayBotCombatPolicy& InPolicy, FString& Error)
{
	if (!InPolicy.IsValid(Error)) { return false; }
	*this = FVBPlayBotHospitalCombatActions(); Policy = InPolicy; return true;
}
void FVBPlayBotHospitalCombatActions::NotifyInjected(int32 Id, double Seconds)
{
	if (Id == PressId) { ++InjectedAttacks; NextAttackSeconds = Seconds+Policy.AttackIntervalSeconds; }
}

EVBPlayBotOpeningResult FVBPlayBotHospitalCombatActions::Tick(const FVBPlayBotHospitalCombatView& View, double Seconds,
	FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error)
{
	Input.SetStickWant(FVector2D::ZeroVector); Input.SetLookWant(FVector2D::ZeroVector);
	if (!Policy.IsValid(Error)) { return EVBPlayBotOpeningResult::Failed; }
	if (View.bPlayerDead || View.PlayerHealth <= 0) { Error = TEXT("player died during the natural first fight"); return EVBPlayBotOpeningResult::Failed; }
	if (!View.Movement.bWeaponOwned) { Error = TEXT("native first fight lost its actual sword ownership"); return EVBPlayBotOpeningResult::Failed; }
	if (View.Targets.IsEmpty()) { Error = TEXT("native fight has no observed targets"); return EVBPlayBotOpeningResult::Failed; }
	int32 Hits = 0; bool bAllDead = true, bEachHit = true;
	const FVBPlayBotCombatTargetView* Nearest = nullptr; double Distance = TNumericLimits<double>::Max();
	for (const FVBPlayBotCombatTargetView& Target : View.Targets)
	{
		Hits += Target.PlayerDamageHits; bAllDead &= Target.bDead && Target.bDefeatedFact; bEachHit &= Target.PlayerDamageHits > 0;
		const double D = FVector::Dist2D(View.Movement.Position,Target.Position);
		if (!Target.bDead && Target.bVisible && D < Distance) { Distance = D; Nearest = &Target; }
	}
	if (ProgressSeconds < 0 || Hits > LastDamageHits) { ProgressSeconds = Seconds; LastDamageHits = Hits; }
	if (bAllDead)
	{
		Input.CancelPending(TEXT("declared targets defeated"));
		if (!bEachHit || InjectedAttacks == 0) { Error = TEXT("encounter completed without injected attacks and player damage to every target"); return EVBPlayBotOpeningResult::Failed; }
		if (CompleteSeconds < 0) { CompleteSeconds = Seconds; }
		return Seconds-CompleteSeconds >= 0.3 ? EVBPlayBotOpeningResult::PartialComplete : EVBPlayBotOpeningResult::Running;
	}
	if (Seconds-ProgressSeconds > Policy.NoProgressSeconds) { Error = TEXT("native first fight made no player-damage progress"); return EVBPlayBotOpeningResult::Failed; }
	if (InjectedAttacks >= Policy.MaxAttacks) { Error = TEXT("native first fight exhausted its attack budget"); return EVBPlayBotOpeningResult::Failed; }
	if (!View.Movement.bReady || !View.Movement.bGrounded) { return EVBPlayBotOpeningResult::Running; }
	if (!Nearest) { Input.SetLookWant(FVector2D(0.18,0)); return EVBPlayBotOpeningResult::Running; }
	const FVector Delta = Nearest->Position-View.Movement.Position;
	const double Angle = FMath::Abs(FMath::FindDeltaAngleDegrees(View.Movement.FacingYaw,Delta.Rotation().Yaw));
	const double Magnitude = Distance > Policy.AttackRangeCm
		? Human.GetProfile().MoveMagnitude * FMath::Clamp((Distance-Policy.AttackRangeCm)/160.0,0.25,1.0) : (Angle > 20 ? 0.25 : 0.0);
	Steering.Drive({Delta,View.Movement.MoveYaw,View.Movement.CameraYaw},Magnitude,Seconds,Input,Human);
	if (Distance > Policy.AttackRangeCm || FMath::Abs(Delta.Z) > Policy.AttackHeightCm || Angle > 20 || View.Stamina < View.MaxStamina*Policy.MinStaminaFraction
		|| Input.IsActive(PressId) || Seconds < NextAttackSeconds) { return EVBPlayBotOpeningResult::Running; }
	FVBPlayBotPress Press; Press.Action = Policy.AttackAction; Press.Cue = TEXT("first encounter light attack");
	Press.DelayMs = Human.SampleSimpleReactionMs(); Press.DueSeconds = Seconds+Press.DelayMs/1000.0; Press.HoldSeconds = Human.SampleHoldMs()/1000.0;
	PressId = Input.Schedule(Press);
	if (PressId == INDEX_NONE) { Error = TEXT("native first fight attack mapping/schedule failed"); return EVBPlayBotOpeningResult::Failed; }
	return EVBPlayBotOpeningResult::Running;
}
