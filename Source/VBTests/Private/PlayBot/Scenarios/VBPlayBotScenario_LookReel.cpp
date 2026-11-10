// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBFinisherConfig.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"

namespace VBPlayBotLookReel
{
class FScenario : public FVBPlayBotScenarioBase
{
public:
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		const auto& C = *Ctx.Config;
		if (!C.RequireString(TEXT("EnemyClass"), EnemyClass, Error)
			|| !C.RequireString(TEXT("KickMontage"), MontagePath, Error)
			|| !C.RequireString(TEXT("KickSequence"), SequencePath, Error))
		{
			return false;
		}

		Attack = FName(*C.GetString(TEXT("AttackAction"), TEXT("Input.Attack.Light")));
		Execute = FName(*C.GetString(TEXT("ExecuteAction"), TEXT("Input.Execute")));
		KickSection = FName(*C.GetString(TEXT("KickSection"), TEXT("Swing2")));
		Distance = C.GetDouble(TEXT("EnemyDistance"), 180);
		ApproachRadius = C.GetDouble(TEXT("ApproachRadiusCm"), 140);
		Timeout = C.GetDouble(TEXT("TrialTimeoutSeconds"), 20);
		Lead = C.GetDouble(TEXT("LookLeadSeconds"), 0.25);
		Trail = C.GetDouble(TEXT("LookTrailSeconds"), 0.5);
		AttemptBudget = C.GetInt(TEXT("MaxFinisherAttempts"), 9);
		MaxAttacks = C.GetInt(TEXT("MaxSetupAttacks"), 12);
		AttackRange = C.GetDouble(TEXT("AttackRange"), 170);
		FinisherConfigs = C.GetArray(TEXT("FinisherConfig"));
		Finisher = UVBFinisherComponent::FindFor(Ctx.Player);
		if (!Finisher.IsValid() || FinisherConfigs.IsEmpty() || AttemptBudget <= 0 || MaxAttacks <= 0
			|| Distance <= 0 || ApproachRadius <= 0 || AttackRange <= 0 || Timeout <= 0 || Lead < 0 || Trail < 0)
		{
			Error = TEXT("LookReel missing finisher component/config list or invalid limits");
			return false;
		}

		if (!VBPlayBotSkills::EnsureAction(Ctx, Attack, Error) || !VBPlayBotSkills::EnsureAction(Ctx, Execute, Error))
		{
			return false;
		}

		Ctx.Input->ReleaseAll(Ctx.Frame);
		const UVBAttackConfig* AttackConfig = LoadObject<UVBAttackConfig>(nullptr, *C.GetString(TEXT("AttackConfig")));
		Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
		if (!AttackConfig || !Montage.IsValid()) { Error = TEXT("LookReel attack config/montage not loadable"); return false; }
		for (int32 Index = 0; Index < AttackConfig->GetMontageVariantCount(); ++Index)
		{
			if (AttackConfig->ResolveMontageVariant(Index).Montage == Montage.Get())
			{
				ForcedIndex = Index;
			}
		}

		SectionIndex = Montage->GetSectionIndex(KickSection);
		if (ForcedIndex == INDEX_NONE || SectionIndex == INDEX_NONE)
		{
			Error = TEXT("LookReel requested variant/section absent from actual data");
			return false;
		}

		Montage->GetSectionStartAndEndTime(SectionIndex, SectionStart, SectionEnd);
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
				if (Segment.GetAnimReference() && Segment.GetAnimReference()->GetPathName() == SequencePath
					&& Segment.StartPos < SectionEnd && Segment.StartPos + Segment.GetLength() > SectionStart)
				{
					bSegmentVerified = true;
				}
		if (!bSegmentVerified) { Error = TEXT("LookReel requested section does not contain the requested sequence"); return false; }
		ComboOverride = IConsoleManager::Get().FindConsoleVariable(*C.GetString(TEXT("ComboOverrideCVar"), TEXT("vb.Combat.ForceCombo")));
		if (!ComboOverride || C.GetInt(TEXT("AllowDeclaredComboOverride"), 0) != 1)
		{
			Error = TEXT("LookReel Manual variant needs declared development combo override");
			return false;
		}

