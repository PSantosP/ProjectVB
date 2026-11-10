// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/Scenarios/VBPlayBotScenario_FighterLightStop.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_FighterLightStop, "FighterLightStop")

namespace
{
	// 스틱을 이만큼 쥐어도 pawn 이 안 움직이면 느린 출발이 아니라 패드 Move 경로가 깨진 것이다.
	constexpr int64 MoveRouteFrames = 30;
	constexpr double MoveRouteMinSpeed = 5.0;
	constexpr double MoveRouteConfirmSpeed = 50.0;
}

bool FVBPlayBotScenario_FighterLightStop::Setup(FVBPlayBotContext& Ctx, FString& OutError)
{
	const FVBPlayBotRunConfig& C = *Ctx.Config;
	FString Text;
	if (!C.RequireString(TEXT("Weapon"), Weapon, OutError) || !C.RequireString(TEXT("EnemyClass"), EnemyClass, OutError))
	{
		return false;
	}

	if (!C.RequireString(TEXT("AttackAction"), Text, OutError))
	{
		return false;
	}

	Params.AttackAction = FName(*Text);
	if (!C.RequireString(TEXT("WeaponAction"), Text, OutError))
	{
		return false;
	}

	WeaponAction = FName(*Text);
	EnemyDistance = C.GetDouble(TEXT("EnemyDistance"), EnemyDistance);
	TargetYawOffsetDeg = C.GetDouble(TEXT("TargetYawOffsetDeg"), TargetYawOffsetDeg);
	Params.AttackRange = C.GetDouble(TEXT("AttackRange"), Params.AttackRange);
	Params.PracticeStopAfter = C.GetInt(TEXT("PracticeStopAfter"), Params.PracticeStopAfter);
	Params.TrialTimeoutSeconds = C.GetDouble(TEXT("TrialTimeoutSeconds"), Params.TrialTimeoutSeconds);
	Params.ContactTimeoutSeconds = C.GetDouble(TEXT("ContactTimeoutSeconds"), Params.ContactTimeoutSeconds);
	Params.HoldAwaySeconds = C.GetDouble(TEXT("HoldAwaySeconds"), Params.HoldAwaySeconds);
	Params.AwayAfterWindowFrames = C.GetInt(TEXT("AwayAfterWindowFrames"), Params.AwayAfterWindowFrames);
	if (Params.AwayAfterWindowFrames < -1 || Params.AwayAfterWindowFrames == 0)
	{
		OutError = TEXT("AwayAfterWindowFrames must be -1 (prediction) or positive (diagnostic fresh push)");
		return false;
	}

	SettleSeconds = C.GetDouble(TEXT("SettleSeconds"), SettleSeconds);
	Repetitions = C.GetInt(TEXT("Repetitions"), Repetitions);
	PracticeTrials = C.GetInt(TEXT("PracticeTrials"), PracticeTrials);
	for (const FString& Value : C.GetArray(TEXT("StopAfter")))
	{
		StopAfter.Add(FCString::Atoi(*Value));
	}

	for (const FString& Value : C.GetArray(TEXT("Variant")))
	{
		FVBPlayBotVariant& Variant = Variants.AddDefaulted_GetRef();
		if (!Variant.Parse(Value, OutError))
		{
			return false;
		}
	}

	if (StopAfter.IsEmpty() || Variants.IsEmpty() || Repetitions <= 0 || PracticeTrials <= 0 || Params.PracticeStopAfter <= 0
		|| Params.AttackRange <= 0.0 || EnemyDistance <= Params.AttackRange)
	{
		OutError = FString::Printf(TEXT("[%s] StopAfter/Variant lists or a numeric value is invalid"), *C.Section);
		return false;
	}

	for (const int32 Stop : StopAfter)
	{
		if (Stop <= 0 || Stop > Params.PracticeStopAfter)
		{
			OutError = FString::Printf(TEXT("[%s] StopAfter %d is outside 1..PracticeStopAfter"), *C.Section, Stop);
			return false;
		}
		// 멈춤 지점마다 도달 검사 하나(stop_after_N_reached) - 값이 겹치면 시행이 합쳐지고 두 번 보고된다.
		if (StopAfter.FilterByPredicate([Stop](int32 Other) { return Other == Stop; }).Num() > 1)
		{
			OutError = FString::Printf(TEXT("[%s] StopAfter %d is listed twice"), *C.Section, Stop);
			return false;
		}
	}

	if (!VBPlayBotSkills::EnsureAction(Ctx, Params.AttackAction, OutError))
	{
		return false;
	}

	Brain.Configure(*Ctx.Human, *Ctx.Input, Params);
	Brain.Emit = Ctx.Emit;
	if (Params.AwayAfterWindowFrames > 0)
	{
		if (!Ctx.PlayerASC)
		{
			OutError = TEXT("window repush requires the player ASC");
			return false;
		}

		WindowASC = Ctx.PlayerASC;
		// runner 도 같은 ASC 이벤트를 기록한다. 기준 사실은 이 선택형 fixture 안에만 둔다.
		auto OnWindow = [this](const FGameplayEventData* Payload, bool bOpen)
		{
			if (Phase != EPhase::Trial || !Plan.IsValidIndex(PlanIndex) || Plan[PlanIndex].bPractice)
			{
				return;
			}

			const UObject* Notify = Payload ? Payload->OptionalObject.Get() : nullptr;
			if (!bOpen)
			{
				// 섹션 점프 때 이전 섹션의 notify 가 다음 섹션이 시작된 뒤에 끝날 수 있다.
				if (!Notify || RepushWindowNotify.Get() != Notify)
				{
					return;
				}

				PendingWindows.Emplace(false, Plan[PlanIndex].StopAfter);
				RepushWindowNotify.Reset();
				return;
			}

			if (RepushWindowNotify.IsValid())
			{
				return;
			}
			// ability 의 앞선 delegate 가 이미 활성 Montage 섹션을 건너뛰었을 수 있어, 이벤트 payload 의 notify 로 저작 섹션을 찾는다.
			const UAnimMontage* Montage = Notify ? Notify->GetTypedOuter<UAnimMontage>() : nullptr;
			int32 HitIndex = INDEX_NONE;
			int32 Matches = 0;
			if (Montage)
			{
				for (const FAnimNotifyEvent& Event : Montage->Notifies)
				{
					if (Event.NotifyStateClass.Get() != Notify)
					{
						continue;
					}

					++Matches;
					const int32 Section = Montage->GetSectionIndexFromPosition(Event.GetTriggerTime());
					HitIndex = Section == INDEX_NONE ? INDEX_NONE : Section + 1;
				}
			}

			if (Matches != 1 || HitIndex == INDEX_NONE)
			{
				Failure = TEXT("repush window notify has no unique authored montage section");
				return;
			}

			if (HitIndex != Plan[PlanIndex].StopAfter || !TrialContacts.Contains(HitIndex))
			{
				return;
			}

			RepushWindowNotify = Notify;
			PendingWindows.Emplace(true, HitIndex);
		};
		WindowOpenHandle = Ctx.PlayerASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_WindowOpen)
			.AddLambda([OnWindow](const FGameplayEventData* Payload) { OnWindow(Payload, true); });
		WindowCloseHandle = Ctx.PlayerASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_WindowClose)
			.AddLambda([OnWindow](const FGameplayEventData* Payload) { OnWindow(Payload, false); });
	}

	Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
	{
		if (Press.Action == Params.AttackAction)
		{
			Brain.OnPressInjected(Press);
		}
	};
	Ctx.OnHit = [this](const FVBPlayBotHit& Hit)
	{
		if (Phase == EPhase::Trial && Hit.bFromPlayer && Hit.Victim.Get() == Enemy.Get() && Hit.SectionHit != INDEX_NONE
			&& !TrialContacts.Contains(Hit.SectionHit))
		{
			TrialContacts.Add(Hit.SectionHit);
			Brain.OnContact(Hit.SectionHit, Hit.Frame);
		}
	};

	Prepare.Then<FVBSkillEquip>(Weapon, WeaponAction);
	Prepare.Then<FVBSkillDo>(TEXT("spawn still target"), [this](FVBPlayBotContext& C2, FString& Error)
	{
		PlayerStart = C2.Player->GetActorLocation();
		ACharacter* Spawned = VBPlayBotSkills::SpawnStillTarget(C2, EnemyClass, EnemyDistance, Error);
		if (!Spawned)
		{
			return false;
		}

		Enemy = Spawned;
		EnemyStart = Spawned->GetActorLocation();
		C2.Target = Spawned;
		C2.Watch(Spawned);
		return true;
	});
	Prepare.Then<FVBSkillWaitUntil>(TEXT("target AI stopped and grounded"), [this](FVBPlayBotContext& C2)
	{
		return Enemy.IsValid() && VBPlayBotSkills::StopAI(Enemy.Get()) && FVBPlayBotContext::IsGrounded(Enemy.Get());
	}, 5.0);
	return true;
}

