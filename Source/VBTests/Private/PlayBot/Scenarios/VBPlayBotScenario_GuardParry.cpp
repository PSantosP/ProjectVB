// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 가드/패링 기준선 - 앞의 병사가 봇을 친다. raw(가드 없이 한 대), block(내내 가드: raw 보다 덜 잃고 움찔 없음),
//   parry(공격 시작 뒤 정한 지연에 가드: 병사가 봇 몸이 instigator 인 stagger 이벤트를 받으면 패링) 세 단계를 잰다.
// 값은 데이터([Scenario.GuardParry])이고, ParryAttempts 안의 첫 패링으로 통과한다.
class FVBPlayBotScenario_GuardParry : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString SoldierClass, Weapon, Slot, GuardText;
		if (!C.RequireString(TEXT("SoldierClass"), SoldierClass, OutError) || !C.RequireString(TEXT("Weapon"), Weapon, OutError)
			|| !C.RequireString(TEXT("WeaponAction"), Slot, OutError) || !C.RequireString(TEXT("GuardAction"), GuardText,
				OutError))
		{
			return false;
		}

		Guard = FName(*GuardText);
		const double SearchRadius = C.GetDouble(TEXT("SearchRadius"), 2500.0);
		const double FrontDistance = C.GetDouble(TEXT("FrontDistance"), 250.0);
		const double RawWaitSeconds = C.GetDouble(TEXT("RawWaitSeconds"), 10.0);
		BlockHoldSeconds = C.GetDouble(TEXT("BlockHoldSeconds"), 8.0);
		FlinchWindowSeconds = C.GetDouble(TEXT("FlinchWindowSeconds"), 0.5);
		ParryDelayMs = C.GetDouble(TEXT("ParryDelayMs"), 0.0);
		ParryHoldSeconds = C.GetDouble(TEXT("ParryHoldSeconds"), 0.4);
		ParryAttempts = C.GetInt(TEXT("ParryAttempts"), 6);
		const double ParrySeconds = C.GetDouble(TEXT("ParrySeconds"), 40.0);

		Sequence.Then<FVBSkillEquip>(Weapon, FName(*Slot));
		Sequence.Then<FVBSkillDo>(TEXT("find soldier"), [this, SoldierClass, SearchRadius](FVBPlayBotContext& C2, FString& Error)
		{
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
			SoldierASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Found);
			if (!SoldierASC.IsValid() || !C2.PlayerASC)
			{
				Error = TEXT("soldier or player has no ability system component");
				return false;
			}
			// 패링 경직은 공격자에게 가는 허브의 Event.Combat.Hit 이고 instigator 가 가드한 몸이다
			ParryHandle = SoldierASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit)
				.AddLambda([this, &C2](const FGameplayEventData* Payload)
				{
					// parry 단계만 센다 - block 단계 가드가 우연히 패링 창 안에서 시작한 것은 아니다
					if (!Payload || Payload->Instigator != C2.Player || Phase != EPhase::Parry)
					{
						return;
					}

					++Parries;
					if (ParryFrame < 0)
					{
						ParryFrame = C2.Frame;
					}

					C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"parry\",\"f\":%lld,\"attempt\":%d,\"mag\":%.2f}"), C2.Frame, Attempts, Payload->EventMagnitude));
				});
			LastHealth = Health(C2);
			return true;
		});
		auto GetSoldier = [this](FVBPlayBotContext&) { return Soldier.Get(); };
		Sequence.Then<FVBSkillApproachPoint>(TEXT("step in front of the soldier"), [this, FrontDistance](FVBPlayBotContext& C2)
		{
			const AActor* S = Soldier.Get();
			return S ? S->GetActorLocation() + S->GetActorForwardVector().GetSafeNormal2D() * FrontDistance : C2.Player->GetActorLocation();
		}, 120.0, 25.0);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("turn the camera to the soldier"), [GetSoldier](FVBPlayBotContext& C2) { return VBPlayBotSkills::TurnCameraToward(C2, GetSoldier(C2)); }, 3.0);

		// Raw - 서서 한 대 맞는다
		Sequence.Then<FVBSkillDo>(TEXT("raw phase"), [this](FVBPlayBotContext&, FString&) { Phase = EPhase::Raw; return true; });
		Sequence.Then<FVBSkillWaitUntil>(TEXT("take one unguarded hit"), [this](FVBPlayBotContext&) { return RawLoss > 0.0f; }, RawWaitSeconds);

		// raw 타격의 움찔이 먼저 끝나야 한다 - 아니면 가드한 타격의 움찔로 읽힌다
		Sequence.Then<FVBSkillWaitUntil>(TEXT("raw flinch over"), [](FVBPlayBotContext& C2)
		{
			return C2.PlayerASC && !C2.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting);
		}, 3.0);
		// Block - 가드에 타격이 오거나 쥐는 시간이 끝날 때까지 가드를 쥔다
		Sequence.Then<FVBSkillDo>(TEXT("block phase: hold guard"), [this](FVBPlayBotContext& C2, FString& Error)
		{
			if (!VBPlayBotSkills::EnsureAction(C2, Guard, Error))
			{
				return false;
			}

			Phase = EPhase::Block;
			BlockPress = VBPlayBotSkills::ReactPress(C2, Guard, FKey(), TEXT("block phase"), C2.Frame, BlockHoldSeconds);
			if (BlockPress == INDEX_NONE)
			{
				Error = TEXT("guard press could not be scheduled");
			}

			return BlockPress != INDEX_NONE;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("a hit lands on the guard"), [this](FVBPlayBotContext& C2)
		{
			return GuardedFrame >= 0 || !C2.Input->IsActive(BlockPress);
		}, BlockHoldSeconds + 2.0);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("guard released"), [this](FVBPlayBotContext& C2) { return !C2.Input->IsActive(BlockPress); }, BlockHoldSeconds + 2.0);

		// Parry - 공격이 시작할 때마다 짧게 가드를 누른다
		Sequence.Then<FVBSkillDo>(TEXT("parry phase"), [this](FVBPlayBotContext&, FString&) { Phase = EPhase::Parry; return true; });
		Sequence.Then<FVBSkillWaitUntil>(TEXT("parry or attempts used"), [this](FVBPlayBotContext& C2)
		{
			return Parries > 0 || (Attempts >= ParryAttempts && !C2.Input->IsActive(ParryPress));
		}, ParrySeconds);
		Sequence.Then<FVBSkillWait>(TEXT("watch the riposte window"), 1.0);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		Observe(Ctx);
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			// 타격을 못 받은 단계는 깨진 실행이 아니라 잴 수 없음이다. setup 실패는 제 이름을 지킨다
			if (Phase == EPhase::None)
			{
				return Fail(Sequence.GetFailure());
			}

			Finish(Ctx, Sequence.GetFailure());
			return EVBPlayBotState::Succeeded;
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			Finish(Ctx, FString());
		}

		return State;
	}

	virtual void Teardown(FVBPlayBotContext& Ctx) override
	{
		if (UAbilitySystemComponent* A = SoldierASC.Get())
		{
			if (FGameplayEventMulticastDelegate* D = A->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_Hit))
			{
				D->Remove(ParryHandle);
			}
		}
	}

