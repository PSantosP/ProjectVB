// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/Scenarios/VBPlayBotScenario_FastTravel.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotSkills.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Interaction/VBSavePoint.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Map/VBFastTravelComponent.h"
#include "Map/VBTravelPointSubsystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "UI/Map/VBFastTravelScreenWidget.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace VBPlayBotFastTravel
{
class FScenario : public FVBPlayBotScenarioBase
{
public:
	bool Setup(FVBPlayBotContext& Ctx, FString& Error) override
	{
		FString A, B;
		if (!Ctx.Config->RequireString(TEXT("StartPointId"), A, Error) || !Ctx.Config->RequireString(TEXT("FarPointId"), B, Error))
		{
			return false;
		}

		StartId = FGameplayTag::RequestGameplayTag(FName(*A), false);
		FarId = FGameplayTag::RequestGameplayTag(FName(*B), false);
		ArrivalRadius = Ctx.Config->GetDouble(TEXT("ArrivalRadiusCm"), 150);
		FeetTolerance = Ctx.Config->GetDouble(TEXT("ArrivalFeetHeightToleranceCm"), 50);
		SearchRadius = Ctx.Config->GetDouble(TEXT("ActorSearchRadiusCm"), 50000);
		ApproachTimeout = Ctx.Config->GetDouble(TEXT("ApproachTimeoutSeconds"), 120);
		TravelTimeout = Ctx.Config->GetDouble(TEXT("TravelTimeoutSeconds"), 20);
		StableSeconds = Ctx.Config->GetDouble(TEXT("StableArrivalSeconds"), 0.5);
		FadeTolerance = Ctx.Config->GetDouble(TEXT("FadeAlphaTolerance"), 0.02);
		DiscoveryTimeout = Ctx.Config->GetDouble(TEXT("DiscoveryTimeoutSeconds"), 5);
		Interact = FName(*Ctx.Config->GetString(TEXT("InteractAction"), TEXT("Input.Interact")));
		TravelButton = FName(*Ctx.Config->GetString(TEXT("TravelButtonName"), TEXT("BtnTravel")));
		DestinationText = FName(*Ctx.Config->GetString(TEXT("DestinationTextName"), TEXT("TxtDestination")));
		ScreenTag = FGameplayTag::RequestGameplayTag(FName(*Ctx.Config->GetString(TEXT("ScreenTag"), TEXT("UI.Screen.FastTravel"))), false);
		ScreenClass = FSoftClassPath(Ctx.Config->GetString(TEXT("ScreenClass"))).TryLoadClass<UVBFastTravelScreenWidget>();
		Travel = Ctx.PC ? Ctx.PC->FindComponentByClass<UVBFastTravelComponent>() : nullptr;
		Restore = Ctx.Player->FindComponentByClass<UVBPlayerRestoreComponent>();
		Interactor = Ctx.Player->FindComponentByClass<UVBInteractorComponent>();
		Registry = UVBTravelPointSubsystem::Get(Ctx.Player);
		if (!StartId.IsValid() || !FarId.IsValid() || StartId == FarId || ArrivalRadius <= 0 || FeetTolerance <= 0 || SearchRadius <= 0
			|| ApproachTimeout <= 0 || TravelTimeout <= 0 || StableSeconds <= 0 || DiscoveryTimeout <= 0 || FadeTolerance < 0 || FadeTolerance >= 1
			|| !ScreenClass.IsValid() || !ScreenTag.IsValid() || !Travel.IsValid() || !Restore.IsValid() || !Interactor.IsValid()
			|| !Registry.IsValid() || !Ctx.Player->HasAuthority())
		{
			Error = TEXT("FastTravel requires an authority player with travel/restore/interactor, UI class/tag and valid parameters");
			return false;
		}
		// 저장소 격리는 시나리오 Setup 전에 Core 가 확인한다. 여기서는 Auto 발견이 저장해도 된다.
		if (!VBPlayBotSkills::EnsureAction(Ctx, Interact, Error))
		{
			return false;
		}

		if (!VBPlayBotSkills::CommonUIKey(Ctx, false, ConfirmKey, Error))
		{
			return false;
		}

		if (Ctx.Config->GetArray(TEXT("DestinationRoute")).IsEmpty() || Ctx.Config->GetArray(TEXT("TravelButtonRoute")).IsEmpty()
			|| Ctx.Config->GetArray(TEXT("ConfirmRoute")).IsEmpty())
		{
			Error = TEXT("FastTravel requires DestinationRoute, TravelButtonRoute and ConfirmRoute ini data");
			return false;
		}

		Observer.Reset(NewObject<UVBPlayBotTravelObserver>());
		Restore->OnRestoreCompleted.AddDynamic(Observer.Get(), &UVBPlayBotTravelObserver::OnRestored);
		ViewHandle = Travel->OnViewChanged.AddLambda([this]()
		{
			// 페이드 길이가 0 이어도 ServerTravel 은 CloseTravel 방송 전에 bTravelling 을 세운다.
			if (bRequestArmed && Travel.IsValid())
			{
				bSawTravel |= Travel->IsTravelling();
			}
		});
		Ctx.OnPressInjected = [this](const FVBPlayBotPress& Press)
		{
			// Slate 가 이 알림 전에 key-down 처리 중 화면을 닫을 수 있다. 포커스는 앞의 가드가 봤으니
			// 여기서는 실제 키를 보고, 도착은 따로 증명한다.
			if (bRequestArmed && Press.Key == ConfirmKey)
			{
				bConfirmInjected = true;
			}
		};
		return true;
	}

	EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override
	{
		if (!Travel.IsValid() || !Restore.IsValid() || !Registry.IsValid() || !Interactor.IsValid())
		{
			return Fail(TEXT("FastTravel lost a required component/subsystem"));
		}

		if (Phase == EPhase::Resolve)
		{
			AVBSavePoint* Start = Registry->FindPoint(StartId);
			AVBSavePoint* Far = Registry->FindPoint(FarId);
			if (!Start || !Far || Ctx.Distance2D(Start) > SearchRadius || Ctx.Distance2D(Far) > SearchRadius)
			{
				Ctx.Checks->Unavailable(TEXT("FastTravel.RoundTrip"), Ctx.Frame,
					TEXT("two configured registered points are not available inside the search radius"));
				return EVBPlayBotState::Succeeded;
			}

			Points[0] = Start;
			Points[1] = Far;
			const FVector StartLocation = Start->GetActorLocation(), FarLocation = Far->GetActorLocation();
			Ctx.Emit(FString::Printf(TEXT("{\"k\":\"ev\",\"e\":\"travel_fixture\",\"f\":%lld,\"start\":\"%s\",\"far\":\"%s\",\"startDiscovered\":%s,\"farDiscovered\":%s,\"startPos\":[%.3f,%.3f,%.3f],\"farPos\":[%.3f,%.3f,%.3f],\"distanceCm\":%.3f,\"approachTimeoutSeconds\":%.3f}"),
				Ctx.Frame, *Start->GetPathName().ReplaceCharWithEscapedChar(), *Far->GetPathName().ReplaceCharWithEscapedChar(),
				Registry->IsDiscovered(StartId) ? TEXT("true") : TEXT("false"), Registry->IsDiscovered(FarId) ? TEXT("true") : TEXT("false"),
				StartLocation.X, StartLocation.Y, StartLocation.Z, FarLocation.X, FarLocation.Y, FarLocation.Z,
				FVector::Dist2D(StartLocation, FarLocation), ApproachTimeout));
			// 깨끗한 runner userdir 은 발견 없이 시작하므로 실제 Auto 상호작용을 먼저 거쳐 간다.
			if (!Registry->IsDiscovered(StartId))
			{
				AddVisit(Ctx, Start);
			}

			if (!Registry->IsDiscovered(FarId))
			{
				AddVisit(Ctx, Far);
			}

			AddVisit(Ctx, Start);
			Phase = EPhase::Discover;
		}

		if (!Points[0].IsValid() || !Points[1].IsValid())
		{
			return Fail(TEXT("FastTravel registered point disappeared"));
		}

		if (bRequestArmed)
		{
			bSawTravel |= Travel->IsTravelling();
		}

		if (Phase == EPhase::Discover || Phase == EPhase::Approach || Phase == EPhase::Open || Phase == EPhase::Menu)
		{
			const auto State = Sequence.Tick(Ctx);
			if (State == EVBPlayBotState::Failed)
			{
				Ctx.Checks->Expect(Phase == EPhase::Discover ? TEXT("FastTravel.DiscoveredBoth") : LegName() + TEXT(".InputRoute"),
					false, true, Ctx.Frame,
					FString::Printf(TEXT("{\"reason\":\"%s\"}"), *Sequence.GetFailure().ReplaceCharWithEscapedChar()));
				return FinishChecklist(Ctx, Sequence.GetFailure());
			}

			if (State == EVBPlayBotState::Running)
			{
				return State;
			}

			if (Phase == EPhase::Discover)
			{
				const bool bDiscovered = Registry->IsDiscovered(StartId) && Registry->IsDiscovered(FarId);
				Ctx.Checks->Expect(TEXT("FastTravel.DiscoveredBoth"), bDiscovered, true, Ctx.Frame);
				if (!bDiscovered)
				{
					return FinishChecklist(Ctx, TEXT("FastTravel discovery did not complete"));
				}

				BeginLeg(Ctx);
			}
			else if (Phase == EPhase::Approach)
			{
				Sequence.Reset();
				PhaseStart = NextCorrection = Ctx.Frame;
				Phase = EPhase::Face;
			}
			else if (Phase == EPhase::Open)
			{
				if (!BuildMenu(Ctx))
				{
					return FinishChecklist(Ctx, GetFailure());
				}
			}
			else if (Phase == EPhase::Menu)
			{
				Ctx.Checks->Expect(LegName() + TEXT(".InputRoute"), bRequestArmed && bConfirmInjected, true, Ctx.Frame);
				StableFrame = -1;
				Phase = EPhase::Arrive;
			}
		}

		if (Phase == EPhase::Face)
		{
			UVBInteractableComponent* Component = TravelInteractable.Get();
			if (!Component)
			{
				return Fail(TEXT("FastTravel source Tap component missing"));
			}

			if (Interactor->CanReach(Component) && Interactor->FindBestInteractable() == Component && Settled(Ctx))
			{
				Ctx.Input->ReleaseAll(Ctx.Frame);
				Sequence.Reset();
				Sequence.Then<FVBSkillPress>(LegName() + TEXT(" open through interact"), Interact);
				Sequence.Then<FVBSkillWaitUntil>(LegName() + TEXT(" wait for screen"), [this](FVBPlayBotContext& C2) { return Screen(C2) != nullptr; }, DiscoveryTimeout);
				Phase = EPhase::Open;
			}
			else
			{
				if (Ctx.Frame >= NextCorrection)
				{
					const double Desired = (Component->GetComponentLocation() - Ctx.Player->GetActorLocation()).Rotation().Yaw;
					const double Delta = FMath::FindDeltaAngleDegrees(double(Ctx.PC->GetControlRotation().Yaw), Desired);
					Ctx.Input->SetLookWant(FVector2D(FMath::Abs(Delta) > 3 ? FMath::Sign(Delta) * 0.3 : 0, 0));
					NextCorrection = Ctx.Frame + Ctx.MsToFrames(Ctx.Human->SampleCorrectionIntervalMs());
				}

				if (Ctx.Frame - PhaseStart > Ctx.SecondsToFrames(DiscoveryTimeout))
				{
					const auto* Config = Component->GetConfig();
					const auto& Rules = Config->ReachRules;
					const FVector Origin = Ctx.Player->GetActorLocation(), Location = Component->GetComponentLocation();
					FHitResult Blocker;
					FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayBotTravelFocus), false, Ctx.Player);
					Query.AddIgnoredActor(Component->GetOwner());
					const bool bBlocked = Rules.bRequireLineOfSight && Ctx.World->SweepSingleByChannel(Blocker, Origin, Location,
						FQuat::Identity, Rules.LineOfSightChannel, FCollisionShape::MakeCapsule(Rules.LineOfSightRadius,
							Rules.LineOfSightHalfHeight), Query);
					Ctx.Checks->Expect(LegName() + TEXT(".UIOpened"), false, true, Ctx.Frame,
						FString::Printf(TEXT("{\"component\":\"%s\",\"config\":\"%s\",\"best\":\"%s\",\"reachable\":%s,\"settled\":%s,\"distanceCm\":%.3f,\"rangeCm\":%.3f,\"heightCm\":%.3f,\"maxHeightCm\":%.3f,\"yawError\":%.3f,\"maxYaw\":%.3f,\"losBlocked\":%s,\"blocker\":\"%s\"}"),
							*Component->GetPathName().ReplaceCharWithEscapedChar(), *Config->GetPathName().ReplaceCharWithEscapedChar(),
							*GetPathNameSafe(Interactor->FindBestInteractable()).ReplaceCharWithEscapedChar(),
							Interactor->CanReach(Component) ? TEXT("true") : TEXT("false"), Settled(Ctx) ? TEXT("true") : TEXT("false"),
							FVector::Dist(Origin,
								Location), Config->InteractRange, FMath::Abs(Origin.Z - Location.Z), Rules.MaxHeightDifference,
							FMath::Abs(FMath::FindDeltaAngleDegrees(double(Ctx.PC->GetControlRotation().Yaw),
								double((Location - Origin).Rotation().Yaw))), Rules.MaxYawAngle,
							bBlocked ? TEXT("true") : TEXT("false"), *GetPathNameSafe(Blocker.GetActor()).ReplaceCharWithEscapedChar()));
					return FinishChecklist(Ctx, TEXT("FastTravel cannot focus source travel interaction"));
				}
			}
		}

		if (Phase == EPhase::Arrive)
		{
			const FVector Destination = Points[1 - Leg]->GetArrivalTransform().GetLocation();
			const FVector Actual = Ctx.Player->GetActorLocation();
			const double Feet = Actual.Z - Ctx.Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			const bool bPlaced = FVector::Dist2D(Actual, Destination) <= ArrivalRadius && FMath::Abs(Feet - Destination.Z) <= FeetTolerance;
			const APlayerCameraManager* Camera = Ctx.PC->PlayerCameraManager;
			const bool bFadeClear = Camera && (!Camera->bEnableFading || Camera->FadeAmount <= FadeTolerance);
			const bool bCompleted = Observer->Completions > RestoreBaseline && !Restore->IsRestoring() && !Travel->IsTravelling();
			const bool bClosed = !Travel->GetView().IsActive() && !Screen(Ctx);
			const bool bReady = bRequestArmed && bConfirmInjected && bSawTravel && bCompleted && bPlaced && bClosed && bFadeClear && Settled(Ctx);
			StableFrame = bReady ? (StableFrame < 0 ? Ctx.Frame : StableFrame) : -1;
			const bool bStable = StableFrame >= 0 && Ctx.Frame - StableFrame >= Ctx.SecondsToFrames(StableSeconds);
			if (bStable || Ctx.Frame - RequestFrame > Ctx.SecondsToFrames(TravelTimeout))
			{
				const FString Evidence = FString::Printf(TEXT("{\"destination\":\"%s\",\"xyCm\":%.3f,\"feetErrorCm\":%.3f,\"restoreCompletions\":%d,\"travelSeen\":%s,\"fadeClear\":%s}"),
					*Points[1 - Leg]->GetPointId().ToString(), FVector::Dist2D(Actual, Destination), FMath::Abs(Feet - Destination.Z),
					Observer->Completions - RestoreBaseline, bSawTravel ? TEXT("true") : TEXT("false"), bFadeClear ? TEXT("true") : TEXT("false"));
				Ctx.Checks->Expect(LegName() + TEXT(".Transition"), bSawTravel && bCompleted, true, Ctx.Frame, Evidence);
				Ctx.Checks->Expect(LegName() + TEXT(".Arrival"), bStable, true, Ctx.Frame, Evidence);
				Ctx.Checks->Expect(LegName() + TEXT(".UIClosed"), bClosed, true, Ctx.Frame);
				Ctx.Checks->Expect(LegName() + TEXT(".FadeCleared"), bFadeClear, true, Ctx.Frame);
				StableArrivals[Leg] = bStable;
				if (!bStable)
				{
					return FinishChecklist(Ctx, TEXT("FastTravel did not complete stable arrival before deadline"));
				}

				if (++Leg >= 2)
				{
					return FinishChecklist(Ctx);
				}

				BeginLeg(Ctx);
			}
		}

		return EVBPlayBotState::Running;
	}

	void Teardown(FVBPlayBotContext& Ctx) override
	{
		if (Travel.IsValid())
		{
			Travel->OnViewChanged.Remove(ViewHandle);
		}

		if (Restore.IsValid() && Observer.IsValid())
		{
			Restore->OnRestoreCompleted.RemoveDynamic(Observer.Get(), &UVBPlayBotTravelObserver::OnRestored);
		}

		Ctx.Input->ReleaseAll(Ctx.Frame);
		Ctx.OnPressInjected = nullptr;
		Observer.Reset();
	}
