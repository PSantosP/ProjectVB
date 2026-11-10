// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

// 전투 감각 비교용 자유 전투 촬영 (Fighter 전용) - 경공격 연타, 한 타 뒤 놓기, 타격 뒤 회피, 가드, BossClass 가 있으면 보스 연타
// 영상이 먼저라 접촉이나 입력을 못 얻은 단계는 실패, 불가 검사를 남기고 계속 가고, 준비가 빠졌을 때만 멈춘다
// 모든 수치는 ini 데이터다
class FVBPlayBotScenario_FighterFreeCombat : public FVBPlayBotScenarioBase
{
	// 단계가 자기 예산을 넘어 더 돌 수 있는 초 - 이것까지 넘으면 skill timeout 이 실행을 실패시킨다
	static constexpr double StepTimeoutMarginSeconds = 5.0;

public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString Weapon, Slot, AttackText, DodgeText, GuardText;
		if (!C.RequireString(TEXT("Weapon"), Weapon, OutError) || !C.RequireString(TEXT("WeaponAction"), Slot, OutError)
			|| !C.RequireString(TEXT("AttackAction"), AttackText, OutError) || !C.RequireString(TEXT("DodgeAction"), DodgeText, OutError)
			|| !C.RequireString(TEXT("GuardAction"), GuardText, OutError) || !C.RequireString(TEXT("EnemyClass"), EnemyClass, OutError))
		{
			return false;
		}

		Attack = FName(*AttackText);
		Dodge = FName(*DodgeText);
		Guard = FName(*GuardText);
		BossClass = C.GetString(TEXT("BossClass"));
		EnemyDistance = C.GetDouble(TEXT("EnemyDistance"), 400.0);
		AttackRange = C.GetDouble(TEXT("AttackRange"), 250.0);
		MashSeconds = C.GetDouble(TEXT("MashSeconds"), 0.25);
		ChainHits = C.GetInt(TEXT("ChainHits"), 6);
		ChainSeconds = C.GetDouble(TEXT("ChainSeconds"), 8.0);
		RestSeconds = C.GetDouble(TEXT("RestSeconds"), 1.5);
		GuardHoldSeconds = C.GetDouble(TEXT("GuardHoldSeconds"), 0.8);
		CancelWindowSeconds = C.GetDouble(TEXT("CancelWindowSeconds"), 1.0);
		const int32 ChainRepeats = C.GetInt(TEXT("ChainRepeats"), 3);
		const int32 SingleRepeats = C.GetInt(TEXT("SingleRepeats"), 3);
		const int32 DodgeRepeats = C.GetInt(TEXT("DodgeRepeats"), 3);
		const int32 GuardRepeats = C.GetInt(TEXT("GuardRepeats"), 3);
		const int32 BossChains = C.GetInt(TEXT("BossChains"), 2);
		if (EnemyDistance <= 0 || AttackRange <= 0 || MashSeconds <= 0 || ChainHits <= 0 || ChainSeconds <= 0 || RestSeconds < 0
			|| GuardHoldSeconds <= 0 || CancelWindowSeconds <= 0 || ChainRepeats < 0 || SingleRepeats < 0 || DodgeRepeats < 0
			|| GuardRepeats < 0 || BossChains < 0)
		{
			OutError = FString::Printf(TEXT("[%s] invalid distance/count/second parameters"), *C.Section);
			return false;
		}
		// 두 클래스를 지금 불러 둔다 - 보스 클래스가 병사 단계 뒤에 실패하면 실행이 늦게 끝난다
		if (!FSoftClassPath(EnemyClass).TryLoadClass<ACharacter>()
			|| (!BossClass.IsEmpty() && !FSoftClassPath(BossClass).TryLoadClass<ACharacter>()))
		{
			OutError = FString::Printf(TEXT("[%s] EnemyClass or BossClass did not load as a Character"), *C.Section);
			return false;
		}