		OriginalCombo = ComboOverride->GetInt();
		WindowTag = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("FinisherWindowTag"),
			TEXT("State.Combat.FinisherWindow"))), false);
		if (!WindowTag.IsValid()) { Error = TEXT("LookReel missing finisher eligibility tag"); return false; }
		Prepare.Then<FVBSkillEquip>(C.GetString(TEXT("Weapon"), TEXT("Fighter")),
			FName(*C.GetString(TEXT("WeaponAction"), TEXT("Input.WeaponSlot.3"))));
		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			if (Press.Action == Attack)
			{
				++Attacks;
			}

			if (Press.Action == Execute)
			{
				ExecuteFrame = Press.InjectedFrame;
			}
		};
		Ctx.OnHit = [this](const FVBPlayBotHit& Hit)
		{
			if (Hit.bFromPlayer && Hit.Victim == Enemy && Hit.SectionHit == 1 && FirstContact < 0)
			{
				FirstContact = Hit.Frame;
			}
		};
		return true;
	}

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (Phase == EPhase::Stopped)
		{
			return Fail(TEXT("LookReel active scene exceeded the attempt deadline; no forced teardown"));
		}

		if (Phase == EPhase::Equip)
		{
			const auto State = Prepare.Tick(Ctx);
			if (State == EVBPlayBotState::Failed)
			{
				return Fail(Prepare.GetFailure());
			}

			if (State == EVBPlayBotState::Running)
			{
				return State;
			}

			Phase = EPhase::Spawn;
		}

		if (Phase == EPhase::Spawn)
		{
			if (Attempt > AttemptBudget)
			{
				return Finish(Ctx);
			}

			Ctx.Input->ReleaseAll(Ctx.Frame);
			FString Error;
			ACharacter* Spawned = VBPlayBotSkills::SpawnStillTarget(Ctx, EnemyClass, Distance, Error);
			if (!Spawned)
			{
				return Fail(Error);
			}

			Enemy = Spawned;
			Ctx.Target = Spawned;
			Ctx.Watch(Spawned);
			Attacks = 0;
			FirstContact = ExecuteFrame = -1;
			bSecondScheduled = bSawFinisher = false;
			ActiveConfig.Reset();
			PhaseStart = Ctx.Frame;
			Phase = EPhase::Settle;
			// 유일한 덮어쓰기는 명시한 Manual kick fixture 다. finisher 시도 전에 되돌린다.
			ComboOverride->SetWithCurrentPriority(Attempt == 0 ? ForcedIndex : OriginalCombo);
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"override\",\"f\":%lld,\"variant\":\"look_fixture\",\"readback\":\"ForceCombo=%d\"}"), Ctx.Frame, ComboOverride->GetInt()));
		}

		if (!Enemy.IsValid() || !Finisher.IsValid())
		{
			return Fail(TEXT("LookReel lost target or finisher component"));
		}

		if (Phase == EPhase::Settle)
		{
			if (Ctx.Frame - PhaseStart < Ctx.SecondsToFrames(0.5))
			{
				return EVBPlayBotState::Running;
			}

			if (!FVBPlayBotContext::IsGrounded(Ctx.Player) || !FVBPlayBotContext::IsGrounded(Enemy.Get())
				|| Ctx.Player->GetVelocity().Size() > 1 || !Ctx.Player->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero(0.1)
				|| !FVBPlayBotContext::RawMoveInput(Ctx.Player).IsNearlyZero(0.01))
			{
				if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(5))
				{
					return Fail(TEXT("LookReel fixture did not reach actual neutral grounded state"));
				}

				return EVBPlayBotState::Running;
			}

			if (Attempt > 0)
			{
				// 홀수 시도는 앞, 짝수 시도는 옆 waypoint 를 돌아 뒤로 걸어간다. teleport 는 없다.
				SideGoal = Enemy->GetActorLocation() + Enemy->GetActorRightVector() * ApproachRadius * 2;
				FinalGoal = Enemy->GetActorLocation() + Enemy->GetActorForwardVector() * ApproachRadius * (Attempt % 2 ? 1 : -1);
				RouteStage = Attempt % 2 ? 1 : 0;
				NextCorrection = Ctx.Frame;
				Phase = EPhase::Position;
			}
			else
			{
				Phase = EPhase::Face;
			}

			PhaseStart = Ctx.Frame;
		}

		if (Phase == EPhase::Position)
		{
			VBPlayBotSkills::TurnCameraToward(Ctx, Enemy.Get());
			const FVector Goal = RouteStage == 0 ? SideGoal : FinalGoal;
			if (FVector::Dist2D(Goal, Ctx.Player->GetActorLocation()) < 30)
			{
				if (RouteStage == 0)
				{
					++RouteStage;
				}
				else { Ctx.Input->SetStickWant(FVector2D::ZeroVector); Phase = EPhase::Face; PhaseStart = Ctx.Frame; }
			}
			else if (Ctx.Frame >= NextCorrection)
			{
				Ctx.Input->SetStickWant(Ctx.ToStick(Goal - Ctx.Player->GetActorLocation()).GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude);
				NextCorrection = Ctx.Frame + Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs());
			}

			if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Timeout))
			{
				EndAttempt(Ctx, TEXT("approach_timeout"));
			}
		}

		if (Phase == EPhase::Face)
		{
			if (VBPlayBotSkills::TurnCameraToward(Ctx, Enemy.Get()))
			{
				Ctx.Input->SetLookWant(FVector2D::ZeroVector);
				BeginActions(Ctx);
			}
			else if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Timeout))
			{
				EndAttempt(Ctx, TEXT("face_timeout"));
			}
		}

		if (Phase == EPhase::Actions)
		{
			ObserveKick(Ctx);
			if (Phase != EPhase::Actions)
			{
				return EVBPlayBotState::Running;
			}

			if (Attempt == 0 && FirstContact >= 0 && !bSecondScheduled)
			{
				// 반응형 연습 연쇄 - 이미 본 접촉에서 예약하고 combo window 는 읽지 않는다.
				bSecondScheduled = true;
				VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), TEXT("seen kick opener contact"), FirstContact, 0);
			}

			if (Attempt > 0)
			{
				if (Finisher->IsPlaying())
				{
					if (!bSawFinisher)
					{
						bSawFinisher = true;
						FinisherStart = Ctx.Frame;
						ActiveConfig = Finisher->GetActiveConfig();
						Ctx.Input->CancelPending(TEXT("finisher_started"));
					}
				}
				else if (bSawFinisher)
				{
					const bool bCompleted = !Finisher->WasLastFinisherCancelled() && ActiveConfig.IsValid()
						&& FinisherConfigs.Contains(ActiveConfig->GetPathName());
					Ctx.Checks->Expect(FString::Printf(TEXT("LookReel.FinisherAttempt%d"), Attempt), bCompleted, true, Ctx.Frame,
						FString::Printf(TEXT("{\"config\":\"%s\",\"executeFrame\":%lld}"), *GetPathNameSafe(ActiveConfig.Get()).ReplaceCharWithEscapedChar(), ExecuteFrame));
					if (bCompleted)
					{
						CoveredConfigs.Add(ActiveConfig->GetPathName());
					}

					FinisherEnd = Ctx.Frame;
					PhaseStart = Ctx.Frame;
					Phase = EPhase::Trail;
				}
				else
				{
					if (CombatPrepare.Tick(Ctx) == EVBPlayBotState::Failed)
					{
						EndAttempt(Ctx, TEXT("finisher_preparation_failed"));
					}
				}
			}

			if (Phase == EPhase::Actions && Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Timeout))
			{
				EndAttempt(Ctx, TEXT("action_timeout"));
			}
		}

		if (Phase == EPhase::Trail && Ctx.Frame - PhaseStart >= Ctx.SecondsToFrames(Trail))
		{
			Ctx.Checks->Look(FString::Printf(TEXT("LookReel.Finisher%d"), Attempt),
				FMath::Max<int64>(1, FinisherStart - Ctx.SecondsToFrames(Lead)), Ctx.Frame,
				FString::Printf(TEXT("actual=%s; full finisher %lld..%lld; execute input=%s; visual review pending"),
					*GetPathNameSafe(ActiveConfig.Get()), FinisherStart, FinisherEnd,
					Ctx.Input->IsKeyboardFallback(Execute) ? TEXT("keyboard_fallback (no pad binding)") : TEXT("slate_gamepad")));
			EndAttempt(Ctx, TEXT("completed"));
		}

		return EVBPlayBotState::Running;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		if (ComboOverride)
		{
			ComboOverride->SetWithCurrentPriority(OriginalCombo);
		}

		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.OnHit = nullptr;
		Ctx.OnPressInjected = nullptr;
		// scene 이 살아 있는 참여자는 지우지 않는다. 끝날 때 격리된 game world 가 정리한다.
		if (Enemy.IsValid() && (!Finisher.IsValid() || !Finisher->IsPlaying()))
		{
			Enemy->Destroy();
		}
	}

