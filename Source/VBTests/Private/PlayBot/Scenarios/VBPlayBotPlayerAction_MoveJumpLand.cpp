#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"
#include "Data/VBMovementConfig.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

namespace VBPlayBotPlayerActions
{
namespace
{
class FVBMoveJumpLandAction : public FVBPlayerSequenceAction
{
public:
	FVBMoveJumpLandAction() : FVBPlayerSequenceAction(TEXT("Move")) {}
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		if (!Configure(Ctx, Error) || !Positive(Ctx, TEXT("LowMagnitude"), Low, Error)
			|| !Positive(Ctx, TEXT("HighMagnitude"), High, Error) || !Positive(Ctx, TEXT("HoldSeconds"), Hold, Error)
			|| !Positive(Ctx, TEXT("MinTravelCm"), MinTravel, Error))
		{
			return false;
		}

		StableFrames = Ctx.Config->GetInt(TEXT("StableFrames"), 0);
		if (StableFrames <= 0) { Error = TEXT("StableFrames must be a positive frame count"); return false; }
		const UVBMovementConfig* Config = Body->GetMovementConfig();
		if (!Config || !(Low < Config->AnalogWalkRunThreshold && High >= Config->AnalogWalkRunThreshold && High <= 1.0))
		{
			Error = TEXT("movement magnitudes must bracket the live analog threshold inside (0,1]");
			return false;
		}

		Intent = UVBLocomotionIntentComponent::FindFor(Body.Get());
		Threshold = Config->AnalogWalkRunThreshold;
		const FVBSettingsSaveData& Settings = UVBSaveGameSubsystem::GetSettingsData(Ctx.PC);
		DeadZone = Settings.GamepadMoveDeadZone;
		if (!FMath::IsFinite(DeadZone) || DeadZone < 0.0 || DeadZone >= 1.0)
		{ Error = TEXT("input-owner move deadzone must be finite in [0,1)"); return false; }
		if (!VBPlayBotSkills::EnsureAction(Ctx, TEXT("Input.Jump"), Error))
		{
			return false;
		}

		AddNeutral();
		AddLeg(TEXT("RG.Analog.LowWalk"), Low, EVBGait::Walk);
		AddLeg(TEXT("RG.Analog.HighRun"), High, EVBGait::Run);
		AddLeg(TEXT("RG.Analog.FullRunRelease"), 1.0, EVBGait::Run);
		Sequence.Then<FVBSkillDo>(TEXT("jump baseline"), [this](FVBPlayBotContext&, FString&) { AirFrame = -1; return true; });
		Sequence.Then<FVBSkillPress>(TEXT("jump from clear floor"), FName(TEXT("Input.Jump")));
		Sequence.Then<FVBSkillWaitUntil>(TEXT("takeoff and real landing"), [this](FVBPlayBotContext& C) { return AirFrame >= 0 && Neutral(C); }, Timeout);
		Sequence.Then<FVBSkillDo>(TEXT("jump receipt"), [this](FVBPlayBotContext& C, FString&)
		{
			C.Checks->Expect(TEXT("RG.Move.JumpLandRecovery"), AirFrame >= 0 && Neutral(C), true, C.Frame,
				FString::Printf(TEXT("{\"takeoff\":%lld,\"z\":%.3f}"), AirFrame, C.Player->GetActorLocation().Z));
			return true;
		});
		AddLeg(TEXT("RG.Move.FreshMoveAfterLand"), High, EVBGait::Run);
		return true;
	}

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (Body.IsValid() && Body.Get() == Ctx.Player && !Has(Ctx, VBGameplayTags::State_Dead) && !Intent.IsValid())
		{
			Ctx.Checks->Unavailable(TEXT("RG.Analog.IntentOwner"), Ctx.Frame,
				TEXT("required locomotion intent owner is absent; gait and sprint cannot be observed"));
			return EVBPlayBotState::Succeeded;
		}

		return FVBPlayerSequenceAction::Tick(Ctx);
	}

private:
	void AddLeg(FString Name, double Magnitude, EVBGait Wanted)
	{
		Sequence.Then<FVBSkillDo>(Name + TEXT(" begin"), [this, Name, Magnitude, Wanted](FVBPlayBotContext& C, FString&)
		{
			Leg = Name; ExpectedGait = Wanted; GoodFrames = 0; Start = C.Player->GetActorLocation();
			const double DriverMagnitude = DeadZone + (1.0 - DeadZone) * Magnitude;
			C.Input->SetStickWant(FVector2D(0.0, DriverMagnitude));
			return true;
		});
		Sequence.Then<FVBSkillWait>(Name + TEXT(" hold"), Hold);
		Sequence.Then<FVBSkillDo>(Name + TEXT(" evidence"), [this, Name](FVBPlayBotContext& C, FString&)
		{
			const double Travel = FVector::Dist2D(Start, C.Player->GetActorLocation());
			C.Checks->Expect(Name, GoodFrames >= StableFrames && Travel >= MinTravel, true, C.Frame,
				FString::Printf(TEXT("{\"qualifiedFrames\":%d,\"travelCm\":%.3f,\"threshold\":%.3f,\"deadZone\":%.4f,\"driverMagnitude\":%.4f,\"raw\":%.4f,\"gait\":%d}"),
					GoodFrames, Travel, Threshold, DeadZone, C.Input->GetStickOut().Size(), FVBPlayBotContext::RawMoveInput(C.Player).Size(), static_cast<int32>(Intent->GetGait())));
			Leg.Empty();
			return true;
		});
		AddNeutral();
		Sequence.Then<FVBSkillDo>(Name + TEXT(" neutral receipt"), [this, Name](FVBPlayBotContext& C, FString&)
		{
			C.Checks->Expect(Name + TEXT(".Released"), Neutral(C) && Intent->GetGait() == EVBGait::Run, true, C.Frame);
			return true;
		});
	}

	void Observe(FVBPlayBotContext& Ctx) override
	{
		const UCharacterMovementComponent* Move = Ctx.Player->GetCharacterMovement();
		if (Move && Move->IsFalling() && Ctx.Player->GetVelocity().Z > 0.0 && AirFrame < 0)
		{
			AirFrame = Ctx.Frame;
		}

		if (Leg.IsEmpty())
		{
			return;
		}

		const double Raw = FVBPlayBotContext::RawMoveInput(Ctx.Player).Size();
		const bool bMagnitude = ExpectedGait == EVBGait::Walk ? Raw > 0.0 && Raw < Threshold : Raw >= Threshold;
		const bool bGood = bMagnitude && Intent->GetGait() == ExpectedGait && !Intent->GetIsSprinting() && FVBPlayBotContext::IsGrounded(Ctx.Player);
		GoodFrames = bGood ? GoodFrames + 1 : 0;

	}

	TWeakObjectPtr<UVBLocomotionIntentComponent> Intent;
	FString Leg;
	EVBGait ExpectedGait = EVBGait::Run;
	FVector Start = FVector::ZeroVector;
	double Low = 0.0, High = 0.0, Hold = 0.0, MinTravel = 0.0, Threshold = 0.0, DeadZone = 0.0;
	int32 GoodFrames = 0, StableFrames = 0;
	int64 AirFrame = -1;
};

}

TUniquePtr<FVBPlayBotPlayerAction> CreateMoveJumpLand()
{
	return MakeUnique<FVBMoveJumpLandAction>();
}
}