		if (!VBPlayBotSkills::EnsureAction(Ctx, Attack, OutError) || !VBPlayBotSkills::EnsureAction(Ctx, Dodge, OutError)
			|| !VBPlayBotSkills::EnsureAction(Ctx, Guard, OutError))
		{
			return false;
		}

		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			if (Press.Action == Dodge || Press.Action == Guard)
			{
				LastCancelPressFrame = Press.InjectedFrame;
			}
		};

		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		Sequence.Then<FVBSkillDo>(TEXT("spawn soldier"), [this](FVBPlayBotContext& C2, FString& Error) { return SpawnEnemy(C2, EnemyClass, Error); });
		for (int32 Index = 0; Index < ChainRepeats; ++Index)
		{
			AddChain(TEXT("chain"), Index);
		}

		for (int32 Index = 0; Index < SingleRepeats; ++Index)
		{
			AddSingle(Index);
		}

		for (int32 Index = 0; Index < DodgeRepeats; ++Index)
		{
			AddCancel(TEXT("dodge"), Index, Dodge, 0.0, VBGameplayTags::State_Combat_Dodging);
		}

		for (int32 Index = 0; Index < GuardRepeats; ++Index)
		{
			AddCancel(TEXT("guard"), Index, Guard, GuardHoldSeconds, VBGameplayTags::State_Combat_Guarding);
		}

		if (!BossClass.IsEmpty() && BossChains > 0)
		{
			Sequence.Then<FVBSkillDo>(TEXT("swap soldier for boss"), [this](FVBPlayBotContext& C2, FString& Error)
			{
				if (Enemy.IsValid())
				{
					Enemy->Destroy();
				}

				return SpawnEnemy(C2, BossClass, Error);
			});
			for (int32 Index = 0; Index < BossChains; ++Index)
			{
				AddChain(TEXT("boss chain"), Index);
			}
		}

		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		// 취소 상태는 버튼을 놓은 뒤가 아니라 매 프레임 본다 - 누르고 있던 가드는 그때면 이미 끝난다
		if (WatchedState.IsValid() && StateSeenFrame < 0 && LastCancelPressFrame >= 0 && Ctx.PlayerASC
			&& Ctx.PlayerASC->HasMatchingGameplayTag(WatchedState))
		{
			StateSeenFrame = Ctx.Frame;
		}

		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		Ctx.OnPressInjected = nullptr;
		return State == EVBPlayBotState::Failed ? Fail(Sequence.GetFailure()) : State;
	}

	virtual void Teardown(FVBPlayBotContext& Ctx) override
	{
		Ctx.OnPressInjected = nullptr;
	}