EVBPlayBotState FVBPlayBotScenario_FighterLightStop::Tick(FVBPlayBotContext& Ctx)
{
	if (!Failure.IsEmpty())
	{
		return EVBPlayBotState::Failed;
	}

	if (bFinished)
	{
		// 끝난 측정 시행이 없으면 잰 것이 없다 - "measured" 로 끝내지 않고 그렇게 말한다.
		Ctx.Checks->Expect(TEXT("measured_trials_completed"), CompletedMeasured > 0, true, Ctx.Frame,
			FString::Printf(TEXT("{\"completed\":%d,\"planned\":%d}"), CompletedMeasured, Plan.Num() - PracticeTrials));
		// 멈춤 지점마다: 어느 시행도 닿지 않은 지점은 잰 것이 없으니 절대 통과가 아니다.
		for (const int32 Stop : StopAfter)
		{
			const FString Name = FString::Printf(TEXT("stop_after_%d_reached"), Stop);
			const int32 Reached = ReachedByStop.FindRef(Stop);
			if (Reached == 0)
			{
				Ctx.Checks->Unavailable(Name, Ctx.Frame, FString::Printf(TEXT("no measured trial reached section %d"), Stop));
			}
			else Ctx.Checks->Expect(Name, true, true, Ctx.Frame, FString::Printf(TEXT("{\"reached\":%d}"), Reached));
		}

		return EVBPlayBotState::Succeeded;
	}

	switch (Phase)
	{
	case EPhase::Prepare:
	{
		const EVBPlayBotState State = Prepare.Tick(Ctx);
		if (State == EVBPlayBotState::Failed)
		{
			return Fail(Prepare.GetFailure());
		}

		if (State == EVBPlayBotState::Succeeded)
		{
			for (int32 Practice = 0; Practice < PracticeTrials; ++Practice)
			{
				FVBPlayBotTrialPlan& Trial = Plan.AddDefaulted_GetRef();
				Trial.Index = Plan.Num() - 1;
				Trial.PairKey = 900000 + Practice;
				Trial.Variant = TEXT("practice_authored");
				Trial.StopAfter = Params.PracticeStopAfter;
				Trial.bPractice = true;
			}

			for (const FVBPlayBotVariant& Variant : Variants)
			{
				for (const int32 Stop : StopAfter)
				{
					for (int32 Rep = 0; Rep < Repetitions; ++Rep)
					{
						FVBPlayBotTrialPlan& Trial = Plan.AddDefaulted_GetRef();
						Trial.Index = Plan.Num() - 1;
						// 변형 사이에 같은 pair key = A/B 비교용 같은 사람 반응 추첨.
						Trial.PairKey = Stop * 1000 + Rep;
						Trial.Variant = Variant.Name;
						Trial.StopAfter = Stop;
					}
				}
			}

			if (!BeginNextTrial(Ctx))
			{
				return bFinished ? EVBPlayBotState::Running : EVBPlayBotState::Failed;
			}
		}

		return EVBPlayBotState::Running;
	}

	case EPhase::Settle:
	{
		if (!Enemy.IsValid())
		{
			return Fail(TEXT("target lost between trials"));
		}

		if (Ctx.Frame - PhaseFrame < Ctx.SecondsToFrames(SettleSeconds)
			|| (Ctx.PlayerASC && Ctx.PlayerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking)))
		{
			return EVBPlayBotState::Running;
		}
		// 두 몸 모두 바닥에 서야 한다 - 자유 낙하 중에는 마찰이 없어 넉백이 멈추지 않고 간격 수치가 모두 무효가 된다.
		if (!FVBPlayBotContext::IsGrounded(Ctx.Player) || !FVBPlayBotContext::IsGrounded(Enemy.Get()))
		{
			return Fail(TEXT("player or target not standing on the floor after settle"));
		}

		const FVBPlayBotTrialPlan& Trial = Plan[PlanIndex];
		const bool bRepush = !Trial.bPractice && Params.AwayAfterWindowFrames > 0;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"trial_begin\",\"f\":%lld,\"trial\":%d,\"pair\":%d,\"variant\":\"%s\",\"stopAfter\":%d,\"practice\":%s,\"awayTiming\":\"%s\",\"awayAfterWindowFrames\":%d}"),
			Ctx.Frame, Trial.Index, Trial.PairKey, *Trial.Variant, Trial.StopAfter, Trial.bPractice ? TEXT("true") : TEXT("false"),
			bRepush ? TEXT("window_repush") : TEXT("contact_policy"), bRepush ? Params.AwayAfterWindowFrames : 0));
		TrialContacts.Reset();
		Brain.BeginTrial(Trial, Ctx.Frame);
		Phase = EPhase::Trial;
		return EVBPlayBotState::Running;
	}

	case EPhase::Trial:
	{
		// runner 는 다음 프레임으로 월드 이벤트를 기록하고 Ctx.Frame 은 이 tick 에 갱신된다.
		// 그래서 쌓인 이벤트를 이전 tick 문맥으로 찍지 않고 지금 처리한다.
		for (const TPair<bool, int32>& Window : PendingWindows)
		{
			Brain.OnDiagnosticWindow(Window.Key, Window.Value, Ctx.Frame);
		}

		PendingWindows.Reset();
		const FVBPlayBotPercept Percept = Perceive(Ctx);
		if (!bMoveRouteChecked)
		{
			StickActiveFrames = Ctx.Input->GetStickOut().Size() > 0.5 ? StickActiveFrames + 1 : 0;
			if (Percept.Speed >= MoveRouteConfirmSpeed)
			{
				bMoveRouteChecked = true;
			}
			else if (StickActiveFrames > MoveRouteFrames && Percept.Speed < MoveRouteMinSpeed)
			{
				return Fail(TEXT("stick events did not move the pawn (pad Move mapping or input route)"));
			}
		}

		if (Brain.Tick(Ctx.Frame, Percept))
		{
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"trial_end\",\"f\":%lld,\"trial\":%d,\"reason\":\"%s\"}"),
				Ctx.Frame, Plan[PlanIndex].Index, *Brain.GetEndReason()));
			// 측정 시행은 멈추려던 섹션에 실제로 닿았을 때만 센다 - 닿지 않고 끝난 시행이 통과로 읽히지 않게 한다.
			const FVBPlayBotTrialPlan& Ended = Plan[PlanIndex];
			if (!Ended.bPractice)
			{
				const bool bReached = Brain.GetEndReason() == TEXT("completed") && TrialContacts.Num() >= Ended.StopAfter;
				ReachedByStop.FindOrAdd(Ended.StopAfter) += bReached ? 1 : 0;
				if (bReached)
				{
					++CompletedMeasured;
				}
			}

			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"trial_reach\",\"f\":%lld,\"trial\":%d,\"stopAfter\":%d,\"contacts\":%d}"),
				Ctx.Frame, Ended.Index, Ended.StopAfter, TrialContacts.Num()));
			Ctx.Input->ReleaseAll(Ctx.Frame);
			if (!BeginNextTrial(Ctx))
			{
				return bFinished ? EVBPlayBotState::Running : EVBPlayBotState::Failed;
			}
		}

		return EVBPlayBotState::Running;
	}
	}

	return EVBPlayBotState::Running;
}

