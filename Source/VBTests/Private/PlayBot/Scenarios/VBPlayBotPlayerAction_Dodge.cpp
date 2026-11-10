#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"
#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "PlayBot/VBPlayBotFrame.h"
#include "UObject/UnrealType.h"

namespace VBPlayBotPlayerActions
{
namespace
{
class FVBDodgeAction : public FVBPlayerSequenceAction
{
public:
	FVBDodgeAction() : FVBPlayerSequenceAction(TEXT("Dodge")) {}
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		if (!Configure(Ctx, Error) || !Observer.Bind(Ctx, Error))
		{
			return false;
		}

		const UVBDodgeConfig* Config = nullptr;
		for (const FGameplayAbilitySpec& Spec : Ctx.PlayerASC->GetActivatableAbilities())
		{
			if (!Spec.Ability || !Spec.Ability->IsA(UVBGA_Dodge::StaticClass()))
			{
				continue;
			}

			const UGameplayAbility* Instance = Spec.GetPrimaryInstance();
			const UGameplayAbility* Owner = Instance ? Instance : Spec.Ability.Get();
			const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner->GetClass(), TEXT("DodgeConfig"));
			if (Config || !Property) { Error = TEXT("dodge action needs one unambiguous configured dodge ability"); return false; }
			Config = Cast<UVBDodgeConfig>(Property->GetObjectPropertyValue_InContainer(Owner));
		}

		if (!Config || Config->AttackCancelSeconds <= 0.0 || Config->AttackBufferSeconds <= 0.0)
		{
			Error = TEXT("dodge action requires positive live cancel/buffer data"); return false;
		}

		CancelSeconds = Config->AttackCancelSeconds; BufferSeconds = Config->AttackBufferSeconds;
		ObservedASC = Ctx.PlayerASC;
		CommitHandle = Ctx.PlayerASC->RegisterGameplayTagEvent(VBGameplayTags::State_Combat_DodgeCommit,
			EGameplayTagEventType::NewOrRemoved)
			.AddLambda([this, &Ctx](const FGameplayTag, int32 Count)
			{
				if (Count > 0)
				{
					CommitStartTime = Ctx.World->GetTimeSeconds();
				}
				else
				{
					CommitEndTime = Ctx.World->GetTimeSeconds();
				}
			});
		for (const TCHAR* Name : {TEXT("Input.Dodge"), TEXT("Input.Attack.Light"), TEXT("Input.WeaponSlot.3")})
			if (!VBPlayBotSkills::EnsureAction(Ctx, FName(Name), Error))
			{
				return false;
			}
		AddNeutral();
		Sequence.Then<FVBSkillEquip>(TEXT("Fighter"), FName(TEXT("Input.WeaponSlot.3")));
		Sequence.Then<FVBSkillPress>(TEXT("dodge"), FName(TEXT("Input.Dodge")));
		// 회피 버튼을 아직 떼는 중이어도 눈에 보이는 회피 동작에 반응한다.
		Sequence.Then<FVBSkillWaitUntil>(TEXT("Light scheduled from visible dodge"), [this](FVBPlayBotContext&) { return DodgeLightPress != INDEX_NONE; }, Timeout);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("buffered attack and dodge complete"), [this](FVBPlayBotContext& C)
		{
			return Observer.Activations > 0 && Observer.AllReleased() && !Has(C, VBGameplayTags::State_Combat_DodgeCommit)
				&& !Has(C, VBGameplayTags::State_Combat_Attacking) && !Has(C, VBGameplayTags::State_Combat_Dodging);
		}, Timeout);
		AddNeutral();
		return true;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		Observer.Unbind(Ctx);
		if (UAbilitySystemComponent* ASC = ObservedASC.Get())
		{
			ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Combat_DodgeCommit,
				EGameplayTagEventType::NewOrRemoved).Remove(CommitHandle);
		}

		FVBPlayerSequenceAction::Teardown(Ctx);
	}

private:
	void Observe(FVBPlayBotContext& Ctx) override
	{
		Observer.Observe(Ctx);
		if (DodgeLightPress == INDEX_NONE && Has(Ctx, VBGameplayTags::State_Combat_Dodging))
		{
			DodgeLightPress = VBPlayBotSkills::ReactPress(Ctx, FName(TEXT("Input.Attack.Light")), FKey(),
				TEXT("visible dodge motion"), Ctx.Frame, 0.0);
		}

		const int32 Count = Ctx.PlayerASC->GetTagCount(VBGameplayTags::State_Combat_DodgeCommit);
		MaxCommit = FMath::Max(MaxCommit, Count);
	}

	void Finish(FVBPlayBotContext& Ctx) override
	{
		Observer.CheckReceipts(Ctx, TEXT("RG.Dodge.PressReceipt."));
		const bool bExercised = Observer.Presses.Num() == 1 && Observer.Presses[0].CommitAtInjection == 1;
		const double ActivationTime = Observer.ActivationTimes.Num() == 1 ? Observer.ActivationTimes[0] : -1.0;
		const double Age = bExercised ? ActivationTime - Observer.Presses[0].PressTime : -1.0;
		const double FrameSlack = VBPlayBot::FrameMs / 1000.0;
		const bool bDeferred = bExercised && CommitStartTime >= 0.0 && CommitEndTime - CommitStartTime + FrameSlack >= CancelSeconds
			&& ActivationTime + FrameSlack >= CommitStartTime + CancelSeconds && Age >= 0.0 && Age <= BufferSeconds + FrameSlack;
		Ctx.Checks->Expect(TEXT("RG.C7.DodgeStoredThenFires"), bDeferred && Observer.Activations == 1
			&& Observer.CommitActivations == 0 && Observer.AllReleased(), true, Ctx.Frame,
			FString::Printf(TEXT("{\"exercised\":%s,\"activations\":%d,\"duringCommit\":%d,\"start\":%.5f,\"end\":%.5f,\"activation\":%.5f,\"cancelSeconds\":%.5f,\"age\":%.5f,\"bufferSeconds\":%.5f}"),
				bExercised ? TEXT("true") : TEXT("false"), Observer.Activations, Observer.CommitActivations, CommitStartTime, CommitEndTime, ActivationTime, CancelSeconds, Age, BufferSeconds));
		Ctx.Checks->Expect(TEXT("RG.C7.DodgeCommitClears"), MaxCommit == 1 && !Has(Ctx, VBGameplayTags::State_Combat_DodgeCommit),
			true, Ctx.Frame);
		FVBPlayerSequenceAction::Finish(Ctx);
	}

	FVBPlayerPressObserver Observer;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedASC;
	FDelegateHandle CommitHandle;
	double CancelSeconds = 0.0, BufferSeconds = 0.0, CommitStartTime = -1.0, CommitEndTime = -1.0;
	int32 MaxCommit = 0;
	int32 DodgeLightPress = INDEX_NONE;
};

}

TUniquePtr<FVBPlayBotPlayerAction> CreateDodge()
{
	return MakeUnique<FVBDodgeAction>();
}
}