private:
	bool SpawnEnemy(FVBPlayBotContext& Ctx, const FString& ClassPath, FString& Error)
	{
		UClass* Class = FSoftClassPath(ClassPath).TryLoadClass<ACharacter>();
		if (!Class)
		{
			Error = FString::Printf(TEXT("%s did not load as a Character"), *ClassPath);
			return false;
		}

		const FVector Forward = Ctx.Player->GetActorForwardVector().GetSafeNormal2D();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		Enemy = Ctx.World->SpawnActor<ACharacter>(Class, Ctx.Player->GetActorLocation() + Forward * EnemyDistance,
			(-Forward).Rotation(), Params);
		if (!Enemy.IsValid())
		{
			Error = FString::Printf(TEXT("%s spawn failed"), *ClassPath);
			return false;
		}

		if (!Enemy->GetController())
		{
			Enemy->SpawnDefaultController();
		}

		SpawnedClass = ClassPath;
		Ctx.Watch(Enemy.Get());
		Ctx.Target = Enemy.Get();
		return true;
	}

	// 단계 경계를 telemetry 에 남겨 로그 없이 영상을 자를 수 있게 한다 - 단계는 쉼 앞에서 끝난다
	void BeginPhase(FVBPlayBotContext& Ctx, const FString& Name)
	{
		PhaseName = Name;
		PhaseStartFrame = Ctx.Frame;
		Landed = 0;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"phase\",\"f\":%lld,\"name\":\"%s\",\"edge\":\"begin\"}"), Ctx.Frame, *Name));
	}

	void EndPhase(FVBPlayBotContext& Ctx, const FString& Note)
	{
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"phase\",\"f\":%lld,\"name\":\"%s\",\"edge\":\"end\"}"), Ctx.Frame, *PhaseName));
		Ctx.Checks->Look(PhaseName, PhaseStartFrame, Ctx.Frame, Note);
	}

	bool PhaseOver(const FVBPlayBotContext& Ctx, double Seconds) const { return Ctx.Frame - PhaseStartFrame >= Ctx.SecondsToFrames(Seconds); }

	// Want 번 접촉하거나 ChainSeconds 가 지날 때까지 공격한다 - MashSeconds 0 이면 접촉에만 반응한다
	void AddAttack(const FString& Name, int32 Want, double Mash)
	{
		// 공격 skill 은 대상이 없으면 실행을 실패시킨다 - 도중에 사라진 적은 같은 클래스로 다시 만들고 기록해 남은 단계를 이어 간다
		Sequence.Then<FVBSkillDo>(Name + TEXT(": target present"), [this, Name](FVBPlayBotContext& C2, FString& Error)
		{
			if (Enemy.IsValid())
			{
				return true;
			}

			C2.Checks->Unavailable(Name + TEXT(" had its enemy"), C2.Frame, TEXT("enemy was gone - respawned"));
			return SpawnEnemy(C2, SpawnedClass, Error);
		});
		Sequence.Then<FVBSkillAttackUntil>(Name + TEXT(": attack"), [this](FVBPlayBotContext&) -> AActor* { return Enemy.Get(); }, Attack, AttackRange,
			[this, Want](FVBPlayBotContext& C2, int32 Hits)
			{
				Landed = Hits;
				return Hits >= Want || PhaseOver(C2, ChainSeconds);
			}, ChainSeconds + StepTimeoutMarginSeconds, Mash);
	}

	void AddChain(const TCHAR* Kind, int32 Index)
	{
		const FString Name = FString::Printf(TEXT("%s %d"), Kind, Index + 1);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": begin"), [this, Name](FVBPlayBotContext& C2, FString&) { BeginPhase(C2, Name); return true; });
		AddAttack(Name, ChainHits, MashSeconds);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": end"), [this, Name](FVBPlayBotContext& C2, FString&)
		{
			C2.Checks->Expect(Name + TEXT(" reached the chain length"), Landed >= ChainHits, true, C2.Frame,
				FString::Printf(TEXT("{\"hits\":%d,\"want\":%d}"), Landed, ChainHits));
			C2.Input->ReleaseAll(C2.Frame);
			EndPhase(C2, FString::Printf(TEXT("%d contacts, mash every %.2f s"), Landed, MashSeconds));
			return true;
		});
		Sequence.Then<FVBSkillWait>(Name + TEXT(": rest"), RestSeconds);
	}

	void AddSingle(int32 Index)
	{
		const FString Name = FString::Printf(TEXT("single %d"), Index + 1);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": begin"), [this, Name](FVBPlayBotContext& C2, FString&) { BeginPhase(C2, Name); return true; });
		AddAttack(Name, 1, 0.0);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": release"), [this, Name](FVBPlayBotContext& C2, FString&)
		{
			C2.Checks->Expect(Name + TEXT(" landed one contact"), Landed >= 1, true, C2.Frame, FString::Printf(TEXT("{\"hits\":%d}"), Landed));
			C2.Input->ReleaseAll(C2.Frame);
			return true;
		});
		// 이 단계가 보여 주는 것은 한 타 뒤 회복이라 관찰은 단계 안에, 쉼은 그 뒤에 둔다
		Sequence.Then<FVBSkillWait>(Name + TEXT(": watch recovery"), RestSeconds);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": end"), [this](FVBPlayBotContext& C2, FString&) { EndPhase(C2, TEXT("one contact, no further input")); return true; });
		Sequence.Then<FVBSkillWait>(Name + TEXT(": rest"), RestSeconds);
	}

	// 접촉 한 번 뒤 사람 반응 시간만큼 지나 취소 버튼을 누르고, 누른 뒤 CancelWindowSeconds 안에 상태 태그를 기대한다 (Tick 에서 매 프레임 본다)
	void AddCancel(const TCHAR* Kind, int32 Index, FName Action, double HoldSeconds, FGameplayTag StateTag)
	{
		const FString Name = FString::Printf(TEXT("%s %d"), Kind, Index + 1);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": begin"), [this, Name](FVBPlayBotContext& C2, FString&)
		{
			BeginPhase(C2, Name);
			LastCancelPressFrame = -1;
			StateSeenFrame = -1;
			WatchedState = FGameplayTag();
			return true;
		});
		AddAttack(Name, 1, 0.0);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": arm"), [this, StateTag](FVBPlayBotContext& C2, FString&)
		{
			WatchedState = StateTag;
			ArmFrame = C2.Frame;
			return true;
		});
		Sequence.Then<FVBSkillPress>(Name + TEXT(": press"), Action, HoldSeconds);
		const double WindowSeconds = CancelWindowSeconds;
		Sequence.Then<FVBSkillWaitUntil>(Name + TEXT(": state or window end"), [this, WindowSeconds](FVBPlayBotContext& C2)
		{
			// 주입된 입력이 없으면 창을 arm 프레임부터 세므로, 입력을 잃어도 기다림이 끝난다
			const int64 From = LastCancelPressFrame >= 0 ? LastCancelPressFrame : ArmFrame;
			return StateSeenFrame >= 0 || C2.Frame - From >= C2.SecondsToFrames(WindowSeconds);
		}, CancelWindowSeconds + HoldSeconds + StepTimeoutMarginSeconds);
		Sequence.Then<FVBSkillDo>(Name + TEXT(": end"), [this, Name, StateTag](FVBPlayBotContext& C2, FString&)
		{
			const FString Check = Name + TEXT(" entered ") + StateTag.ToString();
			const FString Evidence = FString::Printf(TEXT("{\"hits\":%d,\"press_f\":%lld,\"state_f\":%lld}"), Landed, LastCancelPressFrame, StateSeenFrame);
			if (!C2.PlayerASC)
			{
				C2.Checks->Unavailable(Check, C2.Frame, TEXT("player has no ability system component"));
			}
			else if (Landed < 1 || LastCancelPressFrame < 0)
			{
				C2.Checks->Unavailable(Check, C2.Frame, Landed < 1 ? TEXT("no contact before the press") : TEXT("press was not injected"));
			}
			else
			{
				// Tick 은 누른 뒤의 상태만 적으므로 여기서 StateSeenFrame >= 누른 프레임이다
				const bool bInWindow = StateSeenFrame >= 0 && StateSeenFrame - LastCancelPressFrame <= C2.SecondsToFrames(CancelWindowSeconds);
				C2.Checks->Expect(Check, bInWindow, true, C2.Frame, Evidence);
			}

			WatchedState = FGameplayTag();
			C2.Input->ReleaseAll(C2.Frame);
			EndPhase(C2, FString::Printf(TEXT("press at f%lld, state at f%lld"), LastCancelPressFrame, StateSeenFrame));
			return true;
		});
		Sequence.Then<FVBSkillWait>(Name + TEXT(": rest"), RestSeconds);
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Enemy;
	FString EnemyClass;
	FString BossClass;
	// 지금 단계들이 싸우는 적의 클래스(병사, 다음은 보스) - 다시 만들 때 쓴다
	FString SpawnedClass;
	FName Attack;
	FName Dodge;
	FName Guard;
	double EnemyDistance = 400.0;
	double AttackRange = 250.0;
	double MashSeconds = 0.25;
	int32 ChainHits = 6;
	double ChainSeconds = 8.0;
	double RestSeconds = 1.5;
	double GuardHoldSeconds = 0.8;
	double CancelWindowSeconds = 1.0;
	FString PhaseName;
	int64 PhaseStartFrame = 0;
	int32 Landed = 0;
	FGameplayTag WatchedState;
	int64 ArmFrame = 0;
	int64 LastCancelPressFrame = -1;
	int64 StateSeenFrame = -1;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_FighterFreeCombat, "FighterFreeCombat")