private:
	void BeginActions(FVBPlayBotContext& Ctx)
	{
		Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		PhaseStart = Ctx.Frame;
		Phase = EPhase::Actions;
		if (Attempt == 0)
		{
			VBPlayBotSkills::ReactPress(Ctx, Attack, FKey(), TEXT("manual kick look fixture"), Ctx.Frame, 0);
		}
		else
		{
			CombatPrepare.Reset();
			CombatPrepare.Then<FVBSkillAttackUntil>(TEXT("prepare visible finisher eligibility"), [this](FVBPlayBotContext&) { return Enemy.Get(); }, Attack, AttackRange,
				[this](FVBPlayBotContext&, int32 Hits) { return Eligible() || Hits >= MaxAttacks; }, Timeout);
			CombatPrepare.Then<FVBSkillDo>(TEXT("verify eligibility before execute"), [this](FVBPlayBotContext&, FString& Error)
			{
				if (Eligible())
				{
					return true;
				}

				Error = TEXT("finite hit budget exhausted without finisher eligibility");
				return false;
			});
			CombatPrepare.Then<FVBSkillWaitUntil>(TEXT("face visible finisher target with body"), [this](FVBPlayBotContext& C2)
			{
				if (!Enemy.IsValid())
				{
					return false;
				}

				VBPlayBotSkills::TurnCameraToward(C2, Enemy.Get());
				const double Bearing = (Enemy->GetActorLocation() - C2.Player->GetActorLocation()).Rotation().Yaw;
				const bool bFacing = FMath::Abs(FMath::FindDeltaAngleDegrees(double(C2.Player->GetActorRotation().Yaw), Bearing)) < 20;
				const UAnimInstance* Anim = C2.Player->GetMesh()->GetAnimInstance();
				const bool bIdle = Anim && !Anim->GetCurrentActiveMontage();
				C2.Input->SetStickWant(bFacing && bIdle ? FVector2D::ZeroVector
					: C2.ToStick(Enemy->GetActorLocation() - C2.Player->GetActorLocation()) * C2.Human->GetProfile().MoveMagnitude);
				return bFacing && bIdle;
			}, 5);
			CombatPrepare.Then<FVBSkillPress>(TEXT("execute after visible eligibility"), Execute);
		}
	}

	bool Eligible() const
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy.Get());
		return ASC && ASC->HasMatchingGameplayTag(WindowTag);
	}

	void ObserveKick(FVBPlayBotContext& Ctx)
	{
		if (Attempt != 0)
		{
			return;
		}

		const UAnimInstance* Anim = Ctx.Player->GetMesh()->GetAnimInstance();
		// GetCurrentActiveMontage 는 마지막 원본 프레임 전에 blend out 중인 인스턴스를 뺀다.
		const FAnimMontageInstance* Instance = Anim ? Anim->GetInstanceForMontage(Montage.Get()) : nullptr;
		const bool bInSection = Instance && Instance->GetCurrentSection() == KickSection;
		if (bInSection)
		{
			if (KickStart < 0)
			{
				KickStart = Ctx.Frame;
			}

			KickLast = Ctx.Frame;
			MaxKickPosition = FMath::Max(MaxKickPosition, double(Instance->GetPosition()));
		}

		if (KickStart >= 0 && !bInSection && KickExit < 0)
		{
			KickExit = Ctx.Frame;
		}

		if (KickExit >= 0 && Ctx.Frame - KickExit >= Ctx.SecondsToFrames(Trail))
		{
			EndAttempt(Ctx, TEXT("kick_section_exited"));
		}
	}

	void EndAttempt(FVBPlayBotContext& Ctx, const TCHAR* Why)
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
		if (Attempt == 0)
		{
			const bool bComplete = KickStart >= 0 && KickExit >= 0 && MaxKickPosition >= SectionEnd - 2.0 / 60.0;
			Ctx.Checks->Expect(TEXT("LookReel.KickCoverage"), bSegmentVerified && bComplete, true, Ctx.Frame,
				FString::Printf(TEXT("{\"maxMontagePosition\":%.4f,\"sectionEnd\":%.4f,\"reason\":\"%s\"}"), MaxKickPosition, SectionEnd, Why));
			if (KickStart >= 0)
			{
				Ctx.Checks->Look(TEXT("LookReel.KickAnkle"),
				FMath::Max<int64>(1, KickStart - Ctx.SecondsToFrames(Lead)), Ctx.Frame,
				FString::Printf(TEXT("segment=%s; section frames %lld..%lld; complete=%d; mesh=%s; visual review pending"),
					*SequencePath, KickStart, KickLast, bComplete, *GetPathNameSafe(Ctx.Player->GetMesh()->GetSkeletalMeshAsset())));
			}
		}
		else if (!bSawFinisher) Ctx.Checks->Expect(FString::Printf(TEXT("LookReel.FinisherAttempt%d"), Attempt), false, true, Ctx.Frame,
			FString::Printf(TEXT("{\"reason\":\"%s\",\"attacks\":%d}"), Why, Attacks));
		if (Finisher->IsPlaying())
		{
			Ctx.Checks->Expect(FString::Printf(TEXT("LookReel.FinisherAttempt%d"), Attempt), false, true, Ctx.Frame,
				TEXT("{\"reason\":\"active_scene_timeout\"}"));
			// 시도를 하나 더 만들려고 게임 상태를 부수지 않는다.
			Phase = EPhase::Stopped;
			return;
		}

		if (Enemy.IsValid())
		{
			Enemy->Destroy();
		}

		Enemy.Reset();
		++Attempt;
		Phase = EPhase::Spawn;
	}

	EVBPlayBotState Finish(FVBPlayBotContext& Ctx)
	{
		for (int32 I = 0; I < FinisherConfigs.Num(); ++I)
		{
			const FString Name = FString::Printf(TEXT("LookReel.FinisherCoverage%d"), I + 1);
			if (CoveredConfigs.Contains(FinisherConfigs[I])) Ctx.Checks->Expect(Name, true, true, Ctx.Frame,
				FString::Printf(TEXT("{\"config\":\"%s\"}"), *FinisherConfigs[I].ReplaceCharWithEscapedChar()));
			else
			{
				Ctx.Checks->Unavailable(Name, Ctx.Frame, TEXT("pose-selected config not observed within finite attempt budget"));
			}
		}
		// KickAnkle 과 Finisher 구간은 눈으로 보는 품질 검토용이고 수치 검사가 아니다.
		return EVBPlayBotState::Succeeded;
	}

	enum class EPhase : uint8 { Equip, Spawn, Settle, Position, Face, Actions, Trail, Stopped };
	EPhase Phase = EPhase::Equip;
	FVBPlayBotSequence Prepare;
	FVBPlayBotSequence CombatPrepare;
	TWeakObjectPtr<ACharacter> Enemy;
	TWeakObjectPtr<UAnimMontage> Montage;
	TWeakObjectPtr<UVBFinisherComponent> Finisher;
	TWeakObjectPtr<const UVBFinisherConfig> ActiveConfig;
	IConsoleVariable* ComboOverride = nullptr;
	int32 OriginalCombo = -1, ForcedIndex = INDEX_NONE, SectionIndex = INDEX_NONE, Attempt = 0, AttemptBudget = 9, MaxAttacks = 12, Attacks = 0, RouteStage = 0;
	int64 PhaseStart = 0, NextCorrection = 0, FirstContact = -1, ExecuteFrame = -1;
	int64 KickStart = -1, KickLast = -1, KickExit = -1, FinisherStart = 0, FinisherEnd = 0;
	float SectionStart = 0, SectionEnd = 0;
	double Distance = 180, ApproachRadius = 140, AttackRange = 170, Timeout = 20, Lead = 0.25, Trail = 0.5, MaxKickPosition = 0;
	FVector SideGoal = FVector::ZeroVector, FinalGoal = FVector::ZeroVector;
	FName Attack, Execute, KickSection;
	FGameplayTag WindowTag;
	FString EnemyClass, MontagePath, SequencePath;
	TArray<FString> FinisherConfigs;
	TSet<FString> CoveredConfigs;
	bool bSegmentVerified = false, bSecondScheduled = false, bSawFinisher = false;
};
VB_PLAYBOT_SCENARIO(FScenario, "LookReel")
}
