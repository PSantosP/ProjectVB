// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 갑옷 깨기 시나리오 - L_VBTest 앞 콤보 병사를 3번 치고, 반격의 breakable armor 가 뜰 때까지 멈췄다가 그 위로 친다.
//   PASS = 갑옷이 보이고, 그동안 poise 가 깨지고, 병사가 stagger 한다. 뒤쪽 보스 troll 은 기록만 한다.
class FVBPlayBotScenario_ArmorBreak : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString SoldierClass;
		FString Weapon;
		FString Slot;
		FString AttackText;
		if (!C.RequireString(TEXT("SoldierClass"), SoldierClass, OutError) || !C.RequireString(TEXT("Weapon"), Weapon, OutError)
			|| !C.RequireString(TEXT("WeaponAction"), Slot, OutError) || !C.RequireString(TEXT("AttackAction"), AttackText,
				OutError))
		{
			return false;
		}

		const FName Attack(*AttackText);
		const double SearchRadius = C.GetDouble(TEXT("SearchRadius"), 2500.0);
		const double Range = C.GetDouble(TEXT("AttackRange"), 170.0);
		const int32 HitsBeforePause = C.GetInt(TEXT("HitsBeforePause"), 3);
		const double ArmorWaitSeconds = C.GetDouble(TEXT("ArmorWaitSeconds"), 4.0);
		const double BreakSeconds = C.GetDouble(TEXT("BreakSeconds"), 5.0);
		TrollClass = C.GetString(TEXT("TrollClass"));
		SoldierHealth = C.GetDouble(TEXT("SoldierHealth"), 0.0);
		// 반격은 2초 창 안에 3타가 필요하다 - 접촉마다 반응하면 간격이 너무 벌어진다.
		const double MashSeconds = C.GetDouble(TEXT("MashSeconds"), 0.0);

		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		Sequence.Then<FVBSkillDo>(TEXT("find soldier"), [this, SoldierClass, SearchRadius](FVBPlayBotContext& C2, FString& Error)
		{
			// 보스도 적 base 를 상속하므로 병사로 잡지 않게 뺀다.
			ACharacter* Found = Cast<ACharacter>(C2.FindNearest(FSoftClassPath(SoldierClass).TryLoadClass<ACharacter>(), NAME_None,
				SearchRadius,
				FSoftClassPath(TEXT("/Script/VB.VBBossBase")).TryLoadClass<AActor>()));
			if (!Found)
			{
				Error = FString::Printf(TEXT("no %s within %.0f cm of the start"), *SoldierClass, SearchRadius);
				return false;
			}

			Soldier = Found;
			C2.Target = Found;
			C2.Watch(Found);
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Found))
			{
				SoldierASC = ASC;
				// 배치된 병사는 ExecCmds 전에 초기화돼 vb.Combat.EnemyHealthScale 이 안 닿으니 직접 체력을 넣는다.
				//   체력이 넉넉해야 bleed burst 동안 finisher 문턱 위에 남는다.
				if (SoldierHealth > 0.0)
				{
					ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), SoldierHealth);
					ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), SoldierHealth);
				}

				PoiseHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_PoiseBroken)
					.AddLambda([this, &C2](const FGameplayEventData*)
					{
						const UAbilitySystemComponent* A = SoldierASC.Get();
						// HandleGameplayEvent 는 listener 보다 능력을 먼저 발동해 stagger 가 이미 갑옷 tag 를 지웠다.
						//   그래서 이번 프레임 타격 전에 잰 갑옷 상태를 쓴다.
						const bool bBreakable = bArmorLastFrame || (A
							&& A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor_Breakable));
						// 깨질 때의 체력과 finisher 창 - 창 안에서의 깨짐은 설계상 거절된다.
						const float MaxHealth = A ? A->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute()) : 0.f;
						const float HealthRatio = MaxHealth > 0.f ? A->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) / MaxHealth : -1.f;
						const bool bWindow = A && A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
						C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"poise_broken\",\"f\":%lld,\"breakableArmor\":%s,\"health\":%.3f,\"finisherWindow\":%s}"),
							C2.Frame, bBreakable ? TEXT("true") : TEXT("false"), HealthRatio, bWindow ? TEXT("true") : TEXT("false")));
						if (bArmorPhase && BrokenFrame < 0)
						{
							BrokenFrame = C2.Frame;
							bBrokenUnderArmor = bBreakable;
						}
					});
			}

			if (!TrollClass.IsEmpty())
			{
				Troll = C2.FindNearest(FSoftClassPath(TrollClass).TryLoadClass<AActor>(), NAME_None, 6000.0);
				if (Troll.IsValid())
				{
					C2.Watch(Troll.Get());
				}
			}

			C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"actors\",\"f\":%lld,\"soldier\":\"%s\",\"soldierCm\":%.0f,\"troll\":\"%s\",\"trollCm\":%.0f}"),
				C2.Frame, *C2.Label(Found), C2.Distance2D(Found), *C2.Label(Troll.Get()), Troll.IsValid() ? C2.Distance2D(Troll.Get()) : -1.0));
			if (!SoldierASC.IsValid())
			{
				Error = TEXT("soldier has no ability system component");
			}

			return SoldierASC.IsValid();
		});
		auto GetSoldier = [this](FVBPlayBotContext&) { return Soldier.Get(); };
		// Front 는 앞에 서서 병사가 봇을 보게 하고, Behind 는 뒤에서 먼저 쳐 damage sense 로 교전하게 만든다.
		const double FrontDistance = C.GetDouble(TEXT("FrontDistance"), 350.0);
		const double BehindDistance = C.GetDouble(TEXT("BehindDistance"), 350.0);
		const double NoticeSeconds = C.GetDouble(TEXT("NoticeSeconds"), 2.0);
		EngageSeconds = C.GetDouble(TEXT("EngageSeconds"), 6.0);
		// 접근 skill 은 직선으로 걸으니 +Waypoint=X,Y 구간으로 지형을 돌아간다.
		for (const FString& Line : C.GetArray(TEXT("Waypoint")))
		{
			FString X, Y;
			if (!Line.Split(TEXT(","), &X, &Y) || !X.TrimStartAndEnd().IsNumeric() || !Y.TrimStartAndEnd().IsNumeric())
			{
				OutError = FString::Printf(TEXT("[%s] Waypoint '%s' must be X,Y"), *C.Section, *Line);
				return false;
			}

			const FVector2D Point(FCString::Atod(*X), FCString::Atod(*Y));
			Sequence.Then<FVBSkillApproachPoint>(FString::Printf(TEXT("waypoint %.0f,%.0f"), Point.X, Point.Y),
				[Point](FVBPlayBotContext& C2) { return FVector(Point.X, Point.Y, C2.Player->GetActorLocation().Z); }, 120.0, 25.0);
		}

		bBehind = C.GetString(TEXT("ApproachSide"), TEXT("Front")) == TEXT("Behind");
		if (bBehind)
		{
			Sequence.Then<FVBSkillApproachPoint>(TEXT("step behind the soldier"), [this, BehindDistance](FVBPlayBotContext& C2)
			{
				const AActor* S = Soldier.Get();
				return S ? S->GetActorLocation() - S->GetActorForwardVector().GetSafeNormal2D() * BehindDistance : C2.Player->GetActorLocation();
			}, 120.0, 25.0);
		}
		else
		{
			Sequence.Then<FVBSkillApproachPoint>(TEXT("step in front of the soldier"), [this, FrontDistance](FVBPlayBotContext& C2)
			{
				const AActor* S = Soldier.Get();
				return S ? S->GetActorLocation() + S->GetActorForwardVector().GetSafeNormal2D() * FrontDistance : C2.Player->GetActorLocation();
			}, 120.0, 25.0);
			Sequence.Then<FVBSkillWaitUntil>(TEXT("turn the camera to the soldier"), [GetSoldier](FVBPlayBotContext& C2) { return VBPlayBotSkills::TurnCameraToward(C2, GetSoldier(C2)); }, 3.0);
			Sequence.Then<FVBSkillWait>(TEXT("let it notice the bot"), NoticeSeconds);
		}

		Sequence.Then<FVBSkillAttackUntil>(TEXT("hit the soldier"), GetSoldier, Attack, Range,
			[HitsBeforePause](FVBPlayBotContext&, int32 Hits) { return Hits >= HitsBeforePause; }, 20.0, MashSeconds);
		Sequence.Then<FVBSkillDo>(TEXT("pause begins"), [this](FVBPlayBotContext&, FString&) { bPausePhase = true; return true; });
		// 멈추고 반격의 breakable armor 를 기다린다 - 사람이 예비 동작을 보는 자리다.
		Sequence.Then<FVBSkillWaitUntil>(TEXT("pause until the counter armor"), [this](FVBPlayBotContext& C2)
		{
			const UAbilitySystemComponent* A = SoldierASC.Get();
			if (A && A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor_Breakable) && ArmorFrame < 0)
			{
				ArmorFrame = C2.Frame;
			}

			return ArmorFrame >= 0;
		}, ArmorWaitSeconds);
		Sequence.Then<FVBSkillDo>(TEXT("armor seen"), [this](FVBPlayBotContext& C2, FString&)
		{
			C2.Checks->Expect(TEXT("counter_armor_seen"), ArmorFrame >= 0, true, ArmorFrame);
			bArmorPhase = true;
			return true;
		});
		Sequence.Then<FVBSkillAttackUntil>(TEXT("hit through the armor"), GetSoldier, Attack, Range,
			[this](FVBPlayBotContext&, int32) { return BrokenFrame >= 0; }, BreakSeconds);
		Sequence.Then<FVBSkillWait>(TEXT("watch the stagger"), 1.0);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (const UAbilitySystemComponent* A = SoldierASC.Get())
		{
			bArmorLastFrame = A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor_Breakable);
			if (BrokenFrame >= 0 && StaggerFrame < 0 && A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Staggered))
			{
				StaggerFrame = Ctx.Frame;
			}

			if (bBehind && FirstHitFrame >= 0 && EngageFrame < 0 && A->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking))
			{
				EngageFrame = Ctx.Frame;
			}
		}

		if (bBehind && FirstHitFrame < 0 && Soldier.IsValid() && Ctx.HitsByPlayer.FindRef(TWeakObjectPtr<AActor>(Soldier.Get())) > 0)
		{
			// 첫 타격 때 병사 정면과 봇 방향 사이 각도 - 90 을 넘으면 뒤에서 친 것이다.
			const FVector ToBot = (Ctx.Player->GetActorLocation() - Soldier->GetActorLocation()).GetSafeNormal2D();
			FirstHitAngle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Soldier->GetActorForwardVector().GetSafeNormal2D(), ToBot), -1.0, 1.0)));
			FirstHitFrame = Ctx.Frame;
		}

		if (const UAbilitySystemComponent* T = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Troll.Get()))
		{
			bTrollAttacked |= T->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
		}

		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			// 갑옷이나 깨짐이 없으면 실행 고장이 아니라 체크리스트 FAIL 로 적고 정상 종료한다.
			//   멈춤 전 실패는 준비 문제라 제 이름을 유지한다.
			if (!bPausePhase) Ctx.Checks->Expect(TEXT("step_completed"), false, true, Ctx.Frame, FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
			else if (ArmorFrame < 0) Ctx.Checks->Expect(TEXT("counter_armor_seen"), false, true, Ctx.Frame, FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
			else if (BrokenFrame < 0) Ctx.Checks->Expect(TEXT("armor_broken_under_armor"), false, true, Ctx.Frame, FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
			else
			{
				return Fail(Sequence.GetFailure());
			}

			Finish(Ctx);
			return EVBPlayBotState::Succeeded;
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			Ctx.Checks->Expect(TEXT("armor_broken_under_armor"), bBrokenUnderArmor, true, BrokenFrame,
				FString::Printf(TEXT("{\"armorFrame\":%lld,\"brokenFrame\":%lld}"), ArmorFrame, BrokenFrame));
			Ctx.Checks->Expect(TEXT("soldier_staggered_after_break"), StaggerFrame >= 0, true, StaggerFrame);
			Ctx.Checks->Look(TEXT("armor_break"), ArmorFrame - 30, (StaggerFrame >= 0 ? StaggerFrame : BrokenFrame) + 60,
				TEXT("counter, break, stagger"));
			Finish(Ctx);
		}

		return State;
	}

	virtual void Teardown(FVBPlayBotContext& Ctx) override
	{
		if (UAbilitySystemComponent* A = SoldierASC.Get())
		{
			if (FGameplayEventMulticastDelegate* D = A->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_PoiseBroken))
			{
				D->Remove(PoiseHandle);
			}
		}
	}

