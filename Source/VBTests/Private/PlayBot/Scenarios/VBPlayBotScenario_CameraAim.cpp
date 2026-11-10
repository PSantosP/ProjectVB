// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace VBPlayBotCameraAim
{
class FScenario : public FVBPlayBotScenarioBase
{
public:
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		Character = Cast<AVBCharacter>(Ctx.Player);
		if (!Character.IsValid() || !Ctx.PC || !Ctx.PC->PlayerCameraManager)
		{
			Error = TEXT("CameraAim requires VBCharacter and player camera manager");
			return false;
		}

		const auto& C = *Ctx.Config;
		Aim = FName(*C.GetString(TEXT("AimAction"), TEXT("Input.Aim")));
		Hold = C.GetDouble(TEXT("AimHoldSeconds"), 1.5);
		Settle = C.GetDouble(TEXT("CameraSettleSeconds"), 0.5);
		Timeout = C.GetDouble(TEXT("StateTimeoutSeconds"), 2.0);
		BaselineTimeout = C.GetDouble(TEXT("BaselineTimeoutSeconds"), 5.0);
		MinChange = C.GetDouble(TEXT("MinOffsetChangeCm"), 15.0);
		ReturnTolerance = C.GetDouble(TEXT("ReturnOffsetToleranceCm"), 10.0);
		RotationTolerance = C.GetDouble(TEXT("ReturnRotationToleranceDegrees"), 3.0);
		FovTolerance = C.GetDouble(TEXT("ReturnFovToleranceDegrees"), 2.0);
		MaxDrift = C.GetDouble(TEXT("MaxPlayerDriftCm"), 5.0);
		MaxYaw = C.GetDouble(TEXT("MaxControlYawDriftDegrees"), 2.0);
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*C.GetString(TEXT("AimInputTag"), TEXT("Input.Aim"))), false);
		const UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(Ctx.PlayerASC);
		if (!ASC || !Tag.IsValid() || Hold <= Settle + 0.2 || Settle <= 0 || Timeout <= Settle || BaselineTimeout <= Settle
			|| MinChange <= 0 || ReturnTolerance <= 0 || RotationTolerance <= 0 || FovTolerance <= 0 || MaxDrift <= 0 || MaxYaw <= 0)
		{
			Error = TEXT("CameraAim invalid timing/tolerances or missing ASC/Input.Aim");
			return false;
		}

		bToggle = ASC->IsToggleInput(Tag, false);
		if (!VBPlayBotSkills::EnsureAction(Ctx, Aim, Error))
		{
			return false;
		}

		Ctx.Input->ReleaseAll(Ctx.Frame);
		Prepare.Then<FVBSkillEquip>(C.GetString(TEXT("Weapon"), TEXT("Fighter")),
			FName(*C.GetString(TEXT("WeaponAction"), TEXT("Input.WeaponSlot.3"))));
		Prepare.Then<FVBSkillWait>(TEXT("settle camera baseline"), Settle);
		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			if (Press.Action == Aim)
			{
				Injected = Press.InjectedFrame;
			}
		};
		return true;
	}

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		AVBCharacter* P = Character.Get();
		if (!P || !Ctx.PC || !Ctx.PC->PlayerCameraManager)
		{
			return Fail(TEXT("CameraAim lost player/camera"));
		}

		if (Phase == 0)
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

			if (!bBaselineReset)
			{
				// 요청 스틱을 0 으로 두는 것만으로는 모자란다 - wake, equip 에서 남은 가속이 남아 있을 수 있다.
				Ctx.Input->ReleaseAll(Ctx.Frame);
				NeutralStart = Ctx.Frame;
				LastOffset = CameraOffset(Ctx);
				bBaselineReset = true;
			}

			const FVector CurrentOffset = CameraOffset(Ctx);
			const bool bNeutral = FVBPlayBotContext::RawMoveInput(P).IsNearlyZero(0.01) && P->GetVelocity().Size() < 1
				&& P->GetCharacterMovement()->GetCurrentAcceleration().IsNearlyZero(0.1)
				&& FVector::Dist(CurrentOffset, LastOffset) < 0.25 && FVBPlayBotContext::IsGrounded(P);
			LastOffset = CurrentOffset;
			NeutralSince = bNeutral ? (NeutralSince < 0 ? Ctx.Frame : NeutralSince) : -1;
			if (NeutralSince < 0 || Ctx.Frame - NeutralSince < Ctx.SecondsToFrames(Settle))
			{
				if (Ctx.Frame - NeutralStart > Ctx.SecondsToFrames(BaselineTimeout))
				{
					Ctx.Checks->Unavailable(TEXT("CameraAim.Baseline"), Ctx.Frame,
						FString::Printf(TEXT("actual neutral state did not settle: rawMove=%s accel=%s velocity=%s"),
							*FVBPlayBotContext::RawMoveInput(P).ToString(), *P->GetCharacterMovement()->GetCurrentAcceleration().ToString(), *P->GetVelocity().ToString()));
					return EVBPlayBotState::Succeeded;
				}

				return EVBPlayBotState::Running;
			}

			if (UVBLocomotionIntentComponent::IsAimingFor(P) || !FVBPlayBotContext::IsUsingGameplayCamera(P)
				|| !FVBPlayBotContext::IsGrounded(P))
			{
				Ctx.Checks->Unavailable(TEXT("CameraAim.Baseline"), Ctx.Frame,
					TEXT("requires grounded, non-aiming GameplayCamera baseline"));
				return EVBPlayBotState::Succeeded;
			}

			Start = P->GetActorLocation();
			Yaw = Ctx.PC->GetControlRotation().Yaw;
			View = Ctx.PC->GetViewTarget();
			Offset = CameraOffset(Ctx);
			Rotation = Ctx.PC->PlayerCameraManager->GetCameraRotation();
			Fov = Ctx.PC->PlayerCameraManager->GetFOVAngle();
			Press = VBPlayBotSkills::ReactPress(Ctx, Aim, FKey(), TEXT("aim from stable baseline"), Ctx.Frame, Hold);
			if (Press == INDEX_NONE)
			{
				return Fail(TEXT("Aim press not scheduled"));
			}

			PhaseStart = Ctx.Frame;
			Phase = 1;
		}

		bStable &= FVector::Dist(Start, P->GetActorLocation()) <= MaxDrift
			&& FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, double(Ctx.PC->GetControlRotation().Yaw))) <= MaxYaw
			&& Ctx.PC->GetViewTarget() == View.Get() && FVBPlayBotContext::IsUsingGameplayCamera(P) && FVBPlayBotContext::IsGrounded(P);
		MaxObservedDrift = FMath::Max(MaxObservedDrift, FVector::Dist(Start, P->GetActorLocation()));
		if (Phase == 1)
		{
			if (Injected >= 0 && Ctx.Frame - Injected >= Ctx.SecondsToFrames(Settle) && Ctx.Input->IsActive(Press))
			{
				bSawAim |= UVBLocomotionIntentComponent::IsAimingFor(P);
				LastChange = FVector::Dist(Offset, CameraOffset(Ctx));
				StableAimSamples = UVBLocomotionIntentComponent::IsAimingFor(P) && LastChange >= MinChange ? StableAimSamples + 1 : 0;
				bCameraResponded |= StableAimSamples >= Ctx.SecondsToFrames(0.2);
			}

			if (Injected >= 0 && !Ctx.Input->IsActive(Press))
			{
				Ctx.Checks->Expect(TEXT("CameraAim.Activates"), bSawAim, true, Ctx.Frame);
				Ctx.Checks->Expect(TEXT("CameraAim.CameraResponse"), bCameraResponded, true, Ctx.Frame,
					FString::Printf(TEXT("{\"offsetDeltaCm\":%.3f,\"fov\":%.3f,\"toggle\":%s}"), LastChange,
						Ctx.PC->PlayerCameraManager->GetFOVAngle(), bToggle ? TEXT("true") : TEXT("false")));
				PhaseStart = Ctx.Frame;
				Phase = 2;
			}
			else if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Hold + Timeout + 2))
			{
				Ctx.Checks->Expect(TEXT("CameraAim.Activates"), false, true, Ctx.Frame);
				Ctx.Checks->Unavailable(TEXT("CameraAim.Remaining"), Ctx.Frame,
					TEXT("aim injection/release timed out; dependent response/mode/return observations were not completed"));
				Ctx.Checks->Expect(TEXT("CameraAim.Stable"), bStable, true, Ctx.Frame);
				return EVBPlayBotState::Succeeded;
			}
		}

		if (Phase == 2 && Ctx.Frame - PhaseStart >= Ctx.SecondsToFrames(Settle))
		{
			Ctx.Checks->Expect(TEXT("CameraAim.InputMode"), UVBLocomotionIntentComponent::IsAimingFor(P), bToggle, Ctx.Frame);
			if (bToggle)
			{
				Injected = -1;
				Press = VBPlayBotSkills::ReactPress(Ctx, Aim, FKey(), TEXT("toggle aim off"), Ctx.Frame, 0);
				if (Press == INDEX_NONE)
				{
					return Fail(TEXT("second Aim press not scheduled"));
				}
			}

			Phase = 3;
			PhaseStart = Ctx.Frame;
		}

		if (Phase == 3 && (!bToggle || (Injected >= 0 && !Ctx.Input->IsActive(Press))))
		{
			const FRotator Delta = (Ctx.PC->PlayerCameraManager->GetCameraRotation() - Rotation).GetNormalized();
			const double ReturnDistance = FVector::Dist(Offset, CameraOffset(Ctx));
			const bool bReturned = !UVBLocomotionIntentComponent::IsAimingFor(P) && ReturnDistance <= ReturnTolerance
				&& FMath::Max3(FMath::Abs(Delta.Pitch), FMath::Abs(Delta.Yaw), FMath::Abs(Delta.Roll)) <= RotationTolerance
				&& FMath::Abs(Ctx.PC->PlayerCameraManager->GetFOVAngle() - Fov) <= FovTolerance;
			ReturnSamples = bReturned ? ReturnSamples + 1 : 0;
			if (ReturnSamples >= Ctx.SecondsToFrames(0.2) || Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Timeout))
			{
				Ctx.Checks->Expect(TEXT("CameraAim.Returns"), ReturnSamples >= Ctx.SecondsToFrames(0.2), true, Ctx.Frame,
					FString::Printf(TEXT("{\"returnDistanceCm\":%.3f,\"maxPlayerDriftCm\":%.3f}"), ReturnDistance, MaxObservedDrift));
				Ctx.Checks->Expect(TEXT("CameraAim.Stable"), bStable, true, Ctx.Frame,
					FString::Printf(TEXT("{\"maxPlayerDriftCm\":%.3f}"), MaxObservedDrift));
				return EVBPlayBotState::Succeeded;
			}
		}

		if (Phase == 3 && Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(Timeout + 2))
		{
			Ctx.Checks->Expect(TEXT("CameraAim.Returns"), false, true, Ctx.Frame);
			Ctx.Checks->Expect(TEXT("CameraAim.Stable"), bStable, true, Ctx.Frame);
			return EVBPlayBotState::Succeeded;
		}

		return EVBPlayBotState::Running;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.OnPressInjected = nullptr;
	}

