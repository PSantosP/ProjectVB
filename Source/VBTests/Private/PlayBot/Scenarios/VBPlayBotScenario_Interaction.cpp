// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/Scenarios/VBPlayBotScenario_Interaction.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/VBCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace VBPlayBotInteraction
{
class FScenario : public FVBPlayBotScenarioBase
{
public:
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		Player = Cast<AVBCharacter>(Ctx.Player);
		Interactor = Ctx.Player->FindComponentByClass<UVBInteractorComponent>();
		if (!Player.IsValid() || !Interactor.IsValid() || !Ctx.PlayerASC || !Ctx.Player->HasAuthority())
		{
			Error = TEXT("Interaction needs a VBCharacter interactor in an authority world");
			return false;
		}

		const auto& C = *Ctx.Config;
		Interact = FName(*C.GetString(TEXT("InteractAction"), TEXT("Input.Interact")));
		Sprint = FName(*C.GetString(TEXT("SprintAction"), TEXT("Input.Sprint")));
		Radius = C.GetDouble(TEXT("ActorSearchRadiusCm"), 5000);
		Neutral = C.GetDouble(TEXT("NeutralSampleSeconds"), 0.12);
		Pulse = C.GetDouble(TEXT("MovePulseSeconds"), 0.15);
		Slack = C.GetDouble(TEXT("CompletionSlackSeconds"), 1);
		ApproachTimeout = C.GetDouble(TEXT("ApproachTimeoutSeconds"), 20);
		LookMagnitude = C.GetDouble(TEXT("LookMagnitude"), 0.8);
		TurnMargin = C.GetDouble(TEXT("TurnBeyondAcquisitionDegrees"), 15);
		if (Radius <= 0 || Neutral <= FVBMoveCancelGate::Interval * 2 || Pulse <= FVBMoveCancelGate::Interval
			|| Slack <= 0 || ApproachTimeout <= 0 || LookMagnitude <= 0 || LookMagnitude > 1 || TurnMargin <= 0)
		{
			Error = TEXT("Interaction invalid radius/timing/look parameters");
			return false;
		}

		if (!VBPlayBotSkills::EnsureAction(Ctx, Interact, Error) || !VBPlayBotSkills::EnsureAction(Ctx, Sprint, Error))
		{
			return false;
		}

		ActiveTag = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("ActiveTag"), TEXT("State.Interacting"))), false);
		if (!ActiveTag.IsValid()) { Error = TEXT("Interaction missing active tag"); return false; }
		Observer.Reset(NewObject<UVBPlayBotInteractionObserver>());
		Observer->ExpectedPlayer = Ctx.Player;
		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			if (Press.Action == Interact)
			{
				Injected = Press.InjectedFrame;
			}

			if (Press.Action == Sprint)
			{
				bSprintInjected = true;
			}
		};
		return true;
	}

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (!Player.IsValid() || !Interactor.IsValid())
		{
			return Fail(TEXT("Interaction lost player/interactor"));
		}

		if (Trial >= 6)
		{
			return EVBPlayBotState::Succeeded;
		}

		if (Phase == EPhase::Find)
		{
			if (!SelectTarget(Ctx))
			{
				++Trial;
				return EVBPlayBotState::Running;
			}

			Prepare.Reset();
			Prepare.Then<FVBSkillApproach>(Name() + TEXT(" approach"), [this](FVBPlayBotContext&) { return Target.IsValid() ? Target->GetOwner() : nullptr; },
				Target->GetConfig()->InteractRange * 0.65, ApproachTimeout);
			Phase = EPhase::Approach;
		}

		if (Phase == EPhase::Approach)
		{
			const auto State = Prepare.Tick(Ctx);
			if (State == EVBPlayBotState::Failed)
			{
				SkipFailedTrial(Ctx, Prepare.GetFailure());
				return EVBPlayBotState::Running;
			}

			if (State == EVBPlayBotState::Running)
			{
				return State;
			}

			Phase = EPhase::Face;
			PhaseStart = NextCorrection = Ctx.Frame;
		}

		if (!Target.IsValid())
		{
			return Fail(TEXT("Interaction selected component destroyed"));
		}

		if (Phase == EPhase::Face)
		{
			if (Interactor->CanReach(Target.Get()) && Interactor->FindBestInteractable() == Target.Get())
			{
				Ctx.Input->ReleaseAll(Ctx.Frame);
				PhaseStart = Ctx.Frame;
				Phase = EPhase::Neutral;
			}
			else
			{
				if (Ctx.Frame >= NextCorrection)
				{
					const double Desired = (Target->GetComponentLocation() - Ctx.Player->GetActorLocation()).Rotation().Yaw;
					const double Delta = FMath::FindDeltaAngleDegrees(double(Ctx.PC->GetControlRotation().Yaw), Desired);
					Ctx.Input->SetLookWant(FVector2D(FMath::Abs(Delta) > 3 ? FMath::Sign(Delta) * 0.3 : 0, 0));
					NextCorrection = Ctx.Frame + Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs());
				}

				if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(ApproachTimeout))
				{
					SkipFailedTrial(Ctx, TEXT("Interaction cannot focus/reach selected component"));
					return EVBPlayBotState::Running;
				}

				return EVBPlayBotState::Running;
			}
		}

		if (Phase == EPhase::Neutral && Ctx.Frame - PhaseStart >= Ctx.SecondsToFrames(Neutral))
		{
			// 접근 감속으로 첫 포커스 표본 뒤 pawn 이 포커스 원뿔 밖으로 나갈 수 있다.
			if (Ctx.Player->GetVelocity().Size2D() > 1)
			{
				return EVBPlayBotState::Running;
			}

			if (!Interactor->CanReach(Target.Get()) || Interactor->FindBestInteractable() != Target.Get())
			{
				Phase = EPhase::Face;
				PhaseStart = NextCorrection = Ctx.Frame;
				return EVBPlayBotState::Running;
			}

			const auto* C = Target->GetConfig();
			const FVBInteractionMotion* Motion = C->SelectMotion(!Target->IsActivated(), Target->GetOwner()->GetActorTransform(),
				Ctx.Player->GetActorLocation());
			Horizon = C->Mode == EVBInteractMode::Hold ? C->HoldDuration : Motion ? Motion->CommitTime + C->AlignDuration : 0;
			ObserveHorizon = Motion && Motion->CharacterMontage ? FMath::Max(Horizon,
				double(Motion->CharacterMontage->GetPlayLength() + C->AlignDuration)) : Horizon;
			if (Horizon <= 0)
			{
				Ctx.Checks->Unavailable(Name(), Ctx.Frame, TEXT("no delayed completion to exercise precommit movement"));
				Next(Ctx);
				return EVBPlayBotState::Running;
			}

			Observer->Completions = 0;
			Target->OnCompletionApplied.AddDynamic(Observer.Get(), &UVBPlayBotInteractionObserver::OnCompleted);
			bWasActivated = Target->IsActivated();
			StartYaw = Ctx.PC->GetControlRotation().Yaw;
			Injected = ActiveFrame = MoveFrame = -1;
			bStarted = bTurned = bRetention = bSprintInjected = bMoveInjected = bMoveObserved = bMovementResumed = false;
			bKeptValid = true;
			bMoveDuringFacing = false;
			RetentionSamples = InvalidKeepSamples = 0;
			MaxTargetYawError = 0;
			LookSign = 0;
			// 미리 짠 누르고-움직이기 동작으로 짧은 문 정렬을 시험한다. 주입 전에 추첨하고,
			// 늦은 추첨을 허용 창으로 옮기거나 실제 commit 시각으로 일정을 잡지 않는다.
			PlannedMoveDelayMs = IsCancel() ? Ctx.Human->SampleHoldMs() : 0;
			MoveDue = -1;
			MaxTurn = 0;
			VBPlayBotSkills::ReactPress(Ctx, Interact, FKey(), Name(), Ctx.Frame, Horizon + Slack + 1);
			PhaseStart = Ctx.Frame;
			Phase = EPhase::Exercise;
		}

		if (Phase == EPhase::Exercise)
		{
			const bool bActive = Interactor->GetMotionTarget() == Target.Get() || Interactor->GetHoldProgress() >= 0;
			if (bActive && !bStarted)
			{
				bStarted = true;
				ActiveFrame = Ctx.Frame;
				// 발동 뒤 중립을 확인하고, 움직이려는 의도에 대한 사람 반응 하나를 예약한다.
				MoveDue = IsCancel() ? Injected + Ctx.MsToFrames(PlannedMoveDelayMs)
					: Ctx.Frame + Ctx.SecondsToFrames(Neutral) + Ctx.Human->GetProfile().PresentationFrames + Ctx.MsToFrames(Ctx.Human->SampleSimpleReactionMs());
				if (Trial == 1)
				{
					VBPlayBotSkills::ReactPress(Ctx, Sprint, FKey(), TEXT("sprint during pickup"), Ctx.Frame, Horizon + Slack);
				}
			}

			if (bActive && IsPickup() && Observer->Completions == 0)
			{
				MaxTurn = FMath::Max(MaxTurn, FMath::Abs(FMath::FindDeltaAngleDegrees(StartYaw, double(Ctx.PC->GetControlRotation().Yaw))));
				const FVBInteractionMotion* Motion = Interactor->GetActiveMotion();
				const UAnimInstance* Anim = Ctx.Player->GetMesh()->GetAnimInstance();
				const bool bRetentionPhase = Target->GetConfig()->Mode == EVBInteractMode::Hold
					|| (Motion && Anim && Anim->Montage_IsPlaying(Motion->CharacterMontage));
				const double Bearing = (Target->GetComponentLocation() - Ctx.Player->GetActorLocation()).Rotation().Yaw;
				const double Error = FMath::FindDeltaAngleDegrees(Bearing, double(Ctx.PC->GetControlRotation().Yaw));
				const double WantedTurn = Target->GetConfig()->ReachRules.MaxYawAngle + TurnMargin;
				if (bRetentionPhase)
				{
					MaxTargetYawError = FMath::Max(MaxTargetYawError, FMath::Abs(Error));
					if (LookSign == 0)
					{
						LookSign = Error < 0 ? -1 : 1;
					}

					bTurned |= FMath::Abs(Error) >= WantedTurn && MaxTurn >= TurnMargin;
					Ctx.Input->SetLookWant(bTurned ? FVector2D::ZeroVector : FVector2D(LookSign * LookMagnitude, 0));
					const bool bKeep = Interactor->CanKeep(Target.Get());
					bRetention |= bTurned && !Interactor->CanReach(Target.Get()) && bKeep;
					bKeptValid &= bKeep;
					++RetentionSamples;
					if (!bKeep)
					{
						++InvalidKeepSamples;
					}
				}
			}
			else if (IsPickup() && Observer->Completions > 0)
			{
				Ctx.Input->SetLookWant(FVector2D::ZeroVector);
			}

			if (bStarted && (IsCancel() || Trial == 1) && Ctx.Frame >= MoveDue && !bMoveInjected
				&& (Trial != 2 || (Interactor->GetMotionTarget() == Target.Get() && Interactor->OwnsCapsuleFacing())))
			{
				bMoveInjected = true;
				MoveFrame = Ctx.Frame;
				MoveDirection = Ctx.Player->GetActorRightVector();
				Ctx.Input->SetStickWant(Ctx.ToStick(MoveDirection).GetRotated(Ctx.Human->SampleDirectionErrorDeg()) * Ctx.Human->GetProfile().MoveMagnitude);
			}

			if (MoveFrame >= 0)
			{
				bMoveObserved |= !FVBPlayBotContext::RawMoveInput(Player.Get()).IsNearlyZero() && Ctx.Input->GetStickOut().Size() > 0.1;
				bMoveDuringFacing |= Interactor->GetMotionTarget() == Target.Get() && Interactor->OwnsCapsuleFacing()
					&& !FVBPlayBotContext::RawMoveInput(Player.Get()).IsNearlyZero() && Ctx.Input->GetStickOut().Size() > 0.1;
				bMovementResumed |= Observer->Completions > 0 && !bActive && Ctx.Player->GetVelocity().Size2D() > 10
					&& !Ctx.Player->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero();
				if (IsCancel() && Ctx.Frame - MoveFrame >= Ctx.SecondsToFrames(Pulse))
				{
					Ctx.Input->SetStickWant(FVector2D::ZeroVector);
				}
			}

			if (Injected >= 0 && Ctx.Frame - Injected > Ctx.SecondsToFrames(ObserveHorizon + Slack))
			{
				const bool bEnded = !bActive && !Ctx.PlayerASC->HasMatchingGameplayTag(ActiveTag);
				const FString Evidence = FString::Printf(TEXT("{\"target\":\"%s\",\"completions\":%d,\"holdMode\":%s,\"maxTurn\":%.3f,\"moveFrame\":%lld,\"activeFrame\":%lld,\"targetYawError\":%.3f,\"retentionSamples\":%d,\"invalidKeepSamples\":%d,\"turned\":%s,\"outsideAcquireWhileKeep\":%s,\"plannedMoveDelayMs\":%.3f,\"commitHorizonSeconds\":%.3f,\"rawMoveDuringFacing\":%s}"),
					*Target->GetPathName().ReplaceCharWithEscapedChar(), Observer->Completions,
					Target->GetConfig()->Mode == EVBInteractMode::Hold ? TEXT("true") : TEXT("false"), MaxTurn, MoveFrame, ActiveFrame,
					MaxTargetYawError, RetentionSamples, InvalidKeepSamples, bTurned ? TEXT("true") : TEXT("false"),
					bRetention ? TEXT("true") : TEXT("false"), PlannedMoveDelayMs, Horizon, bMoveDuringFacing ? TEXT("true") : TEXT("false"));
				Ctx.Checks->Expect(Name() + TEXT(".Starts"), bStarted, true, Ctx.Frame, Evidence);
				Ctx.Checks->Expect(Name() + TEXT(".Cleanup"), bStarted && bEnded, true, Ctx.Frame, Evidence);
				if (IsCancel())
				{
					const bool bMoveBeforeCommit = MoveFrame > ActiveFrame && MoveFrame < Injected + Ctx.SecondsToFrames(Horizon)
						&& bMoveObserved && (Target->GetConfig()->Mode == EVBInteractMode::Hold || bMoveDuringFacing);
					Ctx.Checks->Expect(Name() + TEXT(".NewMove"), bMoveBeforeCommit, true, Ctx.Frame, Evidence);
					Ctx.Checks->Expect(Name() + TEXT(".Cancels"), bStarted && bMoveBeforeCommit && bEnded
						&& Observer->Completions == 0 && Target->IsActivated() == bWasActivated, true, Ctx.Frame, Evidence);
				}
				else
				{
					const bool bApplied = Observer->Completions == 1 && Target->IsActivated() != bWasActivated;
					Ctx.Checks->Expect(Name() + TEXT(".Completes"), bStarted && bApplied, true, Ctx.Frame, Evidence);
					if (IsPickup())
					{
						Ctx.Checks->Expect(Name() + TEXT(".Retention"), bTurned && bRetention && bKeptValid, true, Ctx.Frame, Evidence);
						Ctx.Checks->Expect(Name() + TEXT(".Consumed"), bApplied && Target->GetOwner()->IsHidden()
							&& !Target->IsInteractionEnabled(), true, Ctx.Frame);
						if (Trial == 1)
						{
							Ctx.Checks->Expect(Name() + TEXT(".SprintInput"), bSprintInjected && bMoveObserved, true, Ctx.Frame);
							Ctx.Checks->Expect(Name() + TEXT(".MovementResumes"), bMovementResumed, true, Ctx.Frame);
						}
					}
				}

				Next(Ctx);
			}
			else if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(ObserveHorizon + Slack + 5))
			{
				SkipFailedTrial(Ctx, TEXT("Interaction input injection timeout"));
			}
		}

		return EVBPlayBotState::Running;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		Unbind();
		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.OnPressInjected = nullptr;
		Observer.Reset();
	}