private:
	enum class EPhase : uint8 { None, Raw, Block, Parry };

	static float Health(const FVBPlayBotContext& Ctx)
	{
		return Ctx.PlayerASC ? Ctx.PlayerASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) : 0.0f;
	}

	void Observe(FVBPlayBotContext& Ctx)
	{
		const UAbilitySystemComponent* Player = Ctx.PlayerASC;
		const UAbilitySystemComponent* S = SoldierASC.Get();
		if (!Player || !S)
		{
			return;
		}

		const float Now = Health(Ctx);
		const float Loss = LastHealth - Now;
		LastHealth = Now;
		const bool bGuarding = Player->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Guarding);
		if (Loss > 0.0f)
		{
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"player_loss\",\"f\":%lld,\"loss\":%.2f,\"guarding\":%s,\"phase\":%d}"),
				Ctx.Frame, Loss, bGuarding ? TEXT("true") : TEXT("false"), static_cast<int32>(Phase)));
			if (Phase == EPhase::Raw && !bGuarding && RawLoss <= 0.0f)
			{
				RawLoss = Loss;
				RawFrame = Ctx.Frame;
			}

			if (Phase == EPhase::Block && bGuarding && GuardedFrame < 0)
			{
				GuardedLoss = Loss;
				GuardedFrame = Ctx.Frame;
			}
		}
		// 움찔 = 창 안에서 가드한 타격 뒤 반응 태그가 새로 켜지는 것
		const bool bReacting = Player->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting);
		if (GuardedFrame >= 0 && !bFlinched && Ctx.Frame - GuardedFrame <= Ctx.SecondsToFrames(FlinchWindowSeconds) && bReacting
			&& !bWasReacting)
		{
			bFlinched = true;
		}

		bWasReacting = bReacting;
		// parry 단계 - 병사 공격이 켜질 때마다 한 번의 시도다
		const bool bAttacking = S->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
		if (Phase == EPhase::Parry && bAttacking && !bWasAttacking && Parries == 0 && Attempts < ParryAttempts
			&& !Ctx.Input->IsActive(ParryPress))
		{
			++Attempts;
			ParryPress = VBPlayBotSkills::ReactPress(Ctx, Guard, FKey(), TEXT("soldier wind-up"),
				Ctx.Frame + Ctx.MsToFrames(ParryDelayMs), ParryHoldSeconds);
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"parry_attempt\",\"f\":%lld,\"attempt\":%d,\"delayMs\":%.0f}"), Ctx.Frame, Attempts, ParryDelayMs));
		}

		bWasAttacking = bAttacking;
	}

	void Finish(FVBPlayBotContext& Ctx, const FString& Why)
	{
		const FString Reason = Why.ReplaceCharWithEscapedChar();
		if (RawLoss <= 0.0f)
		{
			Ctx.Checks->Unavailable(TEXT("block_costs_less_than_raw"), Ctx.Frame,
				FString::Printf(TEXT("no unguarded hit landed (%s)"), *Reason));
		}
		else if (GuardedFrame < 0)
		{
			Ctx.Checks->Unavailable(TEXT("block_costs_less_than_raw"), Ctx.Frame,
				FString::Printf(TEXT("no hit landed on the guard (%s)"), *Reason));
		}
		else
		{
			Ctx.Checks->Expect(TEXT("block_costs_less_than_raw"), GuardedLoss < RawLoss, true, GuardedFrame,
				FString::Printf(TEXT("{\"raw\":%.2f,\"guarded\":%.2f,\"ratio\":%.3f}"), RawLoss, GuardedLoss, GuardedLoss / RawLoss));
			Ctx.Checks->Expect(TEXT("block_does_not_flinch"), !bFlinched, true, GuardedFrame);
		}

		if (Attempts == 0)
		{
			Ctx.Checks->Unavailable(TEXT("parry_lands"), Ctx.Frame,
				FString::Printf(TEXT("the soldier never started an attack in the parry phase (%s)"), *Reason));
		}
		else
		{
			Ctx.Checks->Expect(TEXT("parry_lands"), Parries > 0, true, ParryFrame >= 0 ? ParryFrame : Ctx.Frame,
				FString::Printf(TEXT("{\"attempts\":%d,\"parries\":%d,\"delayMs\":%.0f}"), Attempts, Parries, ParryDelayMs));
		}

		if (ParryFrame >= 0)
		{
			Ctx.Checks->Look(TEXT("parry"), FMath::Max<int64>(0, ParryFrame - 45), ParryFrame + 60, TEXT("guard flash, soldier staggers"));
		}

		if (GuardedFrame >= 0)
		{
			Ctx.Checks->Look(TEXT("block"), FMath::Max<int64>(0, GuardedFrame - 30), GuardedFrame + 45,
				TEXT("guard takes the hit, no flinch"));
		}
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Soldier;
	TWeakObjectPtr<UAbilitySystemComponent> SoldierASC;
	FDelegateHandle ParryHandle;
	FName Guard;
	EPhase Phase = EPhase::None;
	double BlockHoldSeconds = 8.0;
	double FlinchWindowSeconds = 0.5;
	double ParryDelayMs = 0.0;
	double ParryHoldSeconds = 0.4;
	int32 ParryAttempts = 6;
	int32 BlockPress = INDEX_NONE;
	int32 ParryPress = INDEX_NONE;
	int32 Attempts = 0;
	int32 Parries = 0;
	float LastHealth = 0.0f;
	float RawLoss = 0.0f;
	float GuardedLoss = 0.0f;
	int64 RawFrame = -1;
	int64 GuardedFrame = -1;
	int64 ParryFrame = -1;
	bool bFlinched = false;
	bool bWasReacting = false;
	bool bWasAttacking = false;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_GuardParry, "GuardParry")