private:
	FVector CameraOffset(const FVBPlayBotContext& Ctx) const
	{
		return FRotator(0, Ctx.PC->GetControlRotation().Yaw, 0).UnrotateVector(
			Ctx.PC->PlayerCameraManager->GetCameraLocation() - Ctx.Player->GetActorLocation());
	}

	TWeakObjectPtr<AVBCharacter> Character;
	TWeakObjectPtr<AActor> View;
	FVBPlayBotSequence Prepare;
	FName Aim;
	int32 Phase = 0, Press = INDEX_NONE;
	int64 PhaseStart = 0, Injected = -1, StableAimSamples = 0, ReturnSamples = 0;
	int64 NeutralStart = 0, NeutralSince = -1;
	double Hold = 1.5, Settle = 0.5, Timeout = 2, MinChange = 15, ReturnTolerance = 10;
	double RotationTolerance = 3, FovTolerance = 2, MaxDrift = 5, MaxYaw = 2, Yaw = 0, Fov = 0, LastChange = 0;
	double BaselineTimeout = 5, MaxObservedDrift = 0;
	FVector Start = FVector::ZeroVector, Offset = FVector::ZeroVector;
	FVector LastOffset = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	bool bToggle = false, bSawAim = false, bCameraResponded = false, bStable = true, bBaselineReset = false;
};
VB_PLAYBOT_SCENARIO(FScenario, "CameraAim")
}
