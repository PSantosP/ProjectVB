// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_Riposte.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 실제 입력으로 패링 반격 경로를 재는 플레이봇 - 병사 공격을 가드, 패링한 뒤 RiposteWindow 동안 약공격을 누른다.
// 한 번의 시도 = 반격 하나의 실행(입력, 발동, 종료, 입력 해제, Attacking 복귀), 정상 종료만 센다. 실제 재생 몽타주는 시도마다 기록한다.
// ForceCombo, 직접 발동 없음. 횟수와 타이밍은 [Scenario.FighterRiposte] 데이터, 빠진 단계는 명시 결과로 남긴다.
class FVBPlayBotScenario_FighterRiposte : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString SoldierClass, Weapon, Slot, GuardText, AttackText;
		if (!C.RequireString(TEXT("SoldierClass"), SoldierClass, OutError) || !C.RequireString(TEXT("Weapon"), Weapon, OutError)
			|| !C.RequireString(TEXT("WeaponAction"), Slot, OutError) || !C.RequireString(TEXT("GuardAction"), GuardText, OutError)
			|| !C.RequireString(TEXT("AttackAction"), AttackText, OutError))
		{
			return false;
		}

		Guard = FName(*GuardText);
		Attack = FName(*AttackText);
		const double SearchRadius = C.GetDouble(TEXT("SearchRadius"), 2500.0);
		const double FrontDistance = C.GetDouble(TEXT("FrontDistance"), 250.0);
		ParryDelayMs = C.GetDouble(TEXT("ParryDelayMs"), 0.0);
		ParryHoldSeconds = C.GetDouble(TEXT("ParryHoldSeconds"), 0.4);
		AttackHoldSeconds = C.GetDouble(TEXT("AttackHoldSeconds"), 0.1);
		WindowWaitSeconds = C.GetDouble(TEXT("WindowWaitSeconds"), 0.5);
		PressTimeoutSeconds = C.GetDouble(TEXT("PressTimeoutSeconds"), 1.5);
		RiposteTimeoutSeconds = C.GetDouble(TEXT("RiposteTimeoutSeconds"), 4.0);
		RecoveryTimeoutSeconds = C.GetDouble(TEXT("RecoveryTimeoutSeconds"), 2.0);
		RipostesWanted = C.GetInt(TEXT("RipostesWanted"), 3);
		ParryAttempts = C.GetInt(TEXT("ParryAttempts"), 30);
		RequiredMontages = C.GetArray(TEXT("RequireMontage"));
		const double RiposteSeconds = C.GetDouble(TEXT("RiposteSeconds"), 180.0);

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

			if (!VBPlayBotSkills::EnsureAction(C2, Guard, Error) || !VBPlayBotSkills::EnsureAction(C2, Attack, Error))
			{
				return false;
			}
			// 패링은 가드 쪽이 공격자에게 보내는 stagger 이벤트다(instigator = 막은 몸, impact 태그 없음).
			// 진짜 타격은 InstigatorTags 에 Combat.Impact.* 를 가진다.
			ParryHandle = SoldierASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit)
				.AddLambda([this, &C2](const FGameplayEventData* Payload)
				{
					if (!Payload || Payload->Instigator != C2.Player || !Payload->InstigatorTags.IsEmpty())
					{
						return;
					}

					C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"parry\",\"f\":%lld,\"step\":%d}"), C2.Frame, static_cast<int32>(Step)));
					if (!bActive || Step != EStep::WaitParry)
					{
						return;
					}

					++Parries;
					Current = FAttempt();
					Current.ParryFrame = C2.Frame;
					Step = EStep::WaitWindow;
				});
			// 반격 실행 자체: 플레이어 ASC 에서 riposte ability 의 발동과 끝.
			// 예약된 공격 입력은 Pressing 단계뿐이라 여기서 발동한 반격은 이 입력의 것이다.
			// 주입 콜백이 같은 프레임에 더 늦게 올 수 있어 먼저 오기를 요구하지 않는다.
			ActivatedHandle = C2.PlayerASC->AbilityActivatedCallbacks.AddLambda([this, &C2](UGameplayAbility* Ability)
			{
				if (Step != EStep::Pressing || !Cast<UVBGA_Riposte>(Ability))
				{
					return;
				}

				Current.ActivatedFrame = C2.Frame;
				Step = EStep::Riposting;
				C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"riposte_start\",\"f\":%lld,\"ability\":\"%s\"}"), C2.Frame, *GetNameSafe(Ability)));
			});
			EndedHandle = C2.PlayerASC->OnAbilityEnded.AddLambda([this, &C2](const FAbilityEndedData& Ended)
			{
				if (Step != EStep::Riposting || !Cast<UVBGA_Riposte>(Ended.AbilityThatEnded))
				{
					return;
				}

				Current.EndedFrame = C2.Frame;
				C2.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"riposte_end\",\"f\":%lld,\"cancelled\":%s}"), C2.Frame, Ended.bWasCancelled ? TEXT("true") : TEXT("false")));
				if (Ended.bWasCancelled)
				{
					Close(C2, TEXT("cancelled"));
					return;
				}

				Step = EStep::Recovering;
			});
			C2.OnPressInjected = [this, &C2](const FVBPlayBotPress& Press)
			{
				if (Press.Id == INDEX_NONE || Press.Id != Current.PressId)
				{
					return;
				}

				Current.InjectedFrame = Press.InjectedFrame >= 0 ? Press.InjectedFrame : C2.Frame;
			};
			// 이번 실행의 타격: 발동과 끝 사이에 플레이어가 병사에게, impact 태그를 달고 넣은 것.
			C2.OnHit = [this](const FVBPlayBotHit& Hit)
			{
				if (Step == EStep::Riposting && Hit.bFromPlayer && Hit.Victim.Get() == Soldier.Get() && !Hit.Impact.IsEmpty())
				{
					++Current.Hits;
				}
			};
			return true;
		});
		auto GetSoldier = [this](FVBPlayBotContext&) { return Soldier.Get(); };
		Sequence.Then<FVBSkillApproachPoint>(TEXT("step in front of the soldier"), [this, FrontDistance](FVBPlayBotContext& C2)
		{
			const AActor* S = Soldier.Get();
			return S ? S->GetActorLocation() + S->GetActorForwardVector().GetSafeNormal2D() * FrontDistance : C2.Player->GetActorLocation();
		}, 120.0, 25.0);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("turn the camera to the soldier"), [GetSoldier](FVBPlayBotContext& C2) { return VBPlayBotSkills::TurnCameraToward(C2, GetSoldier(C2)); }, 3.0);
		Sequence.Then<FVBSkillDo>(TEXT("riposte phase"), [this](FVBPlayBotContext&, FString&) { bActive = true; return true; });
		Sequence.Then<FVBSkillWaitUntil>(TEXT("ripostes done or parry attempts used"), [this](FVBPlayBotContext& C2)
		{
			if (Step != EStep::WaitParry)
			{
				return false;
			}

			return Successes >= RipostesWanted || (ParryTries >= ParryAttempts && !C2.Input->IsActive(ParryPress));
		}, RiposteSeconds);
		Sequence.Then<FVBSkillWait>(TEXT("watch the last recovery"), 1.0);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		Observe(Ctx);
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			// 패링, 시간이 다 떨어진 것은 측정 결과다. 준비 실패는 자기 이름을 유지한다.
			if (!bActive)
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
		Ctx.OnHit = nullptr;
		Ctx.OnPressInjected = nullptr;
		if (UAbilitySystemComponent* P = Ctx.PlayerASC)
		{
			P->AbilityActivatedCallbacks.Remove(ActivatedHandle);
			P->OnAbilityEnded.Remove(EndedHandle);
		}

		if (UAbilitySystemComponent* A = SoldierASC.Get())
		{
			if (FGameplayEventMulticastDelegate* D = A->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combat_Hit))
			{
				D->Remove(ParryHandle);
			}
		}
	}