private:
	EVBPlayBotState FinishChecklist(FVBPlayBotContext& Ctx, const FString& Reason = FString())
	{
		Ctx.Checks->Expect(TEXT("FastTravel.RoundTrip"), StableArrivals[0] && StableArrivals[1], true, Ctx.Frame,
			FString::Printf(TEXT("{\"outboundStable\":%s,\"returnStable\":%s,\"reason\":\"%s\"}"),
				StableArrivals[0] ? TEXT("true") : TEXT("false"), StableArrivals[1] ? TEXT("true") : TEXT("false"),
				*Reason.ReplaceCharWithEscapedChar()));
		if (!Reason.IsEmpty())
		{
			Ctx.Checks->Unavailable(TEXT("FastTravel.Remaining"), Ctx.Frame,
			TEXT("dependent travel steps were not exercised after the recorded failure; no forced recovery"));
		}

		return EVBPlayBotState::Succeeded;
	}

	bool Settled(const FVBPlayBotContext& Ctx) const
	{
		const UWorldPartitionSubsystem* Partition = Ctx.World->GetSubsystem<UWorldPartitionSubsystem>();
		return FVBPlayBotContext::IsGrounded(Ctx.Player) && !Restore->IsRestoring() && Ctx.Player->GetVelocity().Size2D() < 5
			&& (!Partition || Partition->IsStreamingCompleted());
	}

	void AddVisit(FVBPlayBotContext& Ctx, AVBSavePoint* Point)
	{
		TWeakObjectPtr<AVBSavePoint> Weak = Point;
		double Range = Point->GetTravelInteractRange();
		TArray<UVBInteractableComponent*> Components;
		Point->GetComponents(Components);
		for (const UVBInteractableComponent* Component : Components)
		{
			if (Component->GetConfig()->Mode == EVBInteractMode::Auto)
			{
				Range = FMath::Min(Range, double(Component->GetConfig()->InteractRange));
			}
		}

		Sequence.Then<FVBSkillApproach>(TEXT("walk to discover ") + Point->GetPointId().ToString(), [Weak](FVBPlayBotContext&) { return Weak.Get(); }, Range * 0.6, ApproachTimeout);
		Sequence.Then<FVBSkillWaitUntil>(TEXT("actual Auto discovery and streaming"), [this, Weak](FVBPlayBotContext& C2)
		{
			return Weak.IsValid() && Registry.IsValid() && Registry->IsDiscovered(Weak->GetPointId()) && Settled(C2);
		}, DiscoveryTimeout);
	}

	void BeginLeg(FVBPlayBotContext& Ctx)
	{
		Ctx.Input->ReleaseAll(Ctx.Frame);
		bRequestArmed = bConfirmInjected = bSawTravel = false;
		Sequence.Reset();
		TravelInteractable.Reset();
		TArray<UVBInteractableComponent*> Components;
		Points[Leg]->GetComponents(Components);
		for (UVBInteractableComponent* Component : Components)
		{
			if (Component->IsInteractionEnabled() && Component->GetConfig()->Mode == EVBInteractMode::Tap)
			{
				TravelInteractable = Component;
			}
		}

		Sequence.Then<FVBSkillApproach>(LegName() + TEXT(" approach source"), [this](FVBPlayBotContext&) { return Points[Leg].Get(); },
			Points[Leg]->GetTravelInteractRange() * 0.6, ApproachTimeout);
		Phase = EPhase::Approach;
	}

	UVBFastTravelScreenWidget* Screen(FVBPlayBotContext& Ctx) const
	{
		UVBFastTravelScreenWidget* Widget = Cast<UVBFastTravelScreenWidget>(Ctx.FindWidget(ScreenClass.Get()));
		return Widget && Widget->IsActivated() && Widget->IsVisible() && Widget->GetOwningPlayer() == Ctx.PC ? Widget : nullptr;
	}

	bool BuildMenu(FVBPlayBotContext& Ctx)
	{
		const auto& View = Travel->GetView();
		const FVBFastTravelEntry* Destination = View.Entries.FindByPredicate([this](const FVBFastTravelEntry& E) { return E.PointId == Points[1 - Leg]->GetPointId(); });
		const UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Ctx.Player);
		const bool bOpened = Screen(Ctx) && UI && UI->IsScreenOpen(ScreenTag) && View.FromPointId == Points[Leg]->GetPointId() && Destination;
		Ctx.Checks->Expect(LegName() + TEXT(".UIOpened"), bOpened, true, Ctx.Frame);
		if (!bOpened) { Fail(TEXT("FastTravel interact did not open the expected source/destination view")); return false; }
		const FString Name = Destination->DisplayName.ToString();
		if (Name.IsEmpty() || View.Entries.FilterByPredicate([&Name](const FVBFastTravelEntry& E) { return E.DisplayName.ToString() == Name; }).Num() != 1)
		{
			Ctx.Checks->Unavailable(LegName() + TEXT(".Selected"), Ctx.Frame,
				TEXT("destination display name is empty or ambiguous for menu navigation"));
			Fail(TEXT("FastTravel destination label is ambiguous"));
			return false;
		}

		TMap<FString, FString> Vars;
		Vars.Add(TEXT("Destination"), Name);
		Sequence.Reset();
		FString Error;
		if (!VBPlayBotSkills::AddMenuPath(Sequence, Ctx.Config->GetArray(TEXT("DestinationRoute")), Vars, Error))
		{
			Fail(Error);
			return false;
		}

		Sequence.Then<FVBSkillDo>(LegName() + TEXT(" verify actual selected detail"), [this, Name](FVBPlayBotContext& C2, FString& E)
		{
			UVBFastTravelScreenWidget* Widget = Screen(C2);
			const UTextBlock* Text = Widget ? Cast<UTextBlock>(Widget->GetWidgetFromName(DestinationText)) : nullptr;
			const bool bSelected = Text && Text->GetText().ToString() == Name;
			C2.Checks->Expect(LegName() + TEXT(".Selected"), bSelected, true, C2.Frame,
				FString::Printf(TEXT("{\"expectedText\":\"%s\",\"actualText\":\"%s\"}"), *Name.ReplaceCharWithEscapedChar(), Text ? *Text->GetText().ToString().ReplaceCharWithEscapedChar() : TEXT("<missing>")));
			if (!bSelected)
			{
				E = TEXT("FastTravel menu did not select the requested destination row");
			}

			return bSelected;
		});
		if (!VBPlayBotSkills::AddMenuPath(Sequence, Ctx.Config->GetArray(TEXT("TravelButtonRoute")), Vars, Error))
		{
			Fail(Error);
			return false;
		}

		Sequence.Then<FVBSkillDo>(LegName() + TEXT(" verify actual travel button focus"), [this](FVBPlayBotContext& C2, FString& E)
		{
			UVBFastTravelScreenWidget* Widget = Screen(C2);
			const UButton* Button = Widget ? Cast<UButton>(Widget->GetWidgetFromName(TravelButton)) : nullptr;
			const bool bFocused = Button && Button->GetIsEnabled() && (Button->HasUserFocus(C2.PC) || Button->HasKeyboardFocus());
			const bool bDistinct = FVector::Dist2D(C2.Player->GetActorLocation(),
				Points[1 - Leg]->GetArrivalTransform().GetLocation()) > ArrivalRadius;
			C2.Checks->Expect(LegName() + TEXT(".ConfirmFocused"), bFocused && bDistinct, true, C2.Frame);
			if (!bFocused || !bDistinct) { E = TEXT("FastTravel confirmation is unfocused or source is already within arrival tolerance"); return false; }
			bRequestArmed = true;
			bSawTravel = false;
			RequestFrame = C2.Frame;
			RestoreBaseline = Observer->Completions;
			return true;
		});
		if (!VBPlayBotSkills::AddMenuPath(Sequence, Ctx.Config->GetArray(TEXT("ConfirmRoute")), Vars, Error))
		{
			Fail(Error);
			return false;
		}

		Phase = EPhase::Menu;
		return true;
	}

	FString LegName() const { return Leg == 0 ? TEXT("FastTravel.Outbound") : TEXT("FastTravel.Return"); }
	enum class EPhase : uint8 { Resolve, Discover, Approach, Face, Open, Menu, Arrive };
	EPhase Phase = EPhase::Resolve;
	FVBPlayBotSequence Sequence;
	TWeakObjectPtr<AVBSavePoint> Points[2];
	TWeakObjectPtr<UVBFastTravelComponent> Travel;
	TWeakObjectPtr<UVBPlayerRestoreComponent> Restore;
	TWeakObjectPtr<UVBTravelPointSubsystem> Registry;
	TWeakObjectPtr<UVBInteractorComponent> Interactor;
	TWeakObjectPtr<UVBInteractableComponent> TravelInteractable;
	TWeakObjectPtr<UClass> ScreenClass;
	TStrongObjectPtr<UVBPlayBotTravelObserver> Observer;
	FDelegateHandle ViewHandle;
	FGameplayTag StartId, FarId, ScreenTag;
	FName Interact, TravelButton, DestinationText;
	FKey ConfirmKey;
	int32 Leg = 0, RestoreBaseline = 0;
	int64 PhaseStart = 0, NextCorrection = 0, RequestFrame = 0, StableFrame = -1;
	double ArrivalRadius = 150, FeetTolerance = 50, SearchRadius = 50000, ApproachTimeout = 120, TravelTimeout = 20, StableSeconds = 0.5, FadeTolerance = 0.02, DiscoveryTimeout = 5;
	bool bRequestArmed = false, bConfirmInjected = false, bSawTravel = false;
	bool StableArrivals[2] = { false, false };
};
VB_PLAYBOT_SCENARIO(FScenario, "FastTravel")
}
