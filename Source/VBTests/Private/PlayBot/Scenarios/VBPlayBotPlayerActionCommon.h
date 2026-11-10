#pragma once

#include "PlayBot/Scenarios/VBPlayBotPlayerActionSteps.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystemGlobals.h"
#include "Character/VBCharacter.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace VBPlayBotPlayerActions
{
bool Positive(const FVBPlayBotContext& Ctx, const TCHAR* Key, double& Value, FString& Error);
bool Has(const FVBPlayBotContext& Ctx, const FGameplayTag& Tag);
bool Neutral(const FVBPlayBotContext& Ctx);
double Health(const AActor* Actor);

class FVBPlayerSequenceAction : public FVBPlayBotPlayerAction
{
public:
	explicit FVBPlayerSequenceAction(FString Name) : ActionName(MoveTemp(Name)) {}
	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (!Body.IsValid() || Ctx.Player != Body.Get() || Has(Ctx, VBGameplayTags::State_Dead))
		{
			Ctx.Checks->Expect(TEXT("RG.") + ActionName + TEXT(".OwnerUsable"), false, true, Ctx.Frame);
			return EVBPlayBotState::Succeeded;
		}

		if (FirstRecordedFrame < 0)
		{
			FirstRecordedFrame = Ctx.Frame;
		}

		Observe(Ctx);
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		Ctx.Checks->Expect(TEXT("RG.") + ActionName + TEXT(".Sequence"), State == EVBPlayBotState::Succeeded, true, Ctx.Frame,
			FString::Printf(TEXT("{\"why\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
		Finish(Ctx);
		return EVBPlayBotState::Succeeded;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.OnPressInjected = nullptr;
		Ctx.OnHit = nullptr;
	}

protected:
	bool Configure(FVBPlayBotContext& Ctx, FString& Error)
	{
		Body = Cast<AVBCharacter>(Ctx.Player);
		if (!Body.IsValid() || !Ctx.PC || !Ctx.PlayerASC || !Ctx.Player->HasAuthority() || Ctx.PlayerASC->GetAvatarActor() != Ctx.Player)
		{
			Error = TEXT("player action requires the real local authority character/controller/ASC");
			return false;
		}

		return Positive(Ctx, TEXT("StateTimeoutSeconds"), Timeout, Error);
	}

	void AddNeutral()
	{
		Sequence.Then<FVBSkillDo>(TEXT("neutral input"), [](FVBPlayBotContext& C, FString&)
		{
			C.Input->ReleaseAll(C.Frame);
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("actual neutral grounded state"), Neutral, Timeout);
	}

	virtual void Observe(FVBPlayBotContext& Ctx) {}
	virtual void Finish(FVBPlayBotContext& Ctx)
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.Checks->Expect(TEXT("RG.") + ActionName + TEXT(".InputReleased"), Ctx.Input->GetDownKeys().IsEmpty()
			&& FVBPlayBotContext::RawMoveInput(Ctx.Player).IsNearlyZero() && Ctx.Input->GetLookOut().IsNearlyZero(), true, Ctx.Frame);
		Ctx.Checks->Look(TEXT("RG.") + ActionName, FirstRecordedFrame, Ctx.Frame,
			TEXT("player action - visual feel remains user judgement"));
		Ctx.Checks->Expect(TEXT("RG.") + ActionName + TEXT(".NoStuckCommit"), !Has(Ctx, VBGameplayTags::State_Combat_DodgeCommit),
			true, Ctx.Frame);
	}

	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<AVBCharacter> Body;
	double Timeout = 0.0;
	FString ActionName;
	int64 FirstRecordedFrame = -1;
};

struct FVBPlayerPressReceipt
{
	int32 Serial = 0;
	int64 Injected = -1;
	bool bRecorded = false;
	bool bReleased = false;
	int32 CommitAtInjection = 0;
	double PressTime = -1.0;
	FVector2D Raw = FVector2D::ZeroVector;
	double Yaw = 0.0;
};

// 기존 ledger, delegate 만 지켜본다 - 여기서 입력 의도를 소비하면 어빌리티가 받을 것을 빼앗는다.
class FVBPlayerPressObserver
{
public:
	bool Bind(FVBPlayBotContext& Ctx, FString& Error)
	{
		ASC = Cast<UVBAbilitySystemComponent>(Ctx.PlayerASC);
		if (!ASC.IsValid() || ASC->FindPress(VBGameplayTags::Input_Attack_Light, 1))
		{
			Error = TEXT("press observer requires a fresh avatar ledger"); return false;
		}

		Activation = ASC->AbilityActivatedCallbacks.AddLambda([this, &Ctx](UGameplayAbility* Ability)
		{
			if (!Ability || !Ability->IsA(UVBGA_MeleeAttackBase::StaticClass()))
			{
				return;
			}

			++Activations;
			ActivationTimes.Add(Ctx.World->GetTimeSeconds());
			if (Ctx.Player->bIsCrouched)
			{
				++CrouchedActivations;
			}

			if (Has(Ctx, VBGameplayTags::State_Combat_DodgeCommit))
			{
				++CommitActivations;
			}
		});
		Combo = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_InputReceived)
			.AddLambda([this](const FGameplayEventData*) { ++ComboEvents; });
		Ctx.OnPressInjected = [this, &Ctx](const FVBPlayBotPress& Press)
		{
			if (Press.Action != FName(TEXT("Input.Attack.Light")))
			{
				return;
			}

			FVBPlayerPressReceipt& Receipt = Presses.AddDefaulted_GetRef();
			Receipt.Serial = Presses.Num(); Receipt.Injected = Press.InjectedFrame;
			Receipt.CommitAtInjection = Ctx.PlayerASC->GetTagCount(VBGameplayTags::State_Combat_DodgeCommit);
			Receipt.Raw = FVBPlayBotContext::RawMoveInput(Ctx.Player);
			Receipt.Yaw = Ctx.PC->GetControlRotation().Yaw;
		};
		return true;
	}

	void Observe(FVBPlayBotContext& Ctx)
	{
		if (!ASC.IsValid())
		{
			return;
		}

		for (FVBPlayerPressReceipt& Receipt : Presses)
		{
			if (Receipt.bReleased)
			{
				continue;
			}

			const FVBInputPress* Press = ASC->FindPress(VBGameplayTags::Input_Attack_Light, Receipt.Serial);
			if (!Press)
			{
				continue;
			}

			const bool bRelease = Press->ReleaseTime >= Press->PressTime && Press->bReleaseRequestAccepted;
			if (Receipt.bRecorded && !bRelease)
			{
				continue;
			}

			Receipt.bRecorded = Press->Serial == Receipt.Serial && Press->PressTime >= 0.0;
			Receipt.bReleased = bRelease; Receipt.PressTime = Press->PressTime;
		}
	}

	bool AllReleased() const
	{
		return !Presses.IsEmpty() && !Presses.ContainsByPredicate([](const FVBPlayerPressReceipt& P) { return !P.bRecorded || !P.bReleased; });
	}

	void CheckReceipts(FVBPlayBotContext& Ctx, const FString& Prefix) const
	{
		for (const FVBPlayerPressReceipt& P : Presses)
		{
			Ctx.Checks->Expect(Prefix + FString::FromInt(P.Serial), P.bRecorded && P.bReleased, true, Ctx.Frame,
				FString::Printf(TEXT("{\"serial\":%d,\"injected\":%lld,\"pressTime\":%.6f,\"raw\":[%.5f,%.5f],\"yaw\":%.3f,\"commit\":%d}"),
					P.Serial, P.Injected, P.PressTime, P.Raw.X, P.Raw.Y, P.Yaw, P.CommitAtInjection));
		}
	}

	void Unbind(FVBPlayBotContext& Ctx)
	{
		if (UVBAbilitySystemComponent* Owner = ASC.Get())
		{
			Owner->AbilityActivatedCallbacks.Remove(Activation);
			if (FGameplayEventMulticastDelegate* Event = Owner->GenericGameplayEventCallbacks.Find(VBGameplayTags::Event_Combo_InputReceived))
			{
				Event->Remove(Combo);
			}
		}
	}

	TArray<FVBPlayerPressReceipt> Presses;
	TArray<double> ActivationTimes;
	int32 Activations = 0, ComboEvents = 0, CrouchedActivations = 0, CommitActivations = 0;

private:
	TWeakObjectPtr<UVBAbilitySystemComponent> ASC;
	FDelegateHandle Activation, Combo;
};


TUniquePtr<FVBPlayBotPlayerAction> CreateMoveJumpLand();
TUniquePtr<FVBPlayBotPlayerAction> CreateLightCombo();
TUniquePtr<FVBPlayBotPlayerAction> CreateDodge();
TUniquePtr<FVBPlayBotPlayerAction> CreateTurnInPlace();
}
