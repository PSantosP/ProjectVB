// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBWeaponPickup.h"
#include "UObject/StrongObjectPtr.h"

// 전투 중 줍기 - 카메라 앞에 무기 pickup 을 하나씩 놓고 세 번 시험한다.
// 1 교전 중: body montage 없이 바로 줍는다. 2 평시: rate 1 로 재생되고 CommitTime 에 접촉한다.
// 3 평시: 접촉 전부터 쥔 스틱은 끊지 않고, 놓았다 다시 밀면 회복 구간을 끊고 움직인다.
class FVBPlayBotScenario_PickupCombat : public FVBPlayBotScenarioBase
{
	struct FTrial
	{
		TWeakObjectPtr<AVBWeaponPickup> Pickup;
		TWeakObjectPtr<UVBInteractableComponent> Interactable;
		int64 PressFrame = -1;
		bool bEngagedAtPress = false;
		int64 MontageStartFrame = -1;
		int64 MontageEndFrame = -1;
		int64 ConsumedFrame = -1;
		float MaxPlayRate = 0.f;
		// Trial 3 only.
		int64 HoldStartFrame = -1;
		int64 ReleaseFrame = -1;
		int64 RepushFrame = -1;
		int64 MoveFrame = -1;
		bool bPlayingBeforeRepush = false;
	};

public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override
	{
		const FVBPlayBotRunConfig& C = *Ctx.Config;
		FString ConfigPath;
		if (!C.RequireString(TEXT("EnemyClass"), EnemyClass, OutError) || !C.RequireString(TEXT("PickupConfig"), ConfigPath, OutError))
		{
			return false;
		}

		PickupConfig.Reset(LoadObject<UVBInteractableConfig>(nullptr, *ConfigPath));
		Interact = FName(*C.GetString(TEXT("InteractAction"), TEXT("Input.Interact")));
		EnemyDistance = C.GetDouble(TEXT("EnemyDistance"), 900.0);
		PickupDistance = C.GetDouble(TEXT("PickupDistance"), 110.0);
		InstantMaxFrames = C.GetInt(TEXT("InstantMaxFrames"), 10);
		ContactToleranceFrames = C.GetInt(TEXT("ContactToleranceFrames"), 6);
		CutWithinFrames = C.GetInt(TEXT("CutWithinFrames"), 12);
		HeldPastContactSeconds = C.GetDouble(TEXT("HeldPastContactSeconds"), 0.3);
		ReleaseSeconds = C.GetDouble(TEXT("ReleaseSeconds"), 0.15);
		if (!PickupConfig || !PickupConfig->ActivateMotion.CharacterMontage || PickupConfig->ActivateMotion.CommitTime <= 0.f)
		{
			OutError = FString::Printf(TEXT("[%s] PickupConfig %s did not load with a body montage and a commit time"), *C.Section,
				*ConfigPath);
			return false;
		}

		if (EnemyDistance <= 0 || PickupDistance <= 0 || InstantMaxFrames <= 0 || ContactToleranceFrames < 0 || CutWithinFrames <= 0
			|| HeldPastContactSeconds <= 0 || ReleaseSeconds <= FVBMoveCancelGate::Interval * 2)
		{
			OutError = FString::Printf(TEXT("[%s] invalid distance/frame/second parameters"), *C.Section);
			return false;
		}

		if (!VBPlayBotSkills::EnsureAction(Ctx, Interact, OutError))
		{
			return false;
		}

		Trials.SetNum(3);
		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			if (Press.Action != Interact || !Trials.IsValidIndex(Current) || Trials[Current].PressFrame >= 0)
			{
				return;
			}

			Trials[Current].PressFrame = Press.InjectedFrame;
			Trials[Current].bEngagedAtPress = IsEngaged(Ctx_World.Get());
		};