void FVBPlayBotScenario_FighterLightStop::Teardown(FVBPlayBotContext& Ctx)
{
	if (UAbilitySystemComponent* ASC = WindowASC.Get())
	{
		if (FGameplayEventMulticastDelegate* D = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_WindowOpen))
		{
			D->Remove(WindowOpenHandle);
		}

		if (FGameplayEventMulticastDelegate* D = ASC->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_WindowClose))
		{
			D->Remove(WindowCloseHandle);
		}
	}

	WindowASC.Reset();
	RepushWindowNotify.Reset();
	PendingWindows.Reset();
	Ctx.OnHit = nullptr;
	Ctx.OnPressInjected = nullptr;
}

bool FVBPlayBotScenario_FighterLightStop::BeginNextTrial(FVBPlayBotContext& Ctx)
{
	FString Error;
	for (;;)
	{
		++PlanIndex;
		if (!Plan.IsValidIndex(PlanIndex))
		{
			bFinished = true;
			return false;
		}

		const FVBPlayBotTrialPlan& Next = Plan[PlanIndex];
		if (!Next.bPractice && !bModelReady)
		{
			if (!Brain.FinalizePracticeModel(Error))
			{
				Fail(Error);
				return false;
			}

			bModelReady = true;
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"practice_model\",\"f\":%lld,\"model\":%s}"), Ctx.Frame, *Brain.PracticeModelJson()));
		}

		if (Next.bPractice || Next.StopAfter <= Brain.GetMaxModeledHit())
		{
			break;
		}
		// 건너뜀으로 기록에 남긴다 - 실제로 맞지 않은 타격이라 인과적으로 시간을 잴 수 없다.
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"trial_end\",\"f\":%lld,\"trial\":%d,\"reason\":\"skipped_unlanded_hit#%d\"}"),
			Ctx.Frame, Next.Index, Brain.GetMaxModeledHit() + 1));
	}

	const FVBPlayBotTrialPlan& Trial = Plan[PlanIndex];
	if (Trial.Variant != ActiveVariant)
	{
		const FVBPlayBotVariant* Variant = Variants.FindByPredicate([&Trial](const FVBPlayBotVariant& V) { return V.Name == Trial.Variant; });
		FString Readback = TEXT("authored");
		if (Variant && !Ctx.Overrides->Apply(*Variant, Readback, Error))
		{
			Fail(Error);
			return false;
		}

		if (!Variant)
		{
			Ctx.Overrides->RestoreAll();
		}

		ActiveVariant = Trial.Variant;
		Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"override\",\"f\":%lld,\"variant\":\"%s\",\"readback\":\"%s\"}"),
			Ctx.Frame, *Trial.Variant, *Readback.ReplaceCharWithEscapedChar()));
	}

	Ctx.Human->BeginTrial(Trial.PairKey);
	RepushWindowNotify.Reset();
	PendingWindows.Reset();
	Ctx.Input->ReleaseAll(Ctx.Frame);
	ResetPositions(Ctx);
	Phase = EPhase::Settle;
	PhaseFrame = Ctx.Frame;
	return true;
}