private:
	void SkipFailedTrial(FVBPlayBotContext& Ctx, const FString& Reason)
	{
		Ctx.Checks->Expect(Name() + TEXT(".Starts"), false, true, Ctx.Frame,
			FString::Printf(TEXT("{\"reason\":\"%s\"}"), *Reason.ReplaceCharWithEscapedChar()));
		Ctx.Checks->Unavailable(Name() + TEXT(".Remaining"), Ctx.Frame,
			TEXT("trial did not start; dependent observations were not exercised"));
		Next(Ctx);
	}

	bool IsPickup() const { return Trial < 2; }
	bool IsCancel() const { return Trial == 2 || Trial == 4; }
	FString Name() const
	{
		static const TCHAR* Names[] = { TEXT("Interaction.PickupCamera"), TEXT("Interaction.PickupSprint"), TEXT("Interaction.DoorCancel"),
			TEXT("Interaction.DoorStationary"), TEXT("Interaction.LeverCancel"), TEXT("Interaction.LeverStationary") };
		return Names[Trial];
	}

	bool SelectTarget(FVBPlayBotContext& Ctx)
	{
		const TCHAR* Key = IsPickup() ? TEXT("PickupConfig") : Trial < 4 ? TEXT("DoorConfig") : TEXT("LeverConfig");
		const FString ConfigPath = Ctx.Config->GetString(Key);
		const FString ClassPath = Ctx.Config->GetString(IsPickup() ? TEXT("PickupClass") : TEXT("ToggleClass"));
		UClass* ActorClass = FSoftClassPath(ClassPath).TryLoadClass<AActor>();
		Target.Reset();
		double BestDistance = Radius;
		if (ActorClass && !ConfigPath.IsEmpty()) for (TActorIterator<AActor> It(Ctx.World, ActorClass); It; ++It)
		{
			TArray<UVBInteractableComponent*> Components;
			It->GetComponents(Components);
			for (UVBInteractableComponent* Component : Components)
			{
				const auto* Config = Component->GetConfig();
				const double Distance = Ctx.Distance2D(*It);
				if (Config->GetPathName() != ConfigPath || !Component->IsInteractionEnabled() || !Component->PassesUseConditions(Ctx.Player)
					|| Config->Mode == EVBInteractMode::Auto || Config->Mode == EVBInteractMode::Push || Distance >= BestDistance
					|| (IsPickup() && (Component->IsActivated() || UsedPickups.Contains(Component)))
					|| (IsCancel() && !Config->bCancelOnMove))
				{
					continue;
				}

				Target = Component;
				BestDistance = Distance;
			}
		}

		if (!Target.IsValid())
		{
			Ctx.Checks->Unavailable(Name(), Ctx.Frame, FString::Printf(TEXT("no enabled usable %s candidate within %.0f cm"), Key, Radius));
			return false;
		}

		if (IsPickup())
		{
			UsedPickups.Add(Target);
		}

		return true;
	}

	void Unbind()
	{
		if (Target.IsValid() && Observer.IsValid())
		{
			Target->OnCompletionApplied.RemoveDynamic(Observer.Get(), &UVBPlayBotInteractionObserver::OnCompleted);
		}
	}

	void Next(FVBPlayBotContext& Ctx)
	{
		Unbind();
		Ctx.Input->ReleaseAll(Ctx.Frame);
		Target.Reset();
		++Trial;
		Phase = EPhase::Find;
	}

	enum class EPhase : uint8 { Find, Approach, Face, Neutral, Exercise };
	EPhase Phase = EPhase::Find;
	FVBPlayBotSequence Prepare;
	TStrongObjectPtr<UVBPlayBotInteractionObserver> Observer;
	TWeakObjectPtr<AVBCharacter> Player;
	TWeakObjectPtr<UVBInteractorComponent> Interactor;
	TWeakObjectPtr<UVBInteractableComponent> Target;
	TSet<TWeakObjectPtr<UVBInteractableComponent>> UsedPickups;
	FName Interact, Sprint;
	FGameplayTag ActiveTag;
	int32 Trial = 0;
	int32 RetentionSamples = 0, InvalidKeepSamples = 0;
	int64 PhaseStart = 0, NextCorrection = 0, Injected = -1, ActiveFrame = -1, MoveFrame = -1, MoveDue = -1;
	double Radius = 5000, Neutral = 0.12, Pulse = 0.15, Slack = 1, ApproachTimeout = 20, LookMagnitude = 0.8, TurnMargin = 15;
	double Horizon = 0, ObserveHorizon = 0, StartYaw = 0, MaxTurn = 0;
	double LookSign = 0, MaxTargetYawError = 0, PlannedMoveDelayMs = 0;
	FVector MoveDirection = FVector::ZeroVector;
	bool bWasActivated = false, bStarted = false, bTurned = false, bRetention = false, bKeptValid = true;
	bool bSprintInjected = false, bMoveInjected = false, bMoveObserved = false, bMovementResumed = false, bMoveDuringFacing = false;
};
VB_PLAYBOT_SCENARIO(FScenario, "Interaction")
}