private:
	// WaitParry -> (패링) WaitWindow -> (창 열림, 입력 예약) Pressing -> (입력 주입 + 반격 발동) Riposting
	//   -> (정상 종료) Recovering -> (입력 해제, Attacking 사라짐) 반격 1회. 다른 출구는 모두 이름 붙은 결과이고 다시 시도한다.
	enum class EStep : uint8 { WaitParry, WaitWindow, Pressing, Riposting, Recovering };

	// 관찰된 그대로의 패링 -> 반격 시도 하나.
	struct FAttempt
	{
		int64 ParryFrame = -1;
		int64 WindowFrame = -1;
		int32 PressId = INDEX_NONE;
		int64 ScheduledFrame = -1;
		int64 InjectedFrame = -1;
		int64 ActivatedFrame = -1;
		int64 MontageFrame = -1;
		int64 EndedFrame = -1;
		int64 RecoveredFrame = -1;
		FString Montage;
		int32 Hits = 0;
		FString Outcome; // riposte | no_window | no_press | no_activation | cancelled | no_end | no_recovery | no_montage
	};

	static FString PlayerMontage(const FVBPlayBotContext& Ctx)
	{
		const USkeletalMeshComponent* Mesh = Ctx.Player ? Ctx.Player->GetMesh() : nullptr;
		const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
		return Montage ? Montage->GetName() : FString();
	}

	void Observe(FVBPlayBotContext& Ctx)
	{
		const UAbilitySystemComponent* Player = Ctx.PlayerASC;
		const UAbilitySystemComponent* S = SoldierASC.Get();
		if (!bActive || !Player || !S)
		{
			return;
		}

		// 병사 공격이 시작될 때마다 정해진 지연 뒤 한 번씩 가드한다(GuardParry 의 패링 단계).
		const bool bAttacking = S->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
		if (Step == EStep::WaitParry && bAttacking && !bWasAttacking && ParryTries < ParryAttempts && Successes < RipostesWanted
			&& !Ctx.Input->IsActive(ParryPress))
		{
			++ParryTries;
			ParryPress = VBPlayBotSkills::ReactPress(Ctx, Guard, FKey(), TEXT("soldier wind-up"),
				Ctx.Frame + Ctx.MsToFrames(ParryDelayMs), ParryHoldSeconds);
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"parry_attempt\",\"f\":%lld,\"attempt\":%d,\"delayMs\":%.0f}"), Ctx.Frame, ParryTries, ParryDelayMs));
		}

		bWasAttacking = bAttacking;

		switch (Step)
		{
		case EStep::WaitWindow:
			if (Player->HasMatchingGameplayTag(VBGameplayTags::State_Combat_RiposteWindow))
			{
				// 창이 열린 걸 보고 사람이 반응한 뒤 누르는 진짜 약공격 버튼.
				Current.WindowFrame = Ctx.Frame;
				AttackPress = VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), TEXT("riposte window"), Ctx.Frame, AttackHoldSeconds);
				Current.PressId = AttackPress;
				Current.ScheduledFrame = Ctx.Frame;
				Step = EStep::Pressing;
				Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"riposte_window\",\"f\":%lld,\"parry_f\":%lld,\"press\":%d}"), Ctx.Frame, Current.ParryFrame, AttackPress));
			}
			else if (Ctx.Frame - Current.ParryFrame > Ctx.SecondsToFrames(WindowWaitSeconds))
			{
				Close(Ctx, TEXT("no_window"));
			}
			break;
		case EStep::Pressing:
			if (Ctx.Frame - Current.ScheduledFrame > Ctx.SecondsToFrames(PressTimeoutSeconds))
			{
				Close(Ctx, Current.InjectedFrame < 0 ? TEXT("no_press") : TEXT("no_activation"));
			}
			break;
		case EStep::Riposting:
		{
			const FString Montage = PlayerMontage(Ctx);
			if (Current.Montage.IsEmpty() && Montage.Contains(TEXT("Riposte")))
			{
				Current.Montage = Montage;
				Current.MontageFrame = Ctx.Frame;
				Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"riposte_montage\",\"f\":%lld,\"montage\":\"%s\"}"), Ctx.Frame, *Montage));
			}

			if (Ctx.Frame - Current.ActivatedFrame > Ctx.SecondsToFrames(RiposteTimeoutSeconds))
			{
				Close(Ctx, TEXT("no_end"));
			}
			break;
		}
		case EStep::Recovering:
			if (!Ctx.Input->IsActive(AttackPress) && !Player->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking))
			{
				Current.RecoveredFrame = Ctx.Frame;
				if (Current.Montage.IsEmpty())
				{
					Close(Ctx, TEXT("no_montage"));
				}
				else
				{
					++Successes;
					Close(Ctx, TEXT("riposte"));
				}
			}
			else if (Ctx.Frame - Current.EndedFrame > Ctx.SecondsToFrames(RecoveryTimeoutSeconds))
			{
				Close(Ctx, TEXT("no_recovery"));
			}
			break;
		default:
			break;
		}
	}

	void Close(FVBPlayBotContext& Ctx, const TCHAR* Outcome)
	{
		Current.Outcome = Outcome;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"riposte_try\",\"f\":%lld,\"outcome\":\"%s\",\"montage\":\"%s\",\"hits\":%d,\"press\":%d,")
			TEXT("\"parry_f\":%lld,\"window_f\":%lld,\"sched_f\":%lld,\"inj_f\":%lld,\"act_f\":%lld,\"montage_f\":%lld,\"end_f\":%lld,\"rec_f\":%lld}"),
			Ctx.Frame, Outcome, *Current.Montage, Current.Hits, Current.PressId, Current.ParryFrame, Current.WindowFrame, Current.ScheduledFrame,
			Current.InjectedFrame, Current.ActivatedFrame, Current.MontageFrame, Current.EndedFrame, Current.RecoveredFrame));
		Attempts.Add(Current);
		Current = FAttempt();
		Step = EStep::WaitParry;
	}

	void Finish(FVBPlayBotContext& Ctx, const FString& Why)
	{
		const FString Reason = Why.ReplaceCharWithEscapedChar();
		FString List;
		for (const FAttempt& A : Attempts)
		{
			List += FString::Printf(TEXT("%s{\"outcome\":\"%s\",\"montage\":\"%s\",\"hits\":%d,\"parry_f\":%lld,\"inj_f\":%lld}"),
				List.IsEmpty() ? TEXT("") : TEXT(","), *A.Outcome, *A.Montage, A.Hits, A.ParryFrame, A.InjectedFrame);
		}

		const FString Evidence = FString::Printf(TEXT("{\"parryTries\":%d,\"parries\":%d,\"ripostes\":%d,\"wanted\":%d,\"tries\":[%s]}"),
			ParryTries, Parries, Successes, RipostesWanted, *List);
		if (ParryTries == 0)
		{
			Ctx.Checks->Unavailable(TEXT("parry_lands"), Ctx.Frame,
				FString::Printf(TEXT("the soldier never started an attack (%s)"), *Reason));
		}
		else
		{
			Ctx.Checks->Expect(TEXT("parry_lands"), Parries > 0, true, Ctx.Frame, Evidence);
		}

		if (Parries == 0)
		{
			Ctx.Checks->Unavailable(TEXT("ripostes_completed"), Ctx.Frame, FString::Printf(TEXT("no parry landed (%s)"), *Reason));
		}
		else
		{
			Ctx.Checks->Expect(TEXT("ripostes_completed"), Successes >= RipostesWanted, true, Ctx.Frame, Evidence);
		}

		int32 Whiffs = 0;
		for (const FAttempt& A : Attempts)
		{
			if (A.Outcome == TEXT("riposte") && A.Hits == 0)
			{
				++Whiffs;
			}
		}

		if (Successes > 0)
		{
			Ctx.Checks->Expect(TEXT("every_riposte_hits"), Whiffs == 0, true, Ctx.Frame, Evidence);
		}

		for (const FString& Name : RequiredMontages)
		{
			const FAttempt* Seen = Attempts.FindByPredicate([&Name](const FAttempt& A) { return A.Outcome == TEXT("riposte") && A.Montage == Name; });
			if (Successes == 0)
			{
				Ctx.Checks->Unavailable(FString::Printf(TEXT("riposte_montage_%s"), *Name), Ctx.Frame, TEXT("no riposte completed"));
			}
			else
			{
				Ctx.Checks->Expect(FString::Printf(TEXT("riposte_montage_%s"), *Name), Seen != nullptr, true,
					Seen ? Seen->MontageFrame : Ctx.Frame, Evidence);
			}
		}

		for (int32 i = 0; i < Attempts.Num(); ++i)
		{
			const FAttempt& A = Attempts[i];
			if (A.Montage.IsEmpty())
			{
				continue;
			}

			Ctx.Checks->Look(FString::Printf(TEXT("riposte_%d_%s_%s"), i, *A.Montage, *A.Outcome), FMath::Max<int64>(0, A.ParryFrame - 45),
				(A.RecoveredFrame >= 0 ? A.RecoveredFrame : A.EndedFrame >= 0 ? A.EndedFrame : Ctx.Frame) + 45,
				TEXT("parry, riposte approach and contact, soldier reaction, recovery"));
		}
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<ACharacter> Soldier;
	TWeakObjectPtr<UAbilitySystemComponent> SoldierASC;
	FDelegateHandle ParryHandle;
	FDelegateHandle ActivatedHandle;
	FDelegateHandle EndedHandle;
	FName Guard;
	FName Attack;
	TArray<FString> RequiredMontages;
	TArray<FAttempt> Attempts;
	FAttempt Current;
	EStep Step = EStep::WaitParry;
	bool bActive = false;
	bool bWasAttacking = false;
	double ParryDelayMs = 0.0;
	double ParryHoldSeconds = 0.4;
	double AttackHoldSeconds = 0.1;
	double WindowWaitSeconds = 0.5;
	double PressTimeoutSeconds = 1.5;
	double RiposteTimeoutSeconds = 4.0;
	double RecoveryTimeoutSeconds = 2.0;
	int32 RipostesWanted = 3;
	int32 ParryAttempts = 30;
	int32 ParryTries = 0;
	int32 Parries = 0;
	int32 Successes = 0;
	int32 ParryPress = INDEX_NONE;
	int32 AttackPress = INDEX_NONE;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_FighterRiposte, "FighterRiposte")