void FVBPlayBotScenario_FighterLightStop::ResetPositions(FVBPlayBotContext& Ctx)
{
	ACharacter* P = Ctx.Player;
	ACharacter* E = Enemy.Get();
	if (!P || !E)
	{
		return;
	}

	const FRotator PlayerFacing = (EnemyStart - PlayerStart).GetSafeNormal2D().Rotation();
	const FRotator EnemyFacing = (PlayerStart - EnemyStart).GetSafeNormal2D().Rotation() + FRotator(0.0, TargetYawOffsetDeg, 0.0);
	P->SetActorLocationAndRotation(PlayerStart, PlayerFacing, false, nullptr, ETeleportType::TeleportPhysics);
	E->SetActorLocationAndRotation(EnemyStart, EnemyFacing, false, nullptr, ETeleportType::TeleportPhysics);
	if (UCharacterMovementComponent* Move = P->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}

	if (UCharacterMovementComponent* Move = E->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}

	if (Ctx.PC)
	{
		Ctx.PC->SetControlRotation(FRotator(Ctx.PC->GetControlRotation().Pitch, PlayerFacing.Yaw, 0.0));
	}

	Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"reset\",\"f\":%lld,\"targetYawOffset\":%.1f}"), Ctx.Frame, TargetYawOffsetDeg));
}

FVBPlayBotPercept FVBPlayBotScenario_FighterLightStop::Perceive(const FVBPlayBotContext& Ctx) const
{
	FVBPlayBotPercept Percept;
	const ACharacter* E = Enemy.Get();
	if (!Ctx.Player || !E || !Ctx.PC)
	{
		return Percept;
	}

	Percept.bValid = true;
	Percept.Distance = Ctx.Distance2D(E);
	Percept.ToTargetStick = Ctx.ToStick(E->GetActorLocation() - Ctx.Player->GetActorLocation());
	Percept.Speed = Ctx.Player->GetVelocity().Size2D();
	return Percept;
}