private:
	void Finish(FVBPlayBotContext& Ctx)
	{
		if (bBehind)
		{
			const FString Name = TEXT("engaged_after_hit_from_behind");
			if (FirstHitFrame < 0)
			{
				Ctx.Checks->Unavailable(Name, Ctx.Frame, TEXT("no hit on the soldier landed"));
			}
			else if (FirstHitAngle <= 90.0)
			{
				Ctx.Checks->Unavailable(Name, FirstHitFrame,
					FString::Printf(TEXT("first hit came from %.0f deg, not from behind"), FirstHitAngle));
			}
			else Ctx.Checks->Expect(Name, EngageFrame >= 0 && EngageFrame - FirstHitFrame <= Ctx.SecondsToFrames(EngageSeconds), true,
				EngageFrame >= 0 ? EngageFrame : FirstHitFrame,
				FString::Printf(TEXT("{\"firstHit\":%lld,\"angle\":%.1f,\"engage\":%lld}"), FirstHitFrame, FirstHitAngle, EngageFrame));
		}

		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"troll\",\"f\":%lld,\"found\":%s,\"attacked\":%s}"),
			Ctx.Frame, Troll.IsValid() ? TEXT("true") : TEXT("false"), bTrollAttacked ? TEXT("true") : TEXT("false")));
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Soldier;
	TWeakObjectPtr<UAbilitySystemComponent> SoldierASC;
	TWeakObjectPtr<AActor> Troll;
	FString TrollClass;
	double SoldierHealth = 0.0;
	FDelegateHandle PoiseHandle;
	bool bArmorPhase = false;
	bool bPausePhase = false;
	bool bBrokenUnderArmor = false;
	bool bArmorLastFrame = false;
	bool bTrollAttacked = false;
	bool bBehind = false;
	double EngageSeconds = 6.0;
	double FirstHitAngle = -1.0;
	int64 FirstHitFrame = -1;
	int64 EngageFrame = -1;
	int64 ArmorFrame = -1;
	int64 BrokenFrame = -1;
	int64 StaggerFrame = -1;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_ArmorBreak, "ArmorBreak")