		// 시험 1 - 교전 중. AI 가 도는 살아 있는 병사가 먼저 플레이어를 대상으로 잡아야 한다.
		Sequence.Then<FVBSkillDo>(TEXT("spawn a live hostile"), [this](FVBPlayBotContext& C2, FString& Error)
		{
			Ctx_World = C2.World;
			UClass* Class = FSoftClassPath(EnemyClass).TryLoadClass<ACharacter>();
			if (!Class) { Error = FString::Printf(TEXT("%s did not load as a Character"), *EnemyClass); return false; }
			const FVector Forward = C2.Player->GetActorForwardVector().GetSafeNormal2D();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			Enemy = C2.World->SpawnActor<ACharacter>(Class, C2.Player->GetActorLocation() + Forward * EnemyDistance,
				(-Forward).Rotation(), Params);
			if (!Enemy.IsValid()) { Error = TEXT("hostile spawn failed"); return false; }
			if (!Enemy->GetController())
			{
				Enemy->SpawnDefaultController();
			}

			C2.Watch(Enemy.Get());
			C2.Target = Enemy.Get();
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("hostile targets the player"), [this](FVBPlayBotContext& C2) { return IsEngaged(C2.World); }, 10.0);
		AddPickupTrial(0);
		// 시험 2-3 - 평시. 적을 치우고 coordinator 가 교전을 풀 때까지 기다린다.
		Sequence.Then<FVBSkillDo>(TEXT("remove the hostile"), [this](FVBPlayBotContext& C2, FString&)
		{
			if (Enemy.IsValid())
			{
				Enemy->Destroy();
			}

			C2.Target = nullptr;
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(TEXT("engagement ends"), [this](FVBPlayBotContext& C2) { return !IsEngaged(C2.World); }, 10.0);
		AddPickupTrial(1);
		AddPickupTrial(2);
		return true;
	}

	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		Observe(Ctx);
		const EVBPlayBotState State = Sequence.Tick(Ctx);
		if (State == EVBPlayBotState::Running)
		{
			return State;
		}

		Report(Ctx);
		return State == EVBPlayBotState::Failed ? Fail(Sequence.GetFailure()) : State;
	}

private:
	static bool IsEngaged(UWorld* World)
	{
		const UVBCombatCoordinatorSubsystem* Coordinator = World ? World->GetSubsystem<UVBCombatCoordinatorSubsystem>() : nullptr;
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return Coordinator && PC && PC->GetPawn() && Coordinator->IsEngaged(PC->GetPawn());
	}

	bool IsPickupMontagePlaying(const FVBPlayBotContext& Ctx, float* OutRate = nullptr) const
	{
		const UAnimInstance* Anim = Ctx.Player && Ctx.Player->GetMesh() ? Ctx.Player->GetMesh()->GetAnimInstance() : nullptr;
		const UAnimMontage* Montage = PickupConfig ? PickupConfig->ActivateMotion.CharacterMontage.Get() : nullptr;
		const bool bPlaying = Anim && Montage && Anim->Montage_IsPlaying(Montage);
		if (bPlaying && OutRate)
		{
			*OutRate = Anim->Montage_GetPlayRate(Montage);
		}

		return bPlaying;
	}

	// AddPickupTrial - 실제 pickup config 로 카메라 앞에 놓고, focus 가 잡히면 interact 를 한 번 눌러 시험 끝까지 기다린다.
	void AddPickupTrial(int32 Index)
	{
		Sequence.Then<FVBSkillDo>(FString::Printf(TEXT("trial %d: spawn pickup"), Index + 1),
			[this, Index](FVBPlayBotContext& C2, FString& Error)
		{
			Current = Index;
			FTrial& T = Trials[Index];
			const FVector Forward = FRotator(0.0, C2.PC->GetControlRotation().Yaw, 0.0).Vector();
			const FTransform Spawn((-Forward).Rotation(), C2.Player->GetActorLocation() + Forward * PickupDistance);
			AVBWeaponPickup* Pickup = C2.World->SpawnActorDeferred<AVBWeaponPickup>(AVBWeaponPickup::StaticClass(), Spawn);
			UVBInteractableComponent* Interactable = Pickup ? Pickup->FindComponentByClass<UVBInteractableComponent>() : nullptr;
			// Config 는 protected 이고 에디터 배치마다 넣는 값이라, spawn 한 pickup 에는 reflection 으로 넣는다.
			FObjectProperty* ConfigProperty = FindFProperty<FObjectProperty>(UVBInteractableComponent::StaticClass(), TEXT("Config"));
			if (!Pickup || !Interactable || !ConfigProperty)
			{
				Error = TEXT("weapon pickup spawn or its Config property not found");
				return false;
			}

			ConfigProperty->SetObjectPropertyValue_InContainer(Interactable, PickupConfig.Get());
			Pickup->FinishSpawning(Spawn);
			T.Pickup = Pickup;
			T.Interactable = Interactable;
			return true;
		});
		Sequence.Then<FVBSkillWaitUntil>(FString::Printf(TEXT("trial %d: pickup focused"), Index + 1), [this, Index](FVBPlayBotContext& C2)
		{
			const UVBInteractorComponent* Interactor = C2.Player->FindComponentByClass<UVBInteractorComponent>();
			return Interactor && Trials[Index].Interactable.IsValid() && Interactor->FindBestInteractable() == Trials[Index].Interactable.Get();
		}, 3.0);
		Sequence.Then<FVBSkillPress>(FString::Printf(TEXT("trial %d: interact"), Index + 1), Interact);
		const double MontageSeconds = PickupConfig ? PickupConfig->ActivateMotion.CharacterMontage->GetPlayLength() : 3.0;
		Sequence.Then<FVBSkillWaitUntil>(FString::Printf(TEXT("trial %d: done"), Index + 1), [this, Index](FVBPlayBotContext& C2)
		{
			const FTrial& T = Trials[Index];
			const bool bMontageOver = T.MontageStartFrame < 0 || T.MontageEndFrame >= 0;
			// 시험 3 은 플레이어가 움직일 때까지(또는 분명히 안 움직일 때까지) 스틱을 민다 - 놓는 것은 settle 이 한다.
			const bool bMoveSeen = Index != 2 || T.MoveFrame >= 0 || (T.MontageEndFrame >= 0
				&& C2.Frame - T.MontageEndFrame > CutWithinFrames * 3);
			return T.ConsumedFrame >= 0 && bMontageOver && bMoveSeen;
		}, MontageSeconds + 4.0);
		Sequence.Then<FVBSkillDo>(FString::Printf(TEXT("trial %d: settle"), Index + 1), [](FVBPlayBotContext& C2, FString&)
		{
			C2.Input->ReleaseAll(C2.Frame);
			return true;
		});
		Sequence.Then<FVBSkillWait>(FString::Printf(TEXT("trial %d: rest"), Index + 1), 0.5);
	}

	void Observe(FVBPlayBotContext& Ctx)
	{
		if (!Trials.IsValidIndex(Current))
		{
			return;
		}

		FTrial& T = Trials[Current];
		float Rate = 0.f;
		const bool bPlaying = IsPickupMontagePlaying(Ctx, &Rate);
		if (bPlaying && T.MontageStartFrame < 0 && T.PressFrame >= 0)
		{
			T.MontageStartFrame = Ctx.Frame;
		}

		if (bPlaying)
		{
			T.MaxPlayRate = FMath::Max(T.MaxPlayRate, Rate);
		}

		if (!bPlaying && T.MontageStartFrame >= 0 && T.MontageEndFrame < 0)
		{
			T.MontageEndFrame = Ctx.Frame;
		}

		if (T.ConsumedFrame < 0 && T.Interactable.IsValid() && T.Interactable->IsActivated())
		{
			T.ConsumedFrame = Ctx.Frame;
		}

		if (Current != 2 || T.MontageStartFrame < 0)
		{
			return;
		}

		// 시험 3 스틱 각본 - gate 를 보는 시험이라 사람 반응이 아니라 정확한 프레임으로 민다.
		const int64 Contact = T.MontageStartFrame + Ctx.SecondsToFrames(PickupConfig->ActivateMotion.CommitTime);
		const FVector2D Push = Ctx.ToStick(Ctx.Player->GetActorRightVector()) * 0.9;
		if (T.HoldStartFrame < 0 && Ctx.Frame >= Contact - Ctx.SecondsToFrames(0.3))
		{
			T.HoldStartFrame = Ctx.Frame;
			Ctx.Input->SetStickWant(Push);
		}

		if (T.HoldStartFrame >= 0 && T.ReleaseFrame < 0 && Ctx.Frame >= Contact + Ctx.SecondsToFrames(HeldPastContactSeconds))
		{
			T.ReleaseFrame = Ctx.Frame;
			Ctx.Input->SetStickWant(FVector2D::ZeroVector);
		}

		if (T.ReleaseFrame >= 0 && T.RepushFrame < 0 && Ctx.Frame >= T.ReleaseFrame + Ctx.SecondsToFrames(ReleaseSeconds))
		{
			T.RepushFrame = Ctx.Frame;
			T.bPlayingBeforeRepush = bPlaying;
			Ctx.Input->SetStickWant(Push);
		}

		if (T.RepushFrame >= 0 && T.MoveFrame < 0 && Ctx.Player->GetVelocity().Size2D() > 50.0)
		{
			T.MoveFrame = Ctx.Frame;
		}
	}

	void Report(FVBPlayBotContext& Ctx)
	{
		FVBPlayBotChecks& Checks = *Ctx.Checks;
		const double Commit = PickupConfig->ActivateMotion.CommitTime;
		auto Evidence = [](const FTrial& T)
		{
			return FString::Printf(TEXT("{\"press\":%lld,\"engaged\":%s,\"montageStart\":%lld,\"montageEnd\":%lld,\"consumed\":%lld,\"maxRate\":%.3f,")
				TEXT("\"holdStart\":%lld,\"release\":%lld,\"repush\":%lld,\"move\":%lld,\"playingBeforeRepush\":%s}"),
				T.PressFrame, T.bEngagedAtPress ? TEXT("true") : TEXT("false"), T.MontageStartFrame, T.MontageEndFrame, T.ConsumedFrame,
				T.MaxPlayRate, T.HoldStartFrame, T.ReleaseFrame, T.RepushFrame, T.MoveFrame, T.bPlayingBeforeRepush ? TEXT("true") : TEXT("false"));
		};

		const FTrial& E = Trials[0];
		if (E.PressFrame < 0 || !E.bEngagedAtPress)
		{
			Checks.Unavailable(TEXT("pickup_instant_engaged"), E.PressFrame,
				TEXT("no press while the coordinator reported the player engaged"));
		}
		else
		{
			const bool bInstant = E.MontageStartFrame < 0 && E.ConsumedFrame >= 0 && E.ConsumedFrame - E.PressFrame <= InstantMaxFrames;
			Checks.Expect(TEXT("pickup_instant_engaged"), bInstant, true, E.ConsumedFrame, Evidence(E));
		}

		const FTrial& N = Trials[1];
		if (N.PressFrame < 0 || N.bEngagedAtPress)
		{
			Checks.Unavailable(TEXT("pickup_montage_normal_rate"), N.PressFrame, TEXT("no calm press (still engaged or not pressed)"));
		}
		else
		{
			// 접촉은 montage 시작 뒤 CommitTime 이고, 스틱이 없으니 montage 는 제 끝(NaturalPlayingFrames)까지 돈다.
			const int64 Expected = N.MontageStartFrame + Ctx.SecondsToFrames(Commit);
			const bool bNormal = N.MontageStartFrame >= 0 && N.ConsumedFrame >= 0 && FMath::IsNearlyEqual(N.MaxPlayRate, 1.f, 0.01f)
				&& FMath::Abs(N.ConsumedFrame - Expected) <= ContactToleranceFrames
				&& (N.MontageEndFrame < 0 || N.MontageEndFrame - N.MontageStartFrame >= NaturalPlayingFrames(Ctx) - ContactToleranceFrames);
			Checks.Expect(TEXT("pickup_montage_normal_rate"), bNormal, true, N.ConsumedFrame, Evidence(N));
		}

		const FTrial& M = Trials[2];
		if (M.PressFrame < 0 || M.bEngagedAtPress || M.ReleaseFrame < 0)
		{
			Checks.Unavailable(TEXT("held_stick_does_not_cut"), M.PressFrame, TEXT("calm trial 3 did not reach the release"));
			Checks.Unavailable(TEXT("pickup_recovery_cut"), M.PressFrame, TEXT("calm trial 3 did not reach the release"));
			return;
		}
		// 접촉 내내 쥐었으면 montage 는 제 끝보다 먼저 끝나면 안 된다 - 놓은 프레임이 자연 끝과 겹칠 수 있어 자연 끝과 비교한다.
		const int64 NaturalEnd = M.MontageStartFrame + NaturalPlayingFrames(Ctx);
		const bool bHeldKept = M.MontageEndFrame < 0 || M.MontageEndFrame >= NaturalEnd - ContactToleranceFrames;
		Checks.Expect(TEXT("held_stick_does_not_cut"), bHeldKept, true, M.ReleaseFrame, Evidence(M));
		if (M.RepushFrame < 0 || !M.bPlayingBeforeRepush)
		{
			Checks.Unavailable(TEXT("pickup_recovery_cut"), M.RepushFrame,
				TEXT("the montage had already ended before the fresh push - nothing left to cut"));
			return;
		}
		// 끊기는 제 끝보다 분명히 먼저여야 한다 - 남은 회복 구간이 그보다 짧으면 끊기와 자연 끝을 구별할 수 없다.
		if (NaturalEnd - M.RepushFrame <= CutWithinFrames + ContactToleranceFrames)
		{
			Checks.Unavailable(TEXT("pickup_recovery_cut"), M.RepushFrame, FString::Printf(
				TEXT("only %lld frames of recovery left after the push - a cut cannot be told from the natural end"), NaturalEnd - M.RepushFrame));
			return;
		}

		const bool bCut = M.MontageEndFrame >= 0 && M.MontageEndFrame - M.RepushFrame <= CutWithinFrames
			&& M.MontageEndFrame < NaturalEnd - ContactToleranceFrames && M.MoveFrame >= 0;
		Checks.Expect(TEXT("pickup_recovery_cut"), bCut, true, M.MontageEndFrame, Evidence(M));
	}

	// NaturalPlayingFrames - montage 가 스스로 재생을 멈추기까지의 프레임. auto blend-out 은 끝보다 BlendOut 만큼 먼저이고 RateScale 이 길이를 줄인다.
	int64 NaturalPlayingFrames(const FVBPlayBotContext& Ctx) const
	{
		const UAnimMontage* Montage = PickupConfig->ActivateMotion.CharacterMontage;
		const double RateScale = Montage->RateScale > 0.f ? Montage->RateScale : 1.0;
		return Ctx.SecondsToFrames((Montage->GetPlayLength() - Montage->GetDefaultBlendOutTime()) / RateScale);
	}

	FVBPlayBotSequence Sequence;
	TArray<FTrial> Trials;
	int32 Current = INDEX_NONE;
	FString EnemyClass;
	FName Interact;
	TStrongObjectPtr<UVBInteractableConfig> PickupConfig;
	TWeakObjectPtr<ACharacter> Enemy;
	TWeakObjectPtr<UWorld> Ctx_World;
	double EnemyDistance = 900.0;
	double PickupDistance = 110.0;
	int32 InstantMaxFrames = 10;
	int32 ContactToleranceFrames = 6;
	int32 CutWithinFrames = 12;
	double HeldPastContactSeconds = 0.3;
	double ReleaseSeconds = 0.15;
};

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_PickupCombat, "PickupCombat")
